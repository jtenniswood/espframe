import { createHash, randomUUID } from 'node:crypto'
import { once } from 'node:events'
import { createServer } from 'node:http'
import { isIPv4 } from 'node:net'
import sharp from 'sharp'
import { createSlideService } from './service.ts'
import { readCapturedPreviews } from './replay_library.ts'
import type { ReplayAsset } from './replay_library.ts'

const host = process.env.REPLAY_HOST ?? '127.0.0.1'
const port = Number(process.env.REPLAY_PORT ?? 9116)
const warmRotation = process.env.REPLAY_WARM_ROTATION
const warmPairFill = process.env.REPLAY_WARM_PAIR_FILL === '1'
const unavailable = process.env.REPLAY_PREPARED_UNAVAILABLE === '1'
const outageHistory = process.env.REPLAY_OFFLINE_HISTORY === '1'
const captureDirectory = process.env.REPLAY_PREVIEW_DIRECTORY
const thumbnailDirectory = process.env.REPLAY_THUMBNAIL_DIRECTORY
const failFirstDetail = process.env.REPLAY_FAIL_DETAIL_ONCE === '1'
const previewSize = Number(process.env.REPLAY_PREVIEW_SIZE ?? '0')
const baselineQuality = Number(process.env.REPLAY_BASELINE_JPEG_QUALITY ?? '0')
const webpQuality = Number(process.env.REPLAY_PREVIEW_WEBP_QUALITY ?? '0')
const previewStallMs = Number(process.env.REPLAY_PREVIEW_STALL_MS ?? '0')
if (!isIPv4(host) || !Number.isInteger(port) || port < 1 || port > 65535 ||
    (warmRotation !== undefined && warmRotation !== '0' && warmRotation !== '180')) {
  throw new Error('Set an IPv4 REPLAY_HOST, TCP REPLAY_PORT, and optional REPLAY_WARM_ROTATION (0 or 180)')
}
if (outageHistory && unavailable) {
  throw new Error('REPLAY_OFFLINE_HISTORY starts with an available service')
}
if (warmPairFill && warmRotation === undefined) {
  throw new Error('Set REPLAY_WARM_ROTATION when prewarming Fill pairs')
}
if (![0, 360, 720, 1080, 1440].includes(previewSize)) {
  throw new Error('Set REPLAY_PREVIEW_SIZE to 360, 720, 1080 or 1440, or leave it unset')
}
if ([baselineQuality, webpQuality].some(quality => !Number.isInteger(quality) || quality < 0 || quality > 100) ||
    (baselineQuality && webpQuality)) {
  throw new Error('Set either REPLAY_BASELINE_JPEG_QUALITY or REPLAY_PREVIEW_WEBP_QUALITY to 1–100, or leave both unset')
}
if (!Number.isInteger(previewStallMs) || previewStallMs < 0 || previewStallMs > 10_000) {
  throw new Error('Set REPLAY_PREVIEW_STALL_MS to a whole number between 0 and 10000')
}
if (captureDirectory && (previewSize || baselineQuality || webpQuality || warmPairFill)) {
  throw new Error('Captured previews use their original encoded bytes and Fit pairs')
}
if ((thumbnailDirectory && !captureDirectory) || (failFirstDetail && !thumbnailDirectory)) {
  throw new Error('Staged detail requires matching preview and thumbnail captures')
}
const immichKey = 'espframe-synthetic-preview'
const token = 'espframe-synthetic-slide-replay-only'
const origin = `http://${host}:${port}`
const service = createSlideService({ immichUrl: origin, immichKey, token, maxAgeMs: 600_000 })
const images = new Map<string, Buffer>()
const library: ReplayAsset[] = []
const fingerprint = createHash('sha256')
const sourceFingerprint = createHash('sha256')

function addPreview(asset: ReplayAsset, bytes: Buffer) {
  library.push(asset)
  images.set(asset.id, bytes)
  fingerprint.update(JSON.stringify(asset)).update(String(bytes.length) + '\n').update(bytes)
}

if (captureDirectory) {
  for await (const { asset, bytes } of readCapturedPreviews(captureDirectory)) addPreview(asset, bytes)
} else {
  for (let index = 0; index < 72; index++) {
    const width = 960
    const height = 1440
    const pixels = Buffer.alloc(width * height * 3)
    const ridge = Array.from({ length: width }, (_, x) => 680 + Math.sin(x / 70 + index) * 180)
    let noise = index + 4321
    const clamp = (value: number) => Math.max(0, Math.min(255, value))
    for (let y = 0; y < height; y++) {
      const layers = Math.cos(y / (13 + index % 12)) * 27
      for (let x = 0; x < width; x++) {
        noise = (Math.imul(noise, 1664525) + 1013904223) >>> 0
        const terrain = (y > ridge[x] ? 40 : 85) + layers + (noise >>> 26)
        const i = (y * width + x) * 3
        pixels[i] = clamp(terrain + (x + index * 19) % 64)
        pixels[i + 1] = clamp(terrain + (y + index * 13) % 72)
        pixels[i + 2] = clamp(terrain + 60)
      }
    }
    const id = `${(index + 1).toString(16).padStart(8, '0')}-1111-4222-8333-123456789abc`
    const image = sharp(pixels, { raw: { width, height, channels: 3 } })
    if (previewSize) {
      // Immich resizes in linear light and fits the image outside the square,
      // so the configured size describes the short edge for these portraits.
      image.pipelineColourspace('scrgb').resize(previewSize, previewSize, { fit: 'outside', withoutEnlargement: true })
        .toColourspace('srgb')
    }
    const preview = baselineQuality
      ? image.jpeg({ quality: baselineQuality, chromaSubsampling: baselineQuality >= 80 ? '4:4:4' : '4:2:0' })
      : webpQuality || index % 3 === 2 ? image.webp({ quality: webpQuality || 84 })
        : image.jpeg({ quality: 87, progressive: index % 3 === 1 })
    const bytes = await preview.toBuffer()
    const date = new Date(Date.UTC(2024, 0, Math.floor(index / 2) * 7 + 1, 12))
    const asset = { id, width, height, localDateTime: date.toISOString() }
    sourceFingerprint.update(JSON.stringify(asset)).update(pixels)
    addPreview(asset, bytes)
  }
}

const thumbnails = new Map<string, Buffer>()
const thumbnailFingerprint = createHash('sha256')
if (thumbnailDirectory) {
  for await (const { asset, bytes } of readCapturedPreviews(thumbnailDirectory)) {
    const preview = library[thumbnails.size]
    if (preview?.id !== asset.id || preview.localDateTime !== asset.localDateTime) {
      throw new Error('Thumbnail IDs and dates must match the preview manifest in order')
    }
    thumbnails.set(asset.id, bytes)
    thumbnailFingerprint.update(JSON.stringify(asset)).update(String(bytes.length) + '\n').update(bytes)
  }
  if (thumbnails.size !== library.length) {
    throw new Error('Preview and thumbnail manifests differ in length')
  }
}
const replayProfile = JSON.stringify({
  run: randomUUID(), sha256: fingerprint.digest('hex'), sourceSha256: captureDirectory ? undefined : sourceFingerprint.digest('hex'),
  capturedPreviews: Boolean(captureDirectory),
  previewSize, baselineQuality, webpQuality, previewStallMs,
  warmRotation: warmRotation ?? null, warmPairFill,
  thumbnailSha256: thumbnailDirectory ? thumbnailFingerprint.digest('hex') : undefined,
  failFirstDetail: thumbnailDirectory ? failFirstDetail : undefined,
})
const stalledPreviews = { started: 0, completed: 0, interrupted: 0 }
let failedDetails = 0
let offset = 0
const history = new Set<string>()
let offline = false
let recordHistory = false
let applyPreviewStalls = false
const server = createServer(async (request, response) => {
  const path = request.url ?? ''
  if (request.method === 'GET' && path === '/replay-profile') {
    return response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }).end(replayProfile)
  }
  if (request.method === 'GET' && path === '/replay-stats') {
    return response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' })
      .end(JSON.stringify({ stalledPreviews, failedDetails: thumbnailDirectory ? failedDetails : undefined }))
  }
  if (offline) {
    console.log(JSON.stringify({ offline: path }))
    return response.writeHead(503).end()
  }
  if (path.startsWith('/v1/slides/')) {
    response.on('finish', () => {
      console.log(JSON.stringify({ prepared: path, status: response.statusCode,
        cache: response.getHeader('X-Espframe-Cache'), bytes: response.getHeader('Content-Length') }))
      if (recordHistory && response.statusCode === 200 && !offline) {
        history.add(path)
        if (history.size === 10) {
          offline = true
          console.log('Replay photo endpoints unavailable after 10 prepared slides')
        }
      }
    })
    if (unavailable) return response.writeHead(503).end()
    service.emit('request', request, response)
    return
  }
  if (request.headers['x-api-key'] !== immichKey) return response.writeHead(401).end()
  if (request.method === 'GET' && path === '/api/server/version') {
    return response.writeHead(200, { 'Content-Type': 'application/json' }).end('{"major":2,"minor":5}')
  }
  if (request.method === 'POST' && path === '/api/search/random') {
    try {
      let body = ''
      for await (const chunk of request) {
        body += chunk
        if (body.length > 4096) return response.writeHead(413).end()
      }
      const query = JSON.parse(body)
      const size = Number(query.size)
      if (!Number.isInteger(size) || size < 1 || size > 16) return response.writeHead(400).end()
      let assets
      if (typeof query.takenAfter === 'string' && typeof query.takenBefore === 'string') {
        assets = library.filter(photo => query.takenAfter <= photo.localDateTime && photo.localDateTime <= query.takenBefore)
      } else {
        assets = library.slice(offset, offset + size)
        offset = (offset + assets.length) % library.length
      }
      const data = JSON.stringify(assets.slice(0, size))
      return response.writeHead(200, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) }).end(data)
    } catch {
      return response.writeHead(400).end()
    }
  }
  const image = /^\/api\/assets\/([a-f0-9-]+)\/thumbnail\?size=(preview|thumbnail)$/.exec(path)
  const id = image?.[1]
  const thumbnail = image?.[2] === 'thumbnail'
  const bytes = id ? (thumbnail ? thumbnails : images).get(id) : undefined
  if (!id || !bytes || request.method !== 'GET') return response.writeHead(404).end()
  if (!thumbnail && failFirstDetail && !failedDetails) {
    ++failedDetails
    console.log(JSON.stringify({ preview: id, status: 503 }))
    return response.writeHead(503).end()
  }
  if (thumbnail) {
    console.log(JSON.stringify({ thumbnail: id, bytes: bytes.length }))
    return response.writeHead(200, { 'Content-Length': bytes.length }).end(bytes)
  }
  console.log(JSON.stringify({ preview: id, bytes: bytes.length }))
  response.writeHead(200, { 'Content-Length': bytes.length })
  if (!applyPreviewStalls || Number.parseInt(id.slice(0, 8), 16) % 8 !== 0) {
    response.end(bytes)
    return
  }
  // Use the same slots on every run. Write the headers and some data first
  // so this tests a stalled body on an established connection.
  ++stalledPreviews.started
  const halfway = Math.floor(bytes.length / 2)
  let finish: NodeJS.Timeout | undefined
  response.once('finish', () => { ++stalledPreviews.completed })
  response.once('close', () => {
    clearTimeout(finish)
    if (!response.writableFinished) ++stalledPreviews.interrupted
  })
  response.write(bytes.subarray(0, halfway), error => {
    if (!error && !response.destroyed) finish = setTimeout(() => response.end(bytes.subarray(halfway)), previewStallMs)
  })
})

await once(server.listen(port, host), 'listening')
if (warmRotation !== undefined) {
  for (let index = 0; index < library.length; index += 2) {
    // The native pending queue selects its next photo from the back.
    const query = `companion=${library[index].id}&rotation=${warmRotation}&mode=${warmPairFill ? 'fill' : 'fit'}`
    const response = await fetch(`${origin}/v1/slides/${library[index + 1].id}?${query}`, {
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!response.ok) throw new Error(`Replay preparation failed (${response.status})`)
    await response.arrayBuffer()
  }
}
recordHistory = outageHistory
applyPreviewStalls = previewStallMs > 0
const encoding = baselineQuality ? `baseline JPEG quality ${baselineQuality}`
  : webpQuality ? `WebP quality ${webpQuality}` : 'mixed JPEG and WebP'
if (captureDirectory) console.log(`Replay captured ${library.length} unchanged Immich previews`)
else console.log(`Replay previews ${previewSize || 'source size'}, ${encoding}`)
if (previewStallMs) console.log(`Replay pauses every eighth preview for ${previewStallMs} ms midway through the response`)
console.log(`Replay ready at ${origin} (${unavailable ? 'service unavailable' : warmRotation === undefined ? 'cold' : 'prewarmed service cache'})`)
