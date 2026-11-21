import assert from 'node:assert/strict'
import { once } from 'node:events'
import { createServer, type Server } from 'node:http'
import { test } from 'node:test'
import sharp from 'sharp'
import { OrientationMismatch, prepareSlide } from './prepare.ts'
import { createSlideService } from './service.ts'

const red = [220, 24, 35]
const blue = [15, 60, 220]
const green = [18, 200, 35]
const black = [0, 0, 0]

async function preview(width: number, height: number, left: number[], right: number[], webp = false, orientation = 1) {
  const pixels = Buffer.alloc(width * height * 3)
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) pixels.set(x < width / 2 ? left : right, (y * width + x) * 3)
  }
  const image = sharp(pixels, { raw: { width, height, channels: 3 } })
  if (orientation !== 1) image.withMetadata({ orientation })
  return (webp ? image.webp({ lossless: true }) : image.jpeg({ progressive: true })).toBuffer()
}

async function measure(jpeg: Buffer, rotation = 0) {
  const info = await sharp(jpeg).metadata()
  assert.equal(info.format, 'jpeg')
  assert.equal(info.width, 800)
  assert.equal(info.height, 1280)
  assert.equal(info.isProgressive, false)
  assert.equal(info.chromaSubsampling, '4:2:0')
  assert.equal(info.orientation, undefined)
  const pixels = await sharp(jpeg).raw().toBuffer()
  return (x: number, y: number, expected: number[], tolerance = 8) => {
    const row = rotation === 180 ? 1279 - x : x
    const column = rotation === 180 ? y : 799 - y
    const offset = (row * 800 + column) * 3
    const rgb = [...pixels.subarray(offset, offset + 3)]
    assert.ok(rgb.every((channel, i) => Math.abs(channel - expected[i]) <= tolerance), `${x},${y}: ${rgb} vs ${expected}`)
  }
}

test('landscape fit and fill match the panel geometry', async () => {
  const landscape = await preview(160, 80, red, blue)
  const fit = await measure(await prepareSlide(landscape, undefined, { mode: 'fit', rotation: 0 }))
  fit(300, 25, black)
  fit(300, 100, red)
  fit(950, 700, blue)
  const fill = await measure(await prepareSlide(landscape, undefined, { mode: 'fill', rotation: 180 }), 180)
  fill(10, 25, red)
  fill(1270, 775, blue)
})

test('EXIF portrait stays intact and WebP pairs meet at the centre', async () => {
  const portrait = await preview(120, 80, red, blue, false, 6)
  const lone = await measure(await prepareSlide(portrait, undefined, { mode: 'fill', rotation: 0, orientation: 'portrait' }))
  lone(10, 400, black)
  lone(600, 50, red)
  lone(600, 750, blue)
  const cropped = await measure(await prepareSlide(portrait, undefined, {
    mode: 'fill-frame', rotation: 180, orientation: 'portrait',
  }), 180)
  cropped(10, 10, red)
  cropped(1270, 790, blue)
  const companion = await preview(80, 120, green, green, true)
  const pair = await measure(await prepareSlide(portrait, companion, { mode: 'fill', rotation: 0, orientation: 'portrait' }))
  pair(100, 400 - 100, red)
  pair(100, 400 + 100, blue)
  // 4:2:0 can soften the color where the two images meet.
  pair(639, 300, red, 75)
  pair(640, 300, green, 75)
  pair(900, 400, green)
  const fitPair = await measure(await prepareSlide(portrait, companion, { mode: 'fit', rotation: 180 }), 180)
  fitPair(0, 300, black)
  fitPair(639, 300, red, 75)
  fitPair(640, 300, green, 75)
  fitPair(1279, 300, black)
  await assert.rejects(prepareSlide(await preview(120, 80, red, blue), companion, { mode: 'fit', rotation: 0 }), /two portrait/)
  await assert.rejects(prepareSlide(portrait, undefined, { mode: 'fit', rotation: 0, orientation: 'landscape' }), OrientationMismatch)
})

async function listen(server: Server) {
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  assert.ok(address && typeof address !== 'string')
  return { url: `http://127.0.0.1:${address.port}`, close: () => server.close() }
}

test('authorizes one library, bounds options and reuses unchanged prepared images', async t => {
  const id = 'a2345678-1234-1234-1234-123456789abc'
  const otherId = 'b2345678-1234-1234-1234-123456789abc'
  const headers = { Authorization: 'Bearer frame-access-key-for-a-local-test-only' }
  const immichKey = 'immich-upstream-test-only'
  let original = await preview(160, 80, red, blue)
  const calls: string[] = []
  const mock = await listen(createServer((request, response) => {
    calls.push(request.url ?? '')
    assert.equal(request.headers['x-api-key'], immichKey)
    assert.equal(request.headers.authorization, undefined)
    const known = `/photos/api/assets/${id}/thumbnail?size=preview`
    if (request.url !== known) return response.writeHead(404).end()
    response.writeHead(200, { 'Content-Type': 'image/jpeg' }).end(original)
  }))
  t.after(mock.close)
  const token = headers.Authorization.slice('Bearer '.length)
  const warm = await listen(createSlideService({ immichUrl: `${mock.url}/photos`, immichKey, token }))
  t.after(warm.close)
  const endpoint = `${warm.url}/v1/slides/${id}`
  assert.equal((await fetch(endpoint)).status, 401)
  assert.equal((await fetch(`${endpoint}?mode=fit&mode=fill`, { headers })).status, 400)
  assert.equal((await fetch(`${endpoint}?rotation=90`, { headers })).status, 400)
  assert.equal((await fetch(`${endpoint}?orientation=wide`, { headers })).status, 400)
  assert.equal((await fetch(`${endpoint}?orientation=portrait&orientation=landscape`, { headers })).status, 400)
  assert.equal((await fetch(`${endpoint}?gap=0`, { headers })).status, 400)
  assert.equal(calls.length, 0)
  const results = await Promise.all([fetch(endpoint, { headers }), fetch(endpoint, { headers })])
  for (const response of results) {
    assert.equal(response.status, 200)
    assert.equal(response.headers.get('content-type'), 'image/jpeg')
    const jpeg = Buffer.from(await response.arrayBuffer())
    const pixel = await measure(jpeg)
    pixel(300, 100, red)
  }
  assert.equal(calls.length, 1)
  assert.equal((await fetch(endpoint, { headers })).headers.get('x-espframe-cache'), 'hit')
  assert.equal(calls.length, 1)
  assert.equal((await fetch(`${endpoint}?companion=${otherId}`, { headers })).status, 404)

  const refresh = await listen(createSlideService({ immichUrl: `${mock.url}/photos/api/`, immichKey, token, maxAgeMs: 0 }))
  t.after(refresh.close)
  const refreshUrl = `${refresh.url}/v1/slides/${id}`
  const first = await fetch(refreshUrl, { headers })
  assert.equal(first.headers.get('x-espframe-cache'), 'miss')
  const initial = Buffer.from(await first.arrayBuffer())
  const same = await fetch(refreshUrl, { headers })
  assert.equal(same.headers.get('x-espframe-cache'), 'revalidated')
  assert.deepEqual(Buffer.from(await same.arrayBuffer()), initial)
  original = await preview(160, 80, green, blue)
  const changed = await fetch(refreshUrl, { headers })
  assert.equal(changed.headers.get('x-espframe-cache'), 'miss')
  const revised = await measure(Buffer.from(await changed.arrayBuffer()))
  revised(300, 100, green)

  // An earlier unfiltered hit does not satisfy an orientation check.
  assert.equal((await fetch(`${endpoint}?orientation=portrait`, { headers })).status, 409)
  const landscape = await fetch(`${endpoint}?orientation=landscape`, { headers })
  assert.equal(landscape.status, 200)
  assert.equal(landscape.headers.get('x-espframe-cache'), 'miss')
  await landscape.arrayBuffer()
  assert.equal((await fetch(`${endpoint}?orientation=landscape`, { headers })).headers.get('x-espframe-cache'), 'hit')
  original = await preview(100, 100, red, red)
  assert.equal((await fetch(`${refreshUrl}?orientation=landscape`, { headers })).status, 200)

  original = await preview(80, 120, red, blue)
  const legacyPortrait = await fetch(`${endpoint}?mode=fill&orientation=portrait`, { headers })
  assert.equal(legacyPortrait.status, 200)
  const legacyPixel = await measure(Buffer.from(await legacyPortrait.arrayBuffer()))
  legacyPixel(10, 400, black)
  const cropPortrait = await fetch(`${endpoint}?mode=fill-frame&orientation=portrait`, { headers })
  assert.equal(cropPortrait.status, 200)
  assert.equal(cropPortrait.headers.get('x-espframe-cache'), 'miss')
  const cropPixel = await measure(Buffer.from(await cropPortrait.arrayBuffer()))
  cropPixel(10, 20, red)
  cropPixel(1270, 780, blue)
  assert.equal((await fetch(`${endpoint}?mode=fill-frame&orientation=portrait`, { headers })).headers.get('x-espframe-cache'), 'hit')
})

test('warms an exact pair without waiting and leaves room for a foreground slide', { timeout: 10_000 }, async t => {
  const left = 'a2345678-1234-1234-1234-123456789abc'
  const right = 'b2345678-1234-1234-1234-123456789abc'
  const other = 'c2345678-1234-1234-1234-123456789abc'
  const next = 'd2345678-1234-1234-1234-123456789abc'
  const following = 'e2345678-1234-1234-1234-123456789abc'
  const extra = 'f2345678-1234-1234-1234-123456789abc'
  const pairPreview = await preview(80, 120, red, blue)
  const singlePreview = await preview(160, 80, green, blue)
  const { promise: resumePair, resolve: releasePair } = Promise.withResolvers<void>()
  const { promise: resumeSingle, resolve: releaseSingle } = Promise.withResolvers<void>()
  const { promise: singleStarted, resolve: startSingle } = Promise.withResolvers<void>()
  t.after(() => { releasePair(); releaseSingle() })
  const calls: string[] = []
  const mock = await listen(createServer(async (request, response) => {
    const id = request.url?.split('/')[3] ?? ''
    calls.push(id)
    if (id === left || id === right) {
      await resumePair
      return response.writeHead(200).end(pairPreview)
    }
    if (id === other) {
      startSingle()
      await resumeSingle
      return response.writeHead(200).end(singlePreview)
    }
    if (id === next || id === following) return response.writeHead(200).end(singlePreview)
    response.writeHead(404).end()
  }))
  t.after(mock.close)
  const token = 'frame-access-key-for-a-local-test-only'
  const service = await listen(createSlideService({ immichUrl: mock.url, immichKey: 'test-immich-key', token }))
  t.after(service.close)
  const headers = { Authorization: `Bearer ${token}` }
  const pair = `${service.url}/v1/slides/${left}?mode=fill&companion=${right}&rotation=180`
  const another = `${service.url}/v1/slides/${other}`
  const upcoming = `${service.url}/v1/slides/${next}`
  const later = `${service.url}/v1/slides/${following}`
  const excess = `${service.url}/v1/slides/${extra}`
  assert.equal((await fetch(pair, { method: 'POST' })).status, 401)
  assert.equal((await fetch(`${pair}&gap=4`, { headers })).status, 400)
  assert.equal((await fetch(`${pair}&gap=0&gap=0`, { headers })).status, 400)
  for (const wait of ['-1', '1.5', '251']) {
    assert.equal((await fetch(`${pair}&wait=${wait}`, { headers })).status, 400)
  }
  const accepted = await fetch(`${pair}&gap=0`, { method: 'POST', headers, body: '{}' })
  assert.equal(accepted.status, 202)
  assert.equal(await accepted.text(), '')
  assert.equal((await fetch(`${pair}&wait=0`, { headers })).status, 202)
  const started = performance.now()
  const bounded = await fetch(`${pair}&wait=40`, { headers })
  assert.equal(bounded.status, 202)
  assert.equal(await bounded.text(), '')
  assert.ok(performance.now() - started >= 30)
  assert.equal((await fetch(upcoming, { method: 'POST', headers })).status, 202)
  assert.equal((await fetch(later, { method: 'POST', headers })).status, 202)
  assert.equal((await fetch(upcoming, { method: 'POST', headers })).status, 202)
  assert.equal((await fetch(excess, { method: 'POST', headers })).status, 429)

  const foreground = fetch(another, { headers })
  await singleStarted
  assert.equal((await fetch(`${upcoming}?wait=0`, { headers })).status, 202)
  assert.deepEqual([...calls].sort(), [left, right, other])
  assert.equal((await fetch(`${excess}?wait=0`, { headers })).status, 429)
  assert.equal((await fetch(excess, { headers })).status, 503)
  releaseSingle()
  assert.equal((await foreground).status, 200)

  releasePair()
  const finished = await fetch(pair, { headers })
  const jpeg = Buffer.from(await finished.arrayBuffer())
  assert.equal(finished.status, 200)
  const pixels = await measure(jpeg, 180)
  pixels(8, 100, red)
  pixels(639, 400, blue, 60)
  pixels(640, 400, red, 60)
  const warmed = await fetch(pair, { method: 'POST', headers })
  assert.equal(warmed.status, 204)
  const fast = await fetch(`${pair}&gap=0&wait=0`, { headers })
  assert.equal(fast.headers.get('x-espframe-cache'), 'hit')
  assert.deepEqual(Buffer.from(await fast.arrayBuffer()), jpeg)
  for (const endpoint of [upcoming, later]) {
    const response = await fetch(endpoint, { headers })
    assert.equal(response.status, 200)
    await response.arrayBuffer()
  }
  assert.deepEqual(calls.sort(), [left, right, other, next, following])

  const quick = await fetch(`${another}?rotation=180&wait=250`, { headers })
  assert.equal(quick.status, 200)
  assert.equal(quick.headers.get('x-espframe-cache'), 'miss')
  await quick.arrayBuffer()
  assert.equal(calls.filter(id => id === other).length, 2)
})
