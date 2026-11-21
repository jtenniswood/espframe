import { createHash, timingSafeEqual } from 'node:crypto'
import { createServer, type ServerResponse } from 'node:http'
import { setTimeout as delay } from 'node:timers/promises'
import { OrientationMismatch, prepareSlide, type SlideLayout } from './prepare.ts'

type Options = { immichUrl: string; immichKey: string; token: string; maxAgeMs?: number }
type CachedSlide = { revision: string; jpeg: Buffer; expires: number }

const assetId = /^[\da-f]{8}(?:-[\da-f]{4}){3}-[\da-f]{12}$/i
const maxPreviewBytes = 8 * 1024 * 1024
const maxCacheBytes = 24 * 1024 * 1024
const maxQueuedWarmSlides = 2
const maxWaitMs = 250

class ReplyError extends Error {
  readonly status: number

  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

function immichApi(value: string) {
  const base = new URL(value)
  if (!['http:', 'https:'].includes(base.protocol) || base.username || base.password || base.search || base.hash) {
    throw new Error('IMMICH_URL must be an HTTP(S) URL without credentials, query or fragment')
  }
  const path = base.pathname.replace(/\/+$/, '')
  base.pathname = path.endsWith('/api') ? `${path}/` : `${path}/api/`
  return base
}

async function readPreview(base: URL, key: string, id: string) {
  const url = new URL(`assets/${id}/thumbnail?size=preview`, base)
  let response: Response
  try {
    response = await fetch(url, { headers: { 'x-api-key': key }, signal: AbortSignal.timeout(12_000), redirect: 'error' })
  } catch {
    throw new ReplyError(502, 'Immich preview request failed')
  }
  if (response.status !== 200 || !response.body || Number(response.headers.get('content-length')) > maxPreviewBytes) {
    await response.body?.cancel()
    if (response.status === 404 || response.status === 410) throw new ReplyError(404, 'Preview unavailable')
    throw new ReplyError(502, 'Immich preview unavailable or too large')
  }
  const reader = response.body.getReader()
  const parts: Buffer[] = []
  let bytes = 0
  try {
    for (;;) {
      const { value, done } = await reader.read()
      if (done) break
      bytes += value.length
      if (bytes > maxPreviewBytes) throw new Error('Preview too large')
      parts.push(Buffer.from(value))
    }
  } catch {
    throw new ReplyError(502, 'Immich preview transfer failed')
  } finally {
    await reader.cancel().catch(() => {})
  }
  return Buffer.concat(parts, bytes)
}

function requestLayout(target: string) {
  if (target.length > 256 || !target.startsWith('/') || target.startsWith('//')) {
    throw new ReplyError(400, 'Invalid slide request')
  }
  const { pathname, searchParams: query } = new URL(target, 'http://localhost')
  const id = /^\/v1\/slides\/([^/]+)$/.exec(pathname)?.[1]
  if (!id) throw new ReplyError(404, 'Unknown route')
  if (Array.from(query).some(([key]) => !['companion', 'mode', 'rotation', 'orientation', 'wait', 'gap'].includes(key) ||
      query.getAll(key).length > 1)) {
    throw new ReplyError(400, 'Invalid slide options')
  }
  const primary = id.toLowerCase()
  const companion = query.get('companion')?.toLowerCase()
  const mode = query.get('mode') ?? 'fit'
  const rotation = query.get('rotation') ?? '0'
  const orientation = query.get('orientation') ?? undefined
  const wait = query.get('wait')
  const gap = query.get('gap')
  const waitMs = wait === null ? undefined : Number(wait)
  if (!assetId.test(primary) || (companion !== undefined && (!assetId.test(companion) || companion === primary)) ||
      (mode !== 'fit' && mode !== 'fill' && mode !== 'fill-frame') || (rotation !== '0' && rotation !== '180') ||
      (orientation !== undefined && orientation !== 'portrait' && orientation !== 'landscape') ||
      (gap !== null && (gap !== '0' || companion === undefined)) ||
      (wait !== null && (!/^(0|[1-9][0-9]{0,2})$/.test(wait) || Number(wait) > maxWaitMs))) {
    throw new ReplyError(400, 'Invalid slide options')
  }
  const layout: SlideLayout = { mode, rotation: rotation === '180' ? 180 : 0, orientation }
  return { primary, companion, layout, waitMs,
    key: `${primary}/${companion ?? ''}/${mode}/${rotation}/${orientation ?? ''}` }
}

const digest = (data: string | Buffer) => createHash('sha256').update(data).digest()

function sendError(response: ServerResponse, error: unknown) {
  const known = error instanceof ReplyError
  response.statusCode = known ? error.status : 500
  response.setHeader('Content-Type', 'text/plain; charset=utf-8')
  response.end(known ? error.message : 'Slide preparation failed')
}

export function createSlideService({ immichUrl, immichKey, token, maxAgeMs = 60_000 }: Options) {
  if (!immichKey || token.length < 32) throw new Error('Supply an Immich API key and a separate token of at least 32 characters')
  if (token === immichKey) throw new Error('Use separate keys for Immich and the slide service')
  const upstream = immichApi(immichUrl)
  const authorization = digest(`Bearer ${token}`)
  const cache = new Map<string, CachedSlide>()
  const inFlight = new Map<string, ReturnType<typeof prepare>>()
  const warmQueue = new Map<string, ReturnType<typeof requestLayout>>()
  let warming = false
  let cachedBytes = 0

  async function prepare(slide: ReturnType<typeof requestLayout>, previous: CachedSlide | undefined) {
    const [primary, companion] = await Promise.allSettled([
      readPreview(upstream, immichKey, slide.primary),
      slide.companion ? readPreview(upstream, immichKey, slide.companion) : undefined,
    ])
    if (primary.status === 'rejected') throw primary.reason
    if (companion.status === 'rejected') throw companion.reason
    const left = primary.value
    const right = companion.value
    const revision = digest(Buffer.concat(right ? [digest(left), digest(right)] : [digest(left)])).toString('hex')
    let jpeg = previous?.revision === revision ? previous.jpeg : undefined
    const state = jpeg ? 'revalidated' : 'miss'
    if (!jpeg) {
      try {
        jpeg = await prepareSlide(left, right, slide.layout)
      } catch (error) {
        if (error instanceof OrientationMismatch) throw new ReplyError(409, error.message)
        throw new ReplyError(422, 'Preview cannot be prepared with this layout')
      }
    }
    const replaced = cache.get(slide.key)
    if (replaced) cachedBytes -= replaced.jpeg.length
    cache.delete(slide.key)
    cache.set(slide.key, { jpeg, revision, expires: Date.now() + maxAgeMs })
    cachedBytes += jpeg.length
    for (const [oldestKey, oldest] of cache) {
      if (cache.size <= 128 && cachedBytes <= maxCacheBytes) break
      cache.delete(oldestKey)
      cachedBytes -= oldest.jpeg.length
    }
    return { jpeg, state }
  }

  function start(slide: ReturnType<typeof requestLayout>, speculative = false): ReturnType<typeof prepare> {
    warmQueue.delete(slide.key)
    if (speculative) warming = true
    const pending = prepare(slide, cache.get(slide.key)).finally(() => {
      inFlight.delete(slide.key)
      if (speculative) warming = false
      startNextWarm()
    })
    inFlight.set(slide.key, pending)
    // Preparation is shared even when all callers have taken the nonblocking reply.
    void pending.catch(() => {})
    return pending
  }

  function startNextWarm() {
    if (warming || inFlight.size >= 2) return
    const next = warmQueue.values().next().value
    if (next) start(next, true)
  }

  const server = createServer(async (request, response) => {
    response.setHeader('Cache-Control', 'private, no-store')
    try {
      if (!timingSafeEqual(authorization, digest(request.headers.authorization ?? ''))) {
        throw new ReplyError(401, 'Unauthorized')
      }
      const warmOnly = request.method === 'POST'
      if (!warmOnly && request.method !== 'GET') throw new ReplyError(405, 'Method not allowed')
      if (warmOnly) request.resume()
      const slide = requestLayout(request.url ?? '')
      const cached = cache.get(slide.key)
      let result
      if (cached && cached.expires > Date.now()) {
        cache.delete(slide.key)
        cache.set(slide.key, cached)
        if (warmOnly) {
          response.writeHead(204).end()
          return
        }
        result = { jpeg: cached.jpeg, state: 'hit' }
      } else {
        let pending = inFlight.get(slide.key)
        if (warmOnly && !pending) {
          if (!warmQueue.has(slide.key)) {
            if (warmQueue.size >= maxQueuedWarmSlides) throw new ReplyError(429, 'Slide service busy')
            warmQueue.set(slide.key, slide)
          }
          startNextWarm()
        } else if (!pending && inFlight.size < 2) {
          pending = start(slide)
        } else if (!pending && (slide.waitMs === undefined || !warmQueue.has(slide.key))) {
          throw new ReplyError(slide.waitMs === undefined ? 503 : 429, 'Slide service busy')
        }
        if (pending && !warmOnly && slide.waitMs !== 0) {
          if (slide.waitMs === undefined) {
            result = await pending
          } else {
            const timeout = new AbortController()
            try {
              result = await Promise.race([pending, delay(slide.waitMs, undefined, { signal: timeout.signal })])
            } finally {
              timeout.abort()
            }
          }
        }
        if (!result) {
          response.writeHead(202, { 'Content-Length': 0 }).end()
          return
        }
      }
      response.writeHead(200, {
        'Content-Type': 'image/jpeg', 'Content-Length': result.jpeg.length, 'X-Espframe-Cache': result.state,
      }).end(result.jpeg)
    } catch (error) {
      sendError(response, error)
    }
  })
  return server
}
