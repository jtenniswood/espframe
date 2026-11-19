const form = document.querySelector('#recovery')
const status = document.querySelector('#status')
const button = form.querySelector('button')
const encoder = new TextEncoder()
const delay = () => new Promise(resolve => setTimeout(resolve, 750))

async function readStatus() {
  const response = await fetch('/api/recovery', { signal: AbortSignal.timeout(2000) })
  if (!response.ok) throw new Error('Wi-Fi setup is unavailable.')
  return response.json()
}

async function waitForConnection(conflictMessage) {
  status.textContent = 'Connecting the frame. Stay on its Wi-Fi setup network.'
  const deadline = Date.now() + 32_000
  do {
    // The hotspot can briefly move channels while the frame joins home Wi-Fi.
    try {
      const result = await readStatus()
      if (result.status === 'ready' && conflictMessage) return { status: 'conflict', message: conflictMessage }
      if (result.status !== 'joining') return result
    } catch {
      // The next poll can continue after the phone reconnects to the hotspot.
    }
    await delay()
  } while (Date.now() < deadline)
  return { status: 'unreachable' }
}

function show(result) {
  form.hidden = result.status === 'connected'
  switch (result.status) {
    case 'connected':
      status.textContent = `Connected. Rejoin your usual Wi-Fi. The frame’s address is ${result.ipv4}.`
      if (result.web_settings === false) {
        status.textContent += ' To set up Immich or enable web settings, connect by USB and open “Connect Wi-Fi and Immich” in the native browser installer.'
      }
      break
    case 'saved':
      status.textContent = 'Wi-Fi settings saved. The frame is reconnecting.'
      break
    case 'failed':
      status.textContent = 'Could not join that network. Check the network name and password, then try again.'
      break
    case 'unreachable':
      status.textContent = 'Could not check the frame’s connection. Rejoin its setup network and try again if it is still available.'
      break
    case 'conflict':
      status.textContent = result.message
      break
    default:
      status.textContent = ''
  }
}

form.addEventListener('submit', async event => {
  event.preventDefault()
  if (button.disabled) return
  const ssid = encoder.encode(form.elements.ssid.value)
  const password = encoder.encode(form.elements.password.value)
  if (ssid.length < 1 || ssid.length > 32 || password.length > 64 || ssid.includes(0) || password.includes(0)) {
    status.textContent = 'Enter a Wi-Fi network name up to 32 bytes and a password up to 64 bytes.'
    return
  }
  button.disabled = true
  try {
    const response = await fetch('/api/recovery', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: new Uint8Array([ssid.length, ...ssid, password.length, ...password]),
      signal: AbortSignal.timeout(5000),
    }).catch(() => null)
    if (response && !response.ok && response.status !== 409) {
      status.textContent = await response.text()
      return
    }
    const conflict = response?.status === 409 ? await response.text() : null
    const result = await waitForConnection(conflict)
    if (result.status !== 'conflict') form.elements.password.value = ''
    show(result)
  } finally {
    button.disabled = false
  }
})

try {
  const current = await readStatus()
  show(current.status === 'joining' ? await waitForConnection() : current)
} catch {
  show({ status: 'unreachable' })
}
