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
  memories_window: 'Within 2 Days', memories_fallback: true,
  date_filter_enabled: true, date_filter_mode: 'Fixed Range', date_from: '2024-02-29',
  date_to: '2024-03-01', relative_amount: 1, relative_unit: 'Years',
  brightness_night: 44,
  base_tone_enabled: true, base_tone: 25, warm_tones_enabled: true, warm_tone_intensity: 60,
  screen_rotation: '180',
  schedule_enabled: true, schedule_on_hour: 6, schedule_off_hour: 22, schedule_wake_timeout: 60,
  show_clock: true, clock_format: '24 Hour', timezone: 'Europe/London (GMT+0)',
  photo_metadata_date_enabled: true, photo_metadata_location_enabled: false,
  photo_metadata_date_format: 'Relative Date', photo_metadata_date_taken_format: 'January 1, 2026',
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
    if (form.elements.screen_rotation.value !== '180')
      throw new Error('Incorrect initial mounting orientation')
    const brightness = form.elements.brightness_day
    const nightBrightness = form.elements.brightness_night
    const brightnessReadout = form.querySelector('output[for="brightness_day"]')
    if (brightness.value !== '74' || brightnessReadout.textContent !== '74%')
      throw new Error('Incorrect initial brightness')
    if (nightBrightness.value !== '44' || form.querySelector('output[for="brightness_night"]').textContent !== '44%')
      throw new Error('Incorrect initial nighttime brightness')
    if (!form.elements.warm_tones_enabled.checked || form.elements.warm_tone_intensity.value !== '60'
      || form.querySelector('output[for="warm_tone_intensity"]').textContent !== '60%')
      throw new Error('Incorrect initial night warmth')
    if (!form.elements.schedule_enabled.checked || form.elements.schedule_off_hour.value !== '22'
      || !document.querySelector('#screen-schedule').open)
      throw new Error('Incorrect initial screen schedule')
    document.querySelector('#download-backup').click()
    const backup = JSON.parse(await downloaded.text())
    if (!filename.startsWith('espframe-config-') || backup.version !== 3 || !backup.photos.favorites_enabled
      || backup.frequency.interval !== '15 seconds' || Object.keys(backup.connection).join() !== 'immich_url'
      || backup.photos.display_mode !== 'Fill' || backup.clock?.show !== true || backup.clock.timezone !== '${setup.timezone}'
      || backup.screen?.brightness_day !== 74 || backup.screen.brightness_night !== 44
      || backup.screen.rotation !== '180'
      || backup.screen.schedule_enabled !== true
      || backup.screen.schedule_on_hour !== 6 || backup.screen.schedule_off_hour !== 22
      || backup.screen.schedule_wake_timeout !== 60)
      throw new Error('Unexpected saved settings backup')
    if (!backup.metadata?.show_date || backup.metadata.show_location !== false
      || backup.metadata.date_format !== 'Relative Date' || backup.metadata.date_taken_format !== 'January 1, 2026')
      throw new Error('Unexpected caption backup settings')
    if (!backup.screen.base_tone_enabled || backup.screen.base_tone !== 25
      || !backup.screen.warm_tones_enabled || backup.screen.warm_tone_intensity !== 60)
      throw new Error('Unexpected screen tone backup settings')
    if (!backup.photos.date_filter_enabled || backup.photos.date_from !== '2024-02-29')
      throw new Error('Capture date filter missing from backup')
    const memories = document.querySelector('#memories-choice')
    const dates = document.querySelector('#capture-dates')
    if (!memories.hidden) throw new Error('On This Day options visible for favourites')
    source.value = 'memories'
    source.dispatchEvent(new Event('change'))
    if (memories.hidden || !album.disabled) throw new Error('On This Day options did not replace the album field')
    if (!dates.hidden) throw new Error('Capture date controls visible for On This Day')
    form.elements.memories_window.value = 'Within 7 Days'
    form.elements.memories_fallback.checked = false
    source.value = 'album'
    mode.value = 'Fit'
    form.elements.screen_rotation.value = '0'
    brightness.value = '45'
    brightness.dispatchEvent(new Event('input'))
    nightBrightness.value = '30'
    nightBrightness.dispatchEvent(new Event('input'))
    if (brightnessReadout.textContent !== '45%') throw new Error('Brightness readout did not update')
    form.elements.schedule_enabled.checked = false
    form.elements.schedule_off_hour.value = '0'
    form.elements.schedule_wake_timeout.value = '90'
    form.elements.timezone.value = 'America/New_York (GMT-5)'
    form.elements.clock_format.value = '12 Hour'
    source.dispatchEvent(new Event('change'))
    if (!album.required || album.disabled) throw new Error('Album selection did not enable the album field')
    if (dates.hidden || document.querySelector('#fixed-dates').hidden)
      throw new Error('Fixed capture date controls missing for album')
    form.elements.date_filter_mode.value = 'Relative Range'
    form.elements.date_filter_mode.dispatchEvent(new Event('change'))
    if (!document.querySelector('#fixed-dates').hidden || document.querySelector('#relative-dates').hidden)
      throw new Error('Relative capture date controls did not replace fixed range')
    form.elements.relative_amount.value = '3'
    form.elements.relative_unit.value = 'Months'
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
    memories_window: 'Within 7 Days', memories_fallback: false,
    date_filter_mode: 'Relative Range', relative_amount: 3, relative_unit: 'Months',
    brightness_night: 30,
    screen_rotation: '0',
    schedule_enabled: false, schedule_off_hour: 0, schedule_wake_timeout: 90,
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
