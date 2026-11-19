import { once } from 'node:events'
import { createServer } from 'node:http'
import { isIPv4 } from 'node:net'
import sharp from 'sharp'
import { createSlideService } from './service.ts'

const host = process.env.REPLAY_HOST ?? '127.0.0.1'
const port = Number(process.env.REPLAY_PORT ?? 9116)
const warmRotation = process.env.REPLAY_WARM_ROTATION
if (!isIPv4(host) || !Number.isInteger(port) || port < 1 || port > 65535 ||
    (warmRotation !== undefined && warmRotation !== '0' && warmRotation !== '180')) {
  throw new Error('Set an IPv4 REPLAY_HOST, TCP REPLAY_PORT, and optional REPLAY_WARM_ROTATION (0 or 180)')
}
const immichKey = 'espframe-synthetic-preview'
const token = 'espframe-synthetic-slide-replay-only'
const origin = `http://${host}:${port}`
const service = createSlideService({ immichUrl: origin, immichKey, token, maxAgeMs: 600_000 })
const images = new Map<string, Buffer>()
const library: { id: string; width: number; height: number; localDateTime: string }[] = []

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
  const bytes = await (index % 3 === 2 ? image.webp({ quality: 84 })
    : image.jpeg({ quality: 87, progressive: index % 3 === 1 })).toBuffer()
  images.set(id, bytes)
  const date = new Date(Date.UTC(2024, 0, Math.floor(index / 2) * 7 + 1, 12))
  library.push({ id, width, height, localDateTime: date.toISOString() })
}

let offset = 0
const server = createServer(async (request, response) => {
  const path = request.url ?? ''
  if (path.startsWith('/v1/slides/')) {
    response.on('finish', () => console.log(JSON.stringify({ prepared: path, status: response.statusCode,
      cache: response.getHeader('X-Espframe-Cache'), bytes: response.getHeader('Content-Length') })))
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
  const id = /^\/api\/assets\/([a-f0-9-]+)\/thumbnail\?size=preview$/.exec(path)?.[1]
  const bytes = id ? images.get(id) : undefined
  if (!bytes || request.method !== 'GET') return response.writeHead(404).end()
  console.log(JSON.stringify({ preview: id, bytes: bytes.length }))
  response.writeHead(200, { 'Content-Length': bytes.length }).end(bytes)
})

await once(server.listen(port, host), 'listening')
if (warmRotation !== undefined) {
  for (let index = 0; index < library.length; index += 2) {
    // The native pending queue selects its next photo from the back.
    const query = `companion=${library[index].id}&rotation=${warmRotation}`
    const response = await fetch(`${origin}/v1/slides/${library[index + 1].id}?${query}`, {
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!response.ok) throw new Error(`Replay preparation failed (${response.status})`)
    await response.arrayBuffer()
  }
}
console.log(`Replay ready at ${origin} (${warmRotation === undefined ? 'cold' : 'prewarmed'} service cache)`)
