import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createServer } from 'node:net'
import { createInterface } from 'node:readline'
import test from 'node:test'
import { setTimeout as sleep } from 'node:timers/promises'

test('replay stalls every eighth preview and counts mid-response disconnects', { timeout: 30_000 }, async t => {
  const socket = createServer()
  await once(socket.listen(0, '127.0.0.1'), 'listening')
  const address = socket.address()
  assert.ok(address && typeof address !== 'string')
  await new Promise<void>((resolve, reject) => socket.close(error => error ? reject(error) : resolve()))

  const stallMs = 1000
  const child = spawn(process.execPath, ['replay.ts'], {
    cwd: new URL('.', import.meta.url),
    env: { ...process.env, REPLAY_HOST: '127.0.0.1', REPLAY_PORT: String(address.port), REPLAY_PREVIEW_STALL_MS: String(stallMs),
      REPLAY_PREVIEW_SIZE: '360', REPLAY_PREVIEW_WEBP_QUALITY: '80', REPLAY_BASELINE_JPEG_QUALITY: undefined,
      REPLAY_WARM_ROTATION: undefined, REPLAY_PREPARED_UNAVAILABLE: undefined, REPLAY_OFFLINE_HISTORY: undefined },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  const exited = once(child, 'exit')
  t.after(async () => {
    child.kill()
    await exited
  })
  let errors = ''
  child.stderr.setEncoding('utf8').on('data', data => { errors += data })
  const ready = Promise.withResolvers<void>()
  createInterface({ input: child.stdout }).on('line', line => {
    if (line.startsWith('Replay ready at ')) ready.resolve()
  })
  await Promise.race([ready.promise, exited.then(() => { throw new Error(`Replay server exited\n${errors}`) })])

  const origin = `http://127.0.0.1:${address.port}`
  const metadata = async (path: string) => {
    const response = await fetch(origin + path, { signal: AbortSignal.timeout(5000) })
    assert.ok(response.ok)
    return response.json()
  }
  const profile = await metadata('/replay-profile')
  assert.equal(profile.previewStallMs, stallMs)
  assert.equal(profile.previewSize, 360)
  assert.equal(profile.webpQuality, 80)
  assert.match(profile.sourceSha256, /^[a-f0-9]{64}$/)
  assert.notEqual(profile.sourceSha256, profile.sha256)
  const preview = (id: string, signal = AbortSignal.timeout(5000)) =>
    fetch(`${origin}/api/assets/${id}-1111-4222-8333-123456789abc/thumbnail?size=preview`, {
      headers: { 'x-api-key': 'espframe-synthetic-preview' }, signal,
    })
  const normal = await preview('00000007')
  assert.ok(normal.ok)
  const bytes = Buffer.from(await normal.arrayBuffer())
  assert.equal(bytes.toString('ascii', 8, 12), 'WEBP')
  assert.deepEqual((await metadata('/replay-stats')).stalledPreviews, { started: 0, completed: 0, interrupted: 0 })

  const controller = new AbortController()
  const partial = await preview('00000008', AbortSignal.any([controller.signal, AbortSignal.timeout(5000)]))
  assert.ok(partial.ok && partial.body)
  const total = Number(partial.headers.get('Content-Length'))
  const first = await partial.body.getReader().read()
  assert.ok(first.value?.length && first.value.length <= Math.floor(total / 2))
  controller.abort()

  const start = performance.now()
  const complete = await preview('00000008')
  assert.ok(complete.ok && (await complete.arrayBuffer()).byteLength === total)
  assert.ok(performance.now() - start >= stallMs - 20)
  for (let retry = 0; retry < 20; ++retry) {
    const { stalledPreviews } = await metadata('/replay-stats')
    if (stalledPreviews.interrupted) {
      assert.deepEqual(stalledPreviews, { started: 2, completed: 1, interrupted: 1 })
      return
    }
    await sleep(25)
  }
  assert.fail('The replay did not count the interrupted body')
})
