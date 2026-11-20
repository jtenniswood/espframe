import assert from 'node:assert/strict'
import { spawn, spawnSync } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { photoFilterDefaults } from '../installer/photo_source.mjs'

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
const firmwarePreferences = { auto_update: false, update_frequency: 'Weekly', wifi_auto_update: true }
const setup = {
  immich_url: 'https://photos.test/api', slide_seconds: 15, portrait_pairing: true, portrait_pairs_only: true,
  conn_timeout: '3 minutes',
  device_language: 'fr',
  transition: 'Fade to White', transition_speed: 'Normal',
  portrait_pairing_range: 'Within 2 Days',
  photo_source: 'favorites', album_id: '', display_mode: 'Fill', brightness_day: 74,
  photo_orientation: 'Portrait Only',
  memories_window: 'Within 2 Days', memories_fallback: true,
  date_filter_enabled: true, date_filter_mode: 'Fixed Range', date_from: '2024-02-29',
  date_to: '2024-03-01', relative_amount: 1, relative_unit: 'Years',
  brightness_night: 44,
  base_tone_enabled: true, base_tone: 25, warm_tones_enabled: true, warm_tone_intensity: 60,
  warm_tone_override: true,
  screen_rotation: '180',
  schedule_enabled: true, schedule_on_hour: 6, schedule_off_hour: 22, schedule_wake_timeout: 60,
  show_clock: true, clock_format: '24 Hour', timezone: 'Europe/London (GMT+0)',
  ntp_server_1: 'time.example.test', ntp_server_2: '', ntp_server_3: '2001:db8::5',
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
    if (form.elements.screen_rotation.value !== '180' || form.elements.photo_orientation.value !== 'Portrait Only')
      throw new Error('Photo and mounting orientation were not read independently')
    if (!form.elements.portrait_pairs_only.checked || form.elements.portrait_pairs_only.disabled)
      throw new Error('Existing paired-portrait setting was not loaded')
    if (form.elements.portrait_pairing_range.value !== 'Within 2 Days' || form.elements.portrait_pairing_range.disabled)
      throw new Error('Existing portrait pairing range was not loaded')
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
    if (!form.elements.warm_tone_override.checked)
      throw new Error('Saved warm tone override did not load into the form')
    if (form.elements.device_language.value !== 'fr')
      throw new Error('Saved photo date language did not load into the form')
    if (!form.elements.schedule_enabled.checked || form.elements.schedule_off_hour.value !== '22'
      || !document.querySelector('#screen-schedule').open)
      throw new Error('Incorrect initial screen schedule')
    const timeServers = ['ntp_server_1', 'ntp_server_2', 'ntp_server_3'].map(name => form.elements[name].value)
    if (timeServers.join(',') !== 'time.example.test,,2001:db8::5')
      throw new Error('Saved time servers did not load into the form')
    document.querySelector('#download-backup').click()
    const backup = JSON.parse(await downloaded.text())
    if (!filename.startsWith('espframe-config-') || backup.version !== 3 || !backup.photos.favorites_enabled
      || backup.frequency.interval !== '15 seconds' || Object.keys(backup.connection).join() !== 'immich_url'
      || backup.frequency.conn_timeout !== '3 minutes'
      || backup.photos.display_mode !== 'Fill' || backup.clock?.show !== true || backup.clock.timezone !== '${setup.timezone}'
      || backup.screen?.brightness_day !== 74 || backup.screen.brightness_night !== 44
      || backup.screen.rotation !== '180' || backup.photos.orientation !== 'Portrait Only'
      || backup.screen.schedule_enabled !== true
      || backup.screen.schedule_on_hour !== 6 || backup.screen.schedule_off_hour !== 22
      || backup.screen.schedule_wake_timeout !== 60)
      throw new Error('Unexpected saved settings backup')
    if (!backup.metadata?.show_date || backup.metadata.show_location !== false
      || backup.metadata.date_format !== 'Relative Date' || backup.metadata.date_taken_format !== 'January 1, 2026')
      throw new Error('Unexpected caption backup settings')
    if (backup.clock.ntp_servers?.join(',') !== timeServers.join(','))
      throw new Error('Time servers missing from backup')
    if (JSON.stringify(backup.firmware_updates) !== JSON.stringify(${JSON.stringify(firmwarePreferences)}))
      throw new Error('Preserved update preferences missing from backup')
    if (backup.screen.language !== 'fr')
      throw new Error('Photo date language missing from backup')
    if (!backup.screen.base_tone_enabled || backup.screen.base_tone !== 25
      || !backup.screen.warm_tones_enabled || backup.screen.warm_tone_intensity !== 60 || !backup.screen.warm_tone_override)
      throw new Error('Unexpected screen tone backup settings')
    if (!backup.photos.date_filter_enabled || backup.photos.date_from !== '2024-02-29')
      throw new Error('Capture date filter missing from backup')
    if (!backup.photos.portrait_pairs_only) throw new Error('Paired-portrait setting missing from backup')
    if (backup.photos.portrait_pairing_range !== 'Within 2 Days') throw new Error('Pairing range missing from backup')
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
    form.elements.portrait_pairing.checked = false
    form.elements.portrait_pairing.dispatchEvent(new Event('change'))
    if (!form.elements.portrait_pairs_only.disabled || !form.elements.portrait_pairs_only.checked)
      throw new Error('Pausing pairing should retain the paired-portrait preference')
    if (!form.elements.portrait_pairing_range.disabled || form.elements.portrait_pairing_range.value !== 'Within 2 Days')
      throw new Error('Pausing pairing should retain the pairing range')
    form.elements.screen_rotation.value = '0'
    form.elements.photo_orientation.value = 'Landscape Only'
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
    form.elements.conn_timeout.value = '45 seconds'
    form.elements.device_language.value = 'he'
    form.elements.warm_tone_override.checked = false
    form.elements.ntp_server_2.value = 'spare.example.test'
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
const firmwareTarget = { offset: 0x420000, elf_sha256: 'a'.repeat(64) }
const previousFirmware = { ...firmwareTarget, offset: 0x10000 }
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
const resetPath = '/espframe/api/v1/reset'
const resetFixture = html.replace('</body>', `<script>
  const form = document.querySelector('#settings')
  const resetSection = document.querySelector('#customization-reset')
  const reset = document.querySelector('#open-reset')
  const confirmation = document.querySelector('#confirm-reset')
  new MutationObserver((_, observer) => {
    if (form.hidden || resetSection.hidden) return
    observer.disconnect()
    const key = form.elements.immich_key
    if (key.required) {
      if (key.value || form.elements.immich_url.value) throw new Error('The old Immich connection survived reset')
      if (form.checkValidity()) throw new Error('The cleared form was already valid')
      form.elements.immich_url.value = 'https://reconnected.test'
      key.value = 'fresh-key'
      form.requestSubmit()
      return
    }
    reset.click()
    if (!confirmation.open) throw new Error('The reset was not confirmed')
    document.querySelector('#cancel-reset').click()
    if (confirmation.open || reset.disabled) throw new Error('Cancel did not dismiss the confirmation')
    reset.click()
    document.querySelector('#apply-reset').click()
  }).observe(form, {attributes: true})
</script></body>`)
const factoryFixture = html.replace('</body>', `<script>
  const form = document.querySelector('#settings')
  const button = document.querySelector('#open-factory-reset')
  const confirmation = document.querySelector('#confirm-factory-reset')
  new MutationObserver(() => {
    if (form.hidden) return
    if (button.closest('#factory-reset').hidden) throw new Error('Factory reset was not offered')
    button.click()
    if (!confirmation.open) throw new Error('Factory reset skipped confirmation')
    document.querySelector('#cancel-factory-reset').click()
    if (confirmation.open || button.disabled) throw new Error('Could not cancel factory reset')
    button.click()
    document.querySelector('#apply-factory-reset').click()
  }).observe(form, {attributes: true})
</script></body>`)
const filterFixture = html.replace('</body>', `<script>
  const form = document.querySelector('#settings')
  new MutationObserver((_, observer) => {
    if (form.hidden) return
    observer.disconnect()
    if (document.querySelector('#filter-controls').hidden || form.elements.photo_source.value !== 'custom')
      throw new Error('The saved custom settings were not shown')
    if (!form.elements.people_enabled.checked || form.elements.person_ids.value !== '${albumId}')
      throw new Error('The saved people selection was not preserved')
    form.elements.excluded_person_ids.value = '0406C468-DABF-4777-BB93-AC6FDAC62857'
    form.elements.minimum_rating.value = '4+'
    form.elements.rating_enabled.checked = true
    form.requestSubmit()
  }).observe(form, {attributes: true})
</script></body>`)
const pages = new Map([
  ['/', ['text/html', fixture]],
  ['/firmware', ['text/html', firmwareFixture]],
  ['/reset-test', ['text/html', resetFixture]],
  ['/factory-reset-test', ['text/html', factoryFixture]],
  ['/filter-test', ['text/html', filterFixture]],
  ['/settings.js', ['text/javascript', script]],
  ['/photo_source.mjs', ['text/javascript', sourceScript]],
  ['/backup.mjs', ['text/javascript', backupScript]],
  ['/settings.css', ['text/css', css]],
])
const updates = []
const uploads = []
const resets = []
let firmwareAvailable = false
let firmwareOutcome = 'updated'
let firmwarePolls = 0
let resetAvailable = false
let factoryAvailable = false
let customFilters = false
let resetPolls = 0
const server = createServer(async (request, response) => {
  if (request.method === 'POST' && request.url === '/api/playback') {
    let body = ''
    for await (const chunk of request) body += chunk
    updates.push(JSON.parse(body))
    response.setHeader('Content-Type', 'application/json')
    response.end(customFilters ? '{"saved":true,"restart_required":false}' : '{"saved":true}')
  } else if (request.method === 'GET' && request.url === '/api/playback') {
    response.setHeader('Content-Type', 'application/json')
    const playback = { ...setup, clock_timezones: clockTimezones, firmware_updates: firmwarePreferences }
    if (customFilters) Object.assign(playback, {
      live_photo_settings: true, photo_source: 'custom', people_enabled: true, person_ids: albumId,
      ...updates.findLast(update => update.photo_source === 'custom'),
    })
    if (resetAvailable) {
      playback.reset_path = resetPath
      playback.factory_reset = factoryAvailable
      playback.api_key_configured = resets.length === 0
      playback.reset_epoch = resets.length
      if (resets.length) playback.immich_url = ''
    }
    response.end(JSON.stringify(firmwareAvailable ? { ...playback, firmware_update: true, board_revision: 'v3' } : playback))
  } else if (request.method === 'POST' && request.url === resetPath) {
    let body = ''
    for await (const chunk of request) body += chunk
    resets.push({ body, type: request.headers['content-type'], origin: request.headers.origin })
    response.writeHead(200, { 'Content-Type': 'application/json' }).end('{"status":"accepted"}')
  } else if (request.method === 'GET' && request.url === resetPath) {
    resetPolls++
    response.writeHead(200, { 'Content-Type': 'application/json' }).end('{"status":"ready","epoch":1,"mode":"none"}')
  } else if (request.method === 'POST' && request.url === '/api/firmware') {
    const chunks = []
    for await (const chunk of request) chunks.push(chunk)
    uploads.push({ type: request.headers['content-type'], bytes: [...Buffer.concat(chunks)] })
    if (firmwareOutcome === 'updated' && uploads.length === 1) {
      response.writeHead(408).end('The firmware transfer was interrupted.')
    } else {
      response.setHeader('Content-Type', 'application/json')
      response.end(JSON.stringify(firmwareOutcome === 'legacy'
        ? { staged: true } : { staged: true, target: firmwareTarget }))
    }
  } else if (request.method === 'GET' && request.url === '/api/firmware') {
    ++firmwarePolls
    if (firmwareOutcome === 'updated' && firmwarePolls === 1) {
      response.writeHead(503).end()
      return
    }
    response.setHeader('Content-Type', 'application/json')
    if (firmwareOutcome === 'rollback') {
      const rejected = firmwarePolls === 1 ? { ...firmwareTarget, elf_sha256: 'b'.repeat(64) } : firmwareTarget
      response.end(JSON.stringify({ running: previousFirmware, boot_verified: true, rejected }))
    } else {
      response.end(JSON.stringify({
        running: firmwarePolls === 2 ? previousFirmware : firmwareTarget, boot_verified: firmwarePolls !== 3,
      }))
    }
  } else if (pages.has(request.url)) {
    const [contentType, body] = pages.get(request.url)
    response.setHeader('Content-Type', contentType)
    response.end(body)
  } else {
    response.writeHead(404).end()
  }
})

async function render(path, virtualTime = 5000) {
  const chrome = spawn(browser, [
    '--headless', '--no-sandbox', '--disable-gpu', '--dump-dom', `--virtual-time-budget=${virtualTime}`,
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
    ...photoFilterDefaults,
    ...setup, photo_source: 'album', album_id: albumId, display_mode: 'Fit', brightness_day: 45, immich_key: '',
    portrait_pairing: false,
    portrait_pair_framing: 'Follow photo framing',
    memories_window: 'Within 7 Days', memories_fallback: false,
    date_filter_mode: 'Relative Range', relative_amount: 3, relative_unit: 'Months',
    brightness_night: 30,
    screen_rotation: '0', photo_orientation: 'Landscape Only',
    schedule_enabled: false, schedule_off_hour: 0, schedule_wake_timeout: 90,
    clock_format: '12 Hour', timezone: 'America/New_York (GMT-5)',
    ntp_server_2: 'spare.example.test',
    conn_timeout: '45 seconds',
    device_language: 'he',
    warm_tone_override: false,
  }])
  console.log('Native photo source settings accept Immich album links and reject share links')

  firmwareAvailable = true
  const firmwarePage = await render('/firmware', 8000)
  assert.match(firmwarePage, /Firmware updated\. The frame passed its startup checks\./)
  assert.match(firmwarePage, /<a id="firmware-reload" href="\/settings">Reload frame settings<\/a>/)
  assert.equal(firmwarePolls, 4)
  const upload = { type: 'application/octet-stream', bytes: firmwareImage }
  assert.deepEqual(uploads, [upload, upload])
  firmwareOutcome = 'rollback'
  firmwarePolls = 0
  const rolledBackPage = await render('/firmware')
  assert.match(rolledBackPage, /The frame returned to its previous firmware during startup\./)
  assert.equal(firmwarePolls, 2)
  firmwareOutcome = 'legacy'
  firmwarePolls = 0
  const legacyFirmwarePage = await render('/firmware')
  assert.match(legacyFirmwarePage, /Update ready\. Reload frame settings after the frame restarts\./)
  assert.equal(firmwarePolls, 0)
  assert.deepEqual(uploads, [upload, upload, upload, upload])
  console.log('Native firmware settings retry an interrupted upload and report verified startup or rollback')

  firmwareAvailable = false
  resetAvailable = true
  const resetPage = await render('/reset-test')
  assert.match(resetPage, /Settings saved\. The frame is restarting\./)
  assert.match(resetPage, /Enter your API key to connect Immich\./)
  assert.deepEqual(resets, [{ body: 'mode=customization', type: 'application/x-www-form-urlencoded',
    origin: `http://127.0.0.1:${server.address().port}` }])
  assert.equal(resetPolls, 1)
  assert.equal(updates.length, 2)
  assert.equal(updates[1].immich_url, 'https://reconnected.test')
  assert.equal(updates[1].immich_key, 'fresh-key')
  assert.equal(updates[1].reset_epoch, 1)
  console.log('Native device tools confirm a customization reset and reconnect Immich after restart')

  factoryAvailable = true
  const factoryPage = await render('/factory-reset-test')
  assert.match(factoryPage, /Factory reset accepted\. After the frame restarts, connect it by USB/)
  assert.deepEqual(resets.map(({ body }) => body), ['mode=customization', 'mode=factory'])
  assert.equal(resetPolls, 1, 'Factory reset must not poll the old Wi-Fi address')
  assert.equal(updates.length, 2, 'Factory reset must not submit playback settings')
  console.log('Native device tools confirm factory reset and hand off to browser setup')

  resetAvailable = false
  customFilters = true
  const filterPage = await render('/filter-test')
  assert.match(filterPage, /Settings saved\./)
  assert.doesNotMatch(filterPage, /The frame is restarting\./)
  assert.equal(updates.length, 3)
  assert.equal(updates[2].photo_source, 'custom')
  assert.equal(updates[2].person_ids, albumId)
  assert.equal(updates[2].excluded_person_ids, '0406c468-dabf-4777-bb93-ac6fdac62857')
  assert.equal(updates[2].rating_enabled, true)
  assert.equal(updates[2].minimum_rating, '4+')
  assert.equal(updates[2].brightness_night, undefined, 'A filter save should not pin the existing night level')
  console.log('Native device tools apply compound filters live and preserve the night brightness setting')
} finally {
  server.close()
}
