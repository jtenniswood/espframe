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
          if (message === `${prefix}${status} ${challenge}`) return status
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

export async function configure(port, settings, { challenge = crypto.randomUUID(), timeout = 5000 } = {}) {
  await port.open({ baudRate: 115200 })
  let reader
  let writer
  try {
    reader = port.readable.getReader()
    writer = port.writable.getWriter()
    await writer.write(encoder.encode(`\n${prefix}HELLO ${challenge}\n`))
    await waitForReply(reader, challenge, ['READY'], timeout)
    await writer.write(encoder.encode(`${prefix}SET ${challenge} ${JSON.stringify(settings)}\n`))
    const status = await waitForReply(reader, challenge, ['SAVED', 'REJECTED'], timeout)
    if (status !== 'SAVED') throw new Error('The frame rejected the settings. Check each field and try again.')
  } finally {
    writer?.releaseLock()
    if (reader) {
      await reader.cancel().catch(() => {})
      reader.releaseLock()
    }
    await port.close().catch(() => {})
  }
}
