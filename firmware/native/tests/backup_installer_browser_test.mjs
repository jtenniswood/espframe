import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join } from 'node:path'

const installer = process.argv[2]
assert.ok(installer, 'Pass the directory of the packaged native installer')
const [html, bundle, css, oldBackup, filteredBackup] = await Promise.all([
  readFile(join(installer, 'index.html'), 'utf8'),
  readFile(join(installer, 'setup.js'), 'utf8'),
  readFile(join(installer, 'setup.css'), 'utf8'),
  readFile(new URL('../../../tests/fixtures/backup/espframe-config-v1-full.json', import.meta.url), 'utf8'),
  readFile(new URL('../../../tests/fixtures/backup/espframe-config-v2-full.json', import.meta.url), 'utf8'),
])
const malformedBackup = JSON.parse(filteredBackup)
malformedBackup.photos.excluded_tag_ids = 'invalid-id'
const invalidOrientationBackup = JSON.parse(oldBackup)
invalidOrientationBackup.photos.orientation = '90'
const album = 'c48e2980-18ac-4d7d-876a-a636759a1aec'
const backup = {
  version: 3,
  connection: { immich_url: 'https://photos.test', api_key: 'old-api-key' },
  photos: { source: 'Memories', album_ids: album, portrait_pairing: false, portrait_pairs_only: true,
    portrait_pairing_range: 'Within 1 Day', display_mode: 'Fit', orientation: 'Landscape Only',
    memories_window: 'Within 7 Days', memories_fallback: false, date_filter_enabled: true,
    date_filter_mode: 'Fixed Range', date_from: '2024-02-29', date_to: '', relative_amount: 2, relative_unit: 'Months' },
  clock: { show: false, format: '12 Hour', timezone: 'America/New_York (GMT-5)' },
  metadata: { show_date: true, show_location: false, date_format: 'Relative Date', date_taken_format: 'January 1, 2026' },
  screen: {
    brightness_day: 75, brightness_night: 35, schedule_enabled: true, rotation: '180',
    base_tone_enabled: true, base_tone: 25, warm_tones_enabled: true, warm_tone_intensity: 60,
    schedule_on_hour: 21, schedule_off_hour: 6, schedule_wake_timeout: 30,
  },
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
          const match = /^ESPFRAME_SETUP 1 (HELLO|SET|STATUS) ([^ ]+)(?: (.*))?$/.exec(text)
          if (!match) throw new Error('Invalid USB message')
          if (match[1] === 'SET') received = JSON.parse(match[3])
          if (match[1] === 'STATUS') {
            if (match[3]) throw new Error('Wi-Fi lookup sent unexpected data')
            const response = attempts === 2 ? 'OFFLINE ' + match[2] : 'CONNECTED ' + match[2] + ' 192.168.50.10 2'
            output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + response + '\\n'))
          } else {
            output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + (match[1] === 'HELLO' ? 'READY' : 'SAVED') + ' ' + match[2] + '\\n'))
          }
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
    await loadBackup(${JSON.stringify(oldBackup)}, 'Backup loaded.')
    if (form.elements.photo_source.value !== 'album' ||
        form.elements.album_id.value !== '11111111-1111-4111-8111-111111111111')
      throw new Error('The older album source was not preserved')
    if (form.elements.display_mode.value !== 'Fill') throw new Error('Existing crop mode was not imported')
    if (form.elements.brightness_day.value !== '100') throw new Error('Existing brightness was not imported')
    if (form.elements.brightness_night.value !== '75') throw new Error('Existing nighttime brightness was not imported')
    if (form.elements.screen_rotation.value !== '0') throw new Error('Existing mounting orientation was not imported')
    if (!form.elements.portrait_pairs_only.checked || form.elements.portrait_pairs_only.disabled)
      throw new Error('Active paired-portrait preference was not imported')
    if (form.elements.portrait_pairing_range.value !== 'Within 2 Days' || form.elements.portrait_pairing_range.disabled)
      throw new Error('Existing pairing range was not imported')
    await loadBackup(${JSON.stringify(JSON.stringify(malformedBackup))}, 'Choose a photo source before saving.')
    if (form.elements.photo_source.value || form.checkValidity()) throw new Error('Unrepresentable filters allowed a save')
    form.requestSubmit()
    if (attempts) throw new Error('USB opened with unresolved photo filters')
    await loadBackup(${JSON.stringify(JSON.stringify(invalidOrientationBackup))}, 'Choose which photo orientations')
    if (form.elements.photo_source.value !== 'album' || form.elements.photo_orientation.value || form.checkValidity())
      throw new Error('Invalid photo orientation was not isolated from the source')
    await loadBackup(${JSON.stringify(JSON.stringify(backup))}, 'Backup loaded.')
    if (form.elements.immich_url.value !== 'https://photos.test') throw new Error('Immich URL was not prefilled')
    if (form.elements.immich_key.value !== 'entered-api-key') throw new Error('A backup replaced the entered key')
    if (document.querySelector('#memories-choice').hidden || form.elements.memories_window.value !== 'Within 7 Days'
      || form.elements.memories_fallback.checked) throw new Error('On This Day settings were not prefilled')
    if (!document.querySelector('#capture-dates').hidden || form.elements.date_from.value !== '2024-02-29')
      throw new Error('Inactive capture date options were not preserved')
    if (form.querySelector('output[for="brightness_day"]').textContent !== '75%'
      || form.querySelector('output[for="brightness_night"]').textContent !== '35%')
      throw new Error('Imported brightness was not shown')
    if (!document.querySelector('#screen-schedule').open || !form.elements.schedule_enabled.checked)
      throw new Error('Imported screen schedule was not shown')
    if (form.elements.screen_rotation.value !== '180' || form.elements.photo_orientation.value !== 'Landscape Only')
      throw new Error('Photo and mounting orientation were not imported independently')
    if (!form.elements.portrait_pairs_only.checked || !form.elements.portrait_pairs_only.disabled)
      throw new Error('Inactive paired-portrait preference was not retained')
    if (form.elements.portrait_pairing_range.value !== 'Within 1 Day' || !form.elements.portrait_pairing_range.disabled)
      throw new Error('Inactive pairing range was not retained')
    const saved = waitFor(document.querySelector('#status'), 'Settings saved.')
    form.requestSubmit()
    await saved
    if (attempts !== 1 || received.immich_key !== 'entered-api-key' || received.ssid !== 'Test Wi-Fi'
      || received.photo_source !== 'memories' || received.album_id !== '${album}' || received.slide_seconds !== 86400
      || received.memories_window !== 'Within 7 Days' || received.memories_fallback !== false
      || received.portrait_pairing !== false || received.portrait_pairs_only !== true
      || received.portrait_pairing_range !== 'Within 1 Day' || received.display_mode !== 'Fit' || Object.hasOwn(received, 'backup'))
      throw new Error('Unexpected migrated USB settings')
    if (received.show_clock !== false || received.clock_format !== '12 Hour' || received.timezone !== 'America/New_York (GMT-5)')
      throw new Error('The clock settings did not reach USB setup')
    if (!received.date_filter_enabled || received.date_filter_mode !== 'Fixed Range' || received.date_from !== '2024-02-29'
      || received.date_to !== '' || received.relative_amount !== 2 || received.relative_unit !== 'Months')
      throw new Error('The capture date filter did not reach USB setup')
    if (!received.photo_metadata_date_enabled || received.photo_metadata_location_enabled !== false
      || received.photo_metadata_date_format !== 'Relative Date' || received.photo_metadata_date_taken_format !== 'January 1, 2026')
      throw new Error('The caption settings did not reach USB setup')
    if (received.brightness_day !== 75 || received.brightness_night !== 35)
      throw new Error('Brightness did not reach USB setup')
    if (!received.base_tone_enabled || received.base_tone !== 25 || !received.warm_tones_enabled || received.warm_tone_intensity !== 60)
      throw new Error('Screen tone did not reach USB setup')
    if (received.screen_rotation !== '180' || received.photo_orientation !== 'Landscape Only')
      throw new Error('Photo and mounting orientation did not reach USB setup independently')
    if (!received.schedule_enabled || received.schedule_on_hour !== 21 || received.schedule_off_hour !== 6
      || received.schedule_wake_timeout !== 30) throw new Error('Screen schedule did not reach USB setup')
    const find = document.querySelector('#find-frame')
    const networkStatus = document.querySelector('#network-status')
    const waiting = waitFor(networkStatus, 'waiting for Wi-Fi')
    find.click()
    await waiting
    const connected = waitFor(networkStatus, 'Connected at 192.168.50.10')
    find.click()
    await connected
    const link = networkStatus.querySelector('a')
    if (attempts !== 3 || link?.href !== 'http://192.168.50.10/' || link.textContent !== 'Open frame settings')
      throw new Error('Wi-Fi lookup did not link to the frame’s settings')
    const reuse = document.querySelector('#reuse-wifi')
    reuse.checked = true
    reuse.dispatchEvent(new Event('change'))
    form.elements.immich_url.value = 'https://photos.test'
    form.elements.immich_key.value = 'after-improv'
    const reused = waitFor(document.querySelector('#status'), 'Settings saved.')
    form.requestSubmit()
    await reused
    if (attempts !== 4 || received.immich_key !== 'after-improv'
      || Object.hasOwn(received, 'ssid') || Object.hasOwn(received, 'password'))
      throw new Error('Immich setup asked for Wi-Fi credentials a second time')
    await loadBackup(${JSON.stringify(filteredBackup)}, 'Backup loaded.')
    if (form.elements.photo_source.value !== 'custom' || document.querySelector('#filter-controls').hidden
      || !form.elements.people_enabled.checked || form.elements.minimum_rating.value !== '4+')
      throw new Error('Compound photo filters did not appear in the source editor')
    form.elements.immich_key.value = 'filtered-key'
    reuse.checked = true
    reuse.dispatchEvent(new Event('change'))
    const filtered = waitFor(document.querySelector('#status'), 'Settings saved.')
    form.requestSubmit()
    await filtered
    if (attempts !== 5 || received.photo_source !== 'custom' || !received.albums_enabled || !received.people_enabled
      || !received.tags_enabled || received.album_order !== 'Album list order' || received.filter_country !== 'New Zealand'
      || received.excluded_tag_ids !== '66666666-6666-4666-8666-666666666666' || received.minimum_rating !== '4+'
      || received.favorite_mode !== 'Exclude favorites' || received.immich_key !== 'filtered-key')
      throw new Error('Compound photo filters did not reach USB setup')
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
