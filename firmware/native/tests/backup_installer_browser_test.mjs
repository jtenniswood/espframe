import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join } from 'node:path'

const installer = process.argv[2]
assert.ok(installer, 'Pass the directory of the packaged native installer')
const [html, bundle, css, oldBackup] = await Promise.all([
  readFile(join(installer, 'index.html'), 'utf8'),
  readFile(join(installer, 'setup.js'), 'utf8'),
  readFile(join(installer, 'setup.css'), 'utf8'),
  readFile(new URL('../../../tests/fixtures/backup/espframe-config-v1-full.json', import.meta.url), 'utf8'),
])
const album = 'c48e2980-18ac-4d7d-876a-a636759a1aec'
const backup = {
  version: 3,
  connection: { immich_url: 'https://photos.test', api_key: 'old-api-key' },
  photos: { albums_enabled: true, album_ids: album, portrait_pairing: false, display_mode: 'Fit' },
  clock: { show: false, format: '12 Hour', timezone: 'America/New_York (GMT-5)' },
  frequency: { interval: '24 hours' },
}
const verify = `
  const encoder = new TextEncoder()
  let received
  let attempts = 0
  Object.defineProperty(navigator, 'serial', { value: {
    async requestPort() {
      attempts++
      let output
      return {
        async open() {},
        async close() {},
        readable: new ReadableStream({start(controller) { output = controller }}),
        writable: new WritableStream({write(bytes) {
          const text = new TextDecoder().decode(bytes).trim()
          const match = /^ESPFRAME_SETUP 1 (HELLO|SET) ([^ ]+)(?: (.*))?$/.exec(text)
          if (!match) throw new Error('Invalid USB message')
          if (match[1] === 'SET') received = JSON.parse(match[3])
          output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + (match[1] === 'HELLO' ? 'READY' : 'SAVED') + ' ' + match[2] + '\\n'))
        }}),
      }
    },
  }})
  function waitFor(element, text) {
    return new Promise(resolve => {
      new MutationObserver((_, observer) => {
        if (element.textContent.includes(text)) {
          observer.disconnect()
          resolve()
        }
      }).observe(element, { childList: true, subtree: true })
    })
  }
  window.addEventListener('DOMContentLoaded', async () => {
    const form = document.querySelector('#settings')
    const file = document.querySelector('#backup')
    const status = document.querySelector('#backup-status')
    function loadBackup(json, message) {
      const done = waitFor(status, message)
      const data = new DataTransfer()
      data.items.add(new File([json], 'espframe-config.json', { type: 'application/json' }))
      file.files = data.files
      file.dispatchEvent(new Event('change'))
      return done
    }
    form.elements.ssid.value = 'Test Wi-Fi'
    form.elements.immich_key.value = 'entered-api-key'
    await loadBackup(${JSON.stringify(oldBackup)}, 'Choose a photo source before saving.')
    if (form.elements.photo_source.value || form.checkValidity()) throw new Error('Unrepresentable filters allowed a save')
    if (form.elements.display_mode.value !== 'Fill') throw new Error('Existing crop mode was not imported')
    form.requestSubmit()
    if (attempts) throw new Error('USB opened with unresolved photo filters')
    await loadBackup(${JSON.stringify(JSON.stringify(backup))}, 'Backup loaded.')
    if (form.elements.immich_url.value !== 'https://photos.test') throw new Error('Immich URL was not prefilled')
    if (form.elements.immich_key.value !== 'entered-api-key') throw new Error('A backup replaced the entered key')
    const saved = waitFor(document.querySelector('#status'), 'Settings saved.')
    form.requestSubmit()
    await saved
    if (attempts !== 1 || received.immich_key !== 'entered-api-key' || received.ssid !== 'Test Wi-Fi'
      || received.photo_source !== 'album' || received.album_id !== '${album}' || received.slide_seconds !== 86400
      || received.portrait_pairing !== false || received.display_mode !== 'Fit' || Object.hasOwn(received, 'backup'))
      throw new Error('Unexpected migrated USB settings')
    if (received.show_clock !== false || received.clock_format !== '12 Hour' || received.timezone !== 'America/New_York (GMT-5)')
      throw new Error('The clock settings did not reach USB setup')
    document.body.dataset.backupTest = 'passed'
  })
`
const pages = new Map([
  ['/', ['text/html', html.replace('</head>', '<script src="verify.js"></script></head>')]],
  ['/setup.js', ['text/javascript', bundle]],
  ['/setup.css', ['text/css', css]],
  ['/verify.js', ['text/javascript', verify]],
])
const server = createServer((request, response) => {
  const page = pages.get(request.url)
  if (page) response.writeHead(200, { 'Content-Type': page[0] }).end(page[1])
  else response.writeHead(404).end()
})
await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--dump-dom', '--virtual-time-budget=5000',
    `http://127.0.0.1:${server.address().port}/`,
  ], { signal: AbortSignal.timeout(15_000) })
  let html = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { html += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  assert.match(html, /data-backup-test="passed"/, errors)
  console.log('Packaged native installer transfers supported backup fields over mocked USB')
} finally {
  server.close()
}
