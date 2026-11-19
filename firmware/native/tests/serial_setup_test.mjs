import assert from 'node:assert/strict'
import { configure, findFrame } from '../installer/serial.mjs'

const prefix = 'ESPFRAME_SETUP 1 '
const challenge = 'ace6c621-8c99-4d4a-b151-7cec3ffee421'
const encoder = new TextEncoder()
const credentials = {
  ssid: 'Photo Lab',
  password: 'p"ass\\word',
  immich_url: 'https://photos.example.test/api',
  immich_key: 'test-key',
}

function mockPort({ correct = true, status = 'SAVED', network = '192.168.1.42 2' } = {}) {
  let receive
  let portOpen = false
  const writes = []
  const readable = new ReadableStream({ start(controller) { receive = controller } })
  const writable = new WritableStream({
    write(value) {
      const line = new TextDecoder().decode(value).trim()
      writes.push(line)
      if (line.startsWith(`${prefix}HELLO`)) {
        receive.enqueue(encoder.encode(`\r\nI console line\r\n${prefix}READY ${correct ? challenge.slice(0, 16) : 'another-id'}`))
        receive.enqueue(encoder.encode(`${correct ? challenge.slice(16) : ''}\r\n`))
      } else if (line.startsWith(`${prefix}SET`)) {
        receive.enqueue(encoder.encode(`\r\n${prefix}${status} ${challenge}\r\n`))
      } else if (line.startsWith(`${prefix}STATUS`)) {
        const nonce = correct ? challenge : 'another-id'
        const reply = network === null ? `OFFLINE ${nonce}` : `CONNECTED ${nonce} ${network}`
        receive.enqueue(encoder.encode(`\r\nI normal console output\r\n${prefix}${reply}\r\n`))
      }
    },
  })
  return {
    readable,
    writable,
    writes,
    get openState() { return portOpen },
    async open(options) {
      assert.deepEqual(options, { baudRate: 115200 })
      portOpen = true
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
console.log('Native browser setup serial tests passed')
