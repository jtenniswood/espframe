const prefix = 'ESPFRAME_SETUP 1 '
const encoder = new TextEncoder()

// Read across USB packet boundaries and ignore firmware console lines.
async function readReply(reader, challenge, accepted) {
  const decoder = new TextDecoder()
  let line = ''
  let overflow = false
  while (true) {
    const { value, done } = await reader.read()
    if (done) throw new Error('The frame disconnected during setup.')
    for (const character of decoder.decode(value, { stream: true })) {
      if (character === '\n') {
        const message = !overflow && line.trimEnd()
        for (const status of accepted) {
          const expected = `${prefix}${status} ${challenge}`
          if (message === expected) return { status, detail: '' }
          if (status === 'CONNECTED' && message && message.startsWith(expected + ' ')) {
            return { status, detail: message.slice(expected.length + 1) }
          }
        }
        line = ''
        overflow = false
      } else if (line.length < 160) {
        line += character
      } else {
        overflow = true
      }
    }
  }
}

async function waitForReply(reader, challenge, accepted, timeout) {
  let timer
  try {
    return await Promise.race([
      readReply(reader, challenge, accepted),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error('The frame did not answer. Close the installer dialog, check the USB connection, and try again.')), timeout)
      }),
    ])
  } finally {
    clearTimeout(timer)
  }
}

async function withPort(port, action) {
  await port.open({ baudRate: 115200 })
  let reader
  let writer
  try {
    reader = port.readable.getReader()
    writer = port.writable.getWriter()
    return await action(reader, writer)
  } finally {
    writer?.releaseLock()
    if (reader) {
      await reader.cancel().catch(() => {})
      reader.releaseLock()
    }
    await port.close().catch(() => {})
  }
}

export function configure(port, settings, { challenge = crypto.randomUUID(), timeout = 5000,
  keepPairing = false, keepWifi = false, keepImmich = false } = {}) {
  return withPort(port, async (reader, writer) => {
    await writer.write(encoder.encode(`\n${prefix}HELLO ${challenge}\n`))
    await waitForReply(reader, challenge, ['READY'], timeout)
    const connections = (keepWifi ? 1 : 0) | (keepImmich ? 2 : 0)
    const command = connections ? 'IMPORT' : keepPairing ? 'MIGRATE' : 'SET'
    const flags = connections ? ` ${connections | (keepPairing ? 4 : 0)}` : ''
    await writer.write(encoder.encode(`${prefix}${command} ${challenge}${flags} ${JSON.stringify(settings)}\n`))
    const { status } = await waitForReply(reader, challenge,
      ['SAVED', 'REJECTED', 'PAIRING_UNAVAILABLE', 'WIFI_UNAVAILABLE', 'IMMICH_UNAVAILABLE'], timeout)
    if (status === 'PAIRING_UNAVAILABLE')
      throw new Error('The frame could not keep the ESPHome pairing. Clear the checkbox to continue setup, or enter an existing key.')
    if (status === 'WIFI_UNAVAILABLE')
      throw new Error('The frame could not keep the ESPHome Wi-Fi connection. Clear the checkbox and enter Wi-Fi details, or use Wi-Fi already saved on the frame.')
    if (status === 'IMMICH_UNAVAILABLE')
      throw new Error('The frame could not keep the ESPHome Immich connection. Clear the checkbox and enter the Immich URL and API key.')
    if (status !== 'SAVED') throw new Error('The frame rejected the settings. Check each field and try again.')
  })
}

export function findFrame(port, { challenge = crypto.randomUUID(), timeout = 5000 } = {}) {
  return withPort(port, async (reader, writer) => {
    await writer.write(encoder.encode(`\n${prefix}STATUS ${challenge}\n`))
    const { status, detail } = await waitForReply(reader, challenge, ['CONNECTED', 'OFFLINE'], timeout)
    if (status === 'OFFLINE') return null
    const fields = detail.split(' ')
    const [address, web] = fields
    const octets = address.split('.')
    if (fields.length !== 2 || !['0', '1', '2'].includes(web) || octets.length !== 4 ||
        !octets.every(octet => /^(0|[1-9]\d{0,2})$/.test(octet) && Number(octet) <= 255)) {
      throw new Error('The frame returned an invalid Wi-Fi address.')
    }
    return { address, web: ['disabled', 'starting', 'ready'][Number(web)] }
  })
}

export async function waitForFrame(port, { timeout = 30_000, interval = 750 } = {}) {
  const deadline = performance.now() + timeout
  let frame = null
  while (deadline - performance.now() > interval) {
    // SET acknowledges before reboot. Give the frame time to restart before
    // asking for an address, and retry while USB or Wi-Fi is coming back.
    await new Promise(resolve => setTimeout(resolve, interval))
    const remaining = deadline - performance.now()
    if (remaining <= 0) break
    frame = await findFrame(port, { timeout: Math.min(1500, remaining) }).catch(() => null)
    if (frame && frame.web !== 'starting') return frame
  }
  return frame
}
