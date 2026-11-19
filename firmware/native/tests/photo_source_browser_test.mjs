import assert from 'node:assert/strict'
import { spawn, spawnSync } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const browser = process.env.CHROME_BIN || 'google-chrome'
const read = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, script, css, sourceScript, backupScript] = await Promise.all([
  read('../main/web/settings.html'),
  read('../main/web/settings.js'),
  read('../installer/setup.css'),
  read('../installer/photo_source.mjs'),
  read('../installer/backup.mjs'),
])
const albumId = 'c48e2980-18ac-4d7d-876a-a636759a1aec'
const setup = {
  immich_url: 'https://photos.test/api', slide_seconds: 15, portrait_pairing: true,
  photo_source: 'favorites', album_id: '', display_mode: 'Fill', brightness_day: 74,
  show_clock: true, clock_format: '24 Hour', timezone: 'Europe/London (GMT+0)',
}
const clockTimezones = {
  'Europe/London (GMT+0)': 'Europe/London (GMT+0; daylight GMT+1)',
  'America/New_York (GMT-5)': 'America/New_York (GMT-5; daylight GMT-4)',
}
const fixture = html.replace('</body>', `<script>
  const form = document.querySelector('#settings')
  let downloaded
  let filename
  URL.createObjectURL = blob => { downloaded = blob; return 'blob:mock-backup' }
  HTMLAnchorElement.prototype.click = function () { filename = this.download }
  new MutationObserver(async (_, observer) => {
    if (form.hidden) return
    observer.disconnect()
    const source = form.elements.photo_source
    const album = form.elements.album_id
    if (source.value !== 'favorites' || !album.disabled) throw new Error('Incorrect initial source')
    const mode = form.elements.display_mode
    if (mode.value !== 'Fill' || mode.selectedOptions[0].text !== 'Crop to fit')
      throw new Error('Incorrect initial framing')
    const brightness = form.elements.brightness_day
    const brightnessReadout = form.querySelector('output[for="brightness_day"]')
    if (brightness.value !== '74' || brightnessReadout.textContent !== '74%')
      throw new Error('Incorrect initial brightness')
    document.querySelector('#download-backup').click()
    const backup = JSON.parse(await downloaded.text())
    if (!filename.startsWith('espframe-config-') || backup.version !== 3 || !backup.photos.favorites_enabled
      || backup.frequency.interval !== '15 seconds' || Object.keys(backup.connection).join() !== 'immich_url'
      || backup.photos.display_mode !== 'Fill' || backup.clock?.show !== true || backup.clock.timezone !== '${setup.timezone}'
      || backup.screen?.brightness_day !== 74)
      throw new Error('Unexpected saved settings backup')
    source.value = 'album'
    mode.value = 'Fit'
    brightness.value = '45'
    brightness.dispatchEvent(new Event('input'))
    if (brightnessReadout.textContent !== '45%') throw new Error('Brightness readout did not update')
    form.elements.timezone.value = 'America/New_York (GMT-5)'
    form.elements.clock_format.value = '12 Hour'
    source.dispatchEvent(new Event('change'))
    if (!album.required || album.disabled) throw new Error('Album selection did not enable the album field')
    album.value = 'https://photos.test/share/${albumId}'
    form.requestSubmit()
    if (album.validity.valid) throw new Error('A share link was accepted as an album')
    album.value = 'https://photos.test/immich/albums/${albumId.toUpperCase()}?from=library'
    album.dispatchEvent(new Event('input'))
    form.requestSubmit()
  }).observe(form, {attributes: true})
</script></body>`)
const firmwareImage = [0xe9, 0x01, 0x00, 0x00]
const firmwareFixture = html.replace('</body>', `<script>
  const section = document.querySelector('#firmware')
  const form = document.querySelector('#firmware-update')
  const button = form.querySelector('button')
  const status = document.querySelector('#firmware-status')
  new MutationObserver((_, observer) => {
    if (section.hidden) return
    observer.disconnect()
    if (!document.querySelector('#firmware-help').textContent.includes('v3-ota.bin'))
      throw new Error('Missing firmware filename for this board')
    const transfer = new DataTransfer()
    transfer.items.add(new File([new Uint8Array(${JSON.stringify(firmwareImage)})], 'v3-ota.bin'))
    form.elements.image.files = transfer.files
    const retry = new MutationObserver((_, errors) => {
      if (status.textContent !== 'The firmware transfer was interrupted.') return
      errors.disconnect()
      if (button.disabled) throw new Error('Firmware update cannot be retried')
      form.requestSubmit()
    })
    retry.observe(status, {childList: true})
    form.requestSubmit()
  }).observe(section, {attributes: true})
</script></body>`)
const pages = new Map([
  ['/', ['text/html', fixture]],
  ['/firmware', ['text/html', firmwareFixture]],
  ['/settings.js', ['text/javascript', script]],
  ['/photo_source.mjs', ['text/javascript', sourceScript]],
  ['/backup.mjs', ['text/javascript', backupScript]],
  ['/settings.css', ['text/css', css]],
])
const updates = []
const uploads = []
let firmwareAvailable = false
const server = createServer(async (request, response) => {
  if (request.method === 'POST' && request.url === '/api/playback') {
    let body = ''
    for await (const chunk of request) body += chunk
    updates.push(JSON.parse(body))
    response.setHeader('Content-Type', 'application/json')
    response.end('{"saved":true}')
  } else if (request.method === 'GET' && request.url === '/api/playback') {
    response.setHeader('Content-Type', 'application/json')
    const playback = { ...setup, clock_timezones: clockTimezones }
    response.end(JSON.stringify(firmwareAvailable ? { ...playback, firmware_update: true, board_revision: 'v3' } : playback))
  } else if (request.method === 'POST' && request.url === '/api/firmware') {
    const chunks = []
    for await (const chunk of request) chunks.push(chunk)
    uploads.push({ type: request.headers['content-type'], bytes: [...Buffer.concat(chunks)] })
    if (uploads.length === 1) {
      response.writeHead(408).end('The firmware transfer was interrupted.')
    } else {
      response.setHeader('Content-Type', 'application/json')
      response.end('{"staged":true}')
    }
  } else if (pages.has(request.url)) {
    const [contentType, body] = pages.get(request.url)
    response.setHeader('Content-Type', contentType)
    response.end(body)
  } else {
    response.writeHead(404).end()
  }
})

async function render(path) {
  const chrome = spawn(browser, [
    '--headless', '--no-sandbox', '--disable-gpu', '--dump-dom', '--virtual-time-budget=5000',
    `http://127.0.0.1:${server.address().port}${path}`,
  ], { signal: AbortSignal.timeout(15_000) })
  let page = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  return page
}

assert.equal(spawnSync(browser, ['--version']).status, 0, 'Set CHROME_BIN to a Chromium executable')
await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const page = await render('/')
  assert.match(page, /Settings saved\. The frame is restarting\./)
  assert.match(page, /<section id="firmware"[^>]* hidden="">/)
  assert.deepEqual(updates, [{
    ...setup, photo_source: 'album', album_id: albumId, display_mode: 'Fit', brightness_day: 45, immich_key: '',
    clock_format: '12 Hour', timezone: 'America/New_York (GMT-5)',
  }])
  console.log('Native photo source settings accept Immich album links and reject share links')

  firmwareAvailable = true
  const firmwarePage = await render('/firmware')
  assert.match(firmwarePage, /Update ready\. The frame is restarting\./)
  const upload = { type: 'application/octet-stream', bytes: firmwareImage }
  assert.deepEqual(uploads, [upload, upload])
  console.log('Native firmware settings retry an interrupted upload and report successful staging')
} finally {
  server.close()
}
