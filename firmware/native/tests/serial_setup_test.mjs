import assert from 'node:assert/strict'
import { configure, findFrame, waitForFrame } from '../installer/serial.mjs'

const prefix = 'ESPFRAME_SETUP 1 '
const challenge = 'ace6c621-8c99-4d4a-b151-7cec3ffee421'
const encoder = new TextEncoder()
const credentials = {
  ssid: 'Photo Lab',
  password: 'p"ass\\word',
  immich_url: 'https://photos.example.test/api',
  immich_key: 'test-key',
}

function mockPort({ correct = true, status = 'SAVED', network = '192.168.1.42 2',
  addresses, booting = false, supportsMigration = true } = {}) {
  let receive
  let portOpen = false
  let readable
  const writes = []
  const writable = new WritableStream({
    write(value) {
      const line = new TextDecoder().decode(value).trim()
      writes.push(line)
      const nonce = line.split(' ')[3]
      if (line.startsWith(`${prefix}HELLO`)) {
        receive.enqueue(encoder.encode(`\r\nI console line\r\n${prefix}READY ${correct ? nonce.slice(0, 16) : 'another-id'}`))
        receive.enqueue(encoder.encode(`${correct ? nonce.slice(16) : ''}\r\n`))
      } else if (line.startsWith(`${prefix}SET `) ||
          (supportsMigration && (line.startsWith(`${prefix}MIGRATE `) || line.startsWith(`${prefix}IMPORT `)))) {
        receive.enqueue(encoder.encode(`\r\n${prefix}${status} ${nonce}\r\n`))
      } else if (line.startsWith(`${prefix}STATUS`)) {
        const id = correct ? nonce : 'another-id'
        const address = addresses?.length ? addresses.shift() : network
        const reply = address === null ? `OFFLINE ${id}` : `CONNECTED ${id} ${address}`
        receive.enqueue(encoder.encode(`\r\nI normal console output\r\n${prefix}${reply}\r\n`))
      }
    },
  })
  return {
    get readable() { return readable },
    writable,
    writes,
    get openState() { return portOpen },
    async open(options) {
      assert.deepEqual(options, { baudRate: 115200 })
      assert.equal(portOpen, false)
      if (booting) {
        booting = false
        throw new DOMException('USB is restarting', 'NetworkError')
      }
      portOpen = true
      readable = new ReadableStream({ start(controller) { receive = controller } })
    },
    async close() { portOpen = false },
  }
}

const ok = mockPort()
await configure(ok, credentials, { challenge })
assert.deepEqual(ok.writes, [`${prefix}HELLO ${challenge}`, `${prefix}SET ${challenge} ${JSON.stringify(credentials)}`])
assert.equal(ok.openState, false)

const wrong = mockPort({ correct: false })
await assert.rejects(configure(wrong, credentials, { challenge, timeout: 20 }), /did not answer/)
assert.deepEqual(wrong.writes, [`${prefix}HELLO ${challenge}`])
assert.equal(wrong.openState, false)

const rejected = mockPort({ status: 'REJECTED' })
await assert.rejects(configure(rejected, credentials, { challenge }), /rejected the settings/)
assert.equal(rejected.openState, false)

const migrated = mockPort()
await configure(migrated, credentials, { challenge, keepPairing: true })
assert.deepEqual(migrated.writes, [`${prefix}HELLO ${challenge}`, `${prefix}MIGRATE ${challenge} ${JSON.stringify(credentials)}`])
assert.equal(migrated.openState, false)
const unavailable = mockPort({ status: 'PAIRING_UNAVAILABLE' })
await assert.rejects(configure(unavailable, credentials, { challenge, keepPairing: true }), /could not keep the ESPHome pairing/)
const oldFirmware = mockPort({ supportsMigration: false })
await assert.rejects(configure(oldFirmware, credentials, { challenge, keepPairing: true, timeout: 30 }), /did not answer/)
assert.deepEqual(oldFirmware.writes, migrated.writes)
assert.equal(oldFirmware.openState, false)

const retained = mockPort()
const playback = { slide_seconds: 10 }
await configure(retained, playback, { challenge, keepWifi: true, keepImmich: true, keepPairing: true })
assert.deepEqual(retained.writes, [`${prefix}HELLO ${challenge}`, `${prefix}IMPORT ${challenge} 7 ${JSON.stringify(playback)}`])
for (const [status, options, message] of [
  ['WIFI_UNAVAILABLE', { keepWifi: true }, /could not keep the ESPHome Wi-Fi connection/],
  ['IMMICH_UNAVAILABLE', { keepImmich: true }, /could not keep the ESPHome Immich connection/],
]) {
  const port = mockPort({ status })
  await assert.rejects(configure(port, playback, { challenge, ...options }), message)
  assert.equal(port.openState, false)
}

const connected = mockPort()
assert.deepEqual(await findFrame(connected, { challenge }), { address: '192.168.1.42', web: 'ready' })
assert.deepEqual(connected.writes, [`${prefix}STATUS ${challenge}`])
assert.equal(connected.openState, false)
assert.equal(await findFrame(mockPort({ network: null }), { challenge }), null)
assert.equal((await findFrame(mockPort({ network: '10.0.0.1 0' }), { challenge })).web, 'disabled')
assert.equal((await findFrame(mockPort({ network: '10.0.0.1 1' }), { challenge })).web, 'starting')
for (const network of ['192.168.1.42@other.test 2', '192.168.1.400 2', '192.168.1.42 3']) {
  const port = mockPort({ network })
  await assert.rejects(findFrame(port, { challenge }), /invalid Wi-Fi address/)
  assert.equal(port.openState, false)
}
await assert.rejects(findFrame(mockPort({ correct: false }), { challenge, timeout: 20 }), /did not answer/)

const restarting = mockPort({ booting: true, addresses: [null, '192.168.1.42 1', '192.168.1.42 2'] })
assert.deepEqual(await waitForFrame(restarting, { timeout: 500, interval: 2 }),
  { address: '192.168.1.42', web: 'ready' })
assert.equal(restarting.writes.length, 3)
assert.ok(restarting.writes.every(line => line.startsWith(`${prefix}STATUS `)))
assert.equal(restarting.openState, false)
const offline = mockPort({ network: null })
assert.equal(await waitForFrame(offline, { timeout: 35, interval: 5 }), null)
assert.ok(offline.writes.length >= 1)
assert.equal(offline.openState, false)

console.log('Native browser setup serial tests passed')
