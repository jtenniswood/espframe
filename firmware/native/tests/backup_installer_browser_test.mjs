import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
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
const personBackup = { version: 1, photos: { source: 'Person', person_ids: album } }
const backup = {
  version: 3,
  identity: { name: '  Salon été & famille  ', hostname: 'old-frame-9876' },
  connection: { immich_url: 'https://photos.test', api_key: 'old-api-key', home_assistant_key: 'backup-key' },
  firmware_updates: JSON.parse(oldBackup).firmware_updates,
  photos: { source: 'Memories', album_ids: album, portrait_pairing: false, portrait_pairs_only: true,
    portrait_pairing_range: 'Any Time', display_mode: 'Fit', orientation: 'Landscape Only',
    transition: 'Slide Down', transition_speed: 'Slow',
    memories_window: 'Within 7 Days', memories_fallback: false, date_filter_enabled: true,
    date_filter_mode: 'Fixed Range', date_from: '2024-02-29', date_to: '', relative_amount: 2, relative_unit: 'Months' },
  clock: { show: false, format: '12 Hour', timezone: 'America/New_York (GMT-5)',
    ntp_servers: ['time.example.test', '', '2001:db8::5'] },
  metadata: { show_date: true, show_location: false, date_format: 'Relative Date', date_taken_format: 'January 1, 2026' },
  screen: {
    brightness_day: 75, brightness_night: 35, schedule_enabled: true, rotation: '180',
    base_tone_enabled: true, base_tone: 25, warm_tones_enabled: true, warm_tone_intensity: 60,
    warm_tone_override: true,
    language: 'he',
    schedule_on_hour: 21, schedule_off_hour: 6, schedule_wake_timeout: 30,
  },
  frequency: { interval: '24 hours', conn_timeout: '3 minutes' },
}
const verify = `
  const encoder = new TextEncoder()
  let received
  let lastWrite
  let lastFlags
  let attempts = 0
  let writes = 0
  let addressChecks = 0
  Object.defineProperty(navigator, 'serial', { value: {
    async requestPort() {
      const selection = ++attempts
      let output
      return {
        async open() {
          this.readable = new ReadableStream({start(controller) { output = controller }})
        },
        async close() {},
        writable: new WritableStream({write(bytes) {
          const text = new TextDecoder().decode(bytes).trim()
          const match = /^ESPFRAME_SETUP 1 (HELLO|SET|MIGRATE|IMPORT|STATUS) ([^ ]+)(?: ([1-7])(?= ))?(?: (.*))?$/.exec(text)
          if (!match) throw new Error('Invalid USB message')
          if (match[1] === 'SET' || match[1] === 'MIGRATE' || match[1] === 'IMPORT') {
            writes++
            lastWrite = match[1]
            lastFlags = match[3]
            received = JSON.parse(match[4])
          }
          if (match[1] === 'STATUS') {
            if (match[3] || match[4]) throw new Error('Wi-Fi lookup sent unexpected data')
            addressChecks++
            const offline = selection === 2 || (selection === 1 && addressChecks === 1)
            const state = selection === 1 && addressChecks === 2 ? 1 : 2
            const response = offline ? 'OFFLINE ' + match[2] : 'CONNECTED ' + match[2] + ' 192.168.50.10 ' + state
            output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + response + '\\n'))
          } else {
            const missing = match[1] === 'IMPORT' && { 8: 'WIFI_UNAVAILABLE', 9: 'IMMICH_UNAVAILABLE' }[selection]
            const reply = match[1] === 'HELLO' ? 'READY' : match[1] === 'MIGRATE' && selection === 6
              ? 'PAIRING_UNAVAILABLE' : missing || 'SAVED'
            output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + reply + ' ' + match[2] + '\\n'))
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
  async function runSetup() {
    const form = document.querySelector('#settings')
    if (form.elements.display_mode.value !== '' || form.elements.portrait_pair_framing.value !== '')
      throw new Error('Opening the installer selected a replacement for saved framing')
    if (!form.elements.portrait_pairing.checked || !form.elements.portrait_pairs_only.checked
      || form.elements.portrait_pairing_range.value !== 'Within 2 Days'
      || form.elements.brightness_day.value !== '100' || form.elements.brightness_night.value !== '70')
      throw new Error('Fresh setup did not show the requested photo and brightness defaults')
    if (form.elements.show_clock.checked || form.elements.photo_metadata_date_enabled.checked
      || form.elements.photo_metadata_location_enabled.checked || form.elements.clock_format.value !== '24 Hour'
      || form.elements.timezone.value !== 'Europe/London (GMT+0)')
      throw new Error('Fresh setup did not show the requested clock and metadata defaults')
    if (!form.elements.base_tone_enabled.checked || !form.elements.warm_tones_enabled.checked
      || form.elements.base_tone.value !== '23' || form.elements.warm_tone_intensity.value !== '64'
      || form.elements.warm_tone_override.value !== 'true')
      throw new Error('Fresh setup did not show the requested tone defaults')
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
    if (form.elements.portrait_pair_framing.value !== 'Fill') throw new Error('ESPHome pair framing was not imported')
    form.elements.portrait_pair_framing.value = 'Fit'
    if (form.elements.brightness_day.value !== '100') throw new Error('Existing brightness was not imported')
    if (form.elements.brightness_night.value !== '75') throw new Error('Existing nighttime brightness was not imported')
    if (form.elements.screen_rotation.value !== '0') throw new Error('Existing mounting orientation was not imported')
    if (!form.elements.portrait_pairs_only.checked || form.elements.portrait_pairs_only.disabled)
      throw new Error('Active paired-portrait preference was not imported')
    if (form.elements.portrait_pairing_range.value !== 'Within 2 Days' || form.elements.portrait_pairing_range.disabled)
      throw new Error('Existing pairing range was not imported')
    await loadBackup(${JSON.stringify(JSON.stringify(personBackup))}, 'Backup loaded.')
    if (form.elements.display_mode.value !== '' || form.elements.portrait_pair_framing.value !== '')
      throw new Error('A backup without framing kept framing from a different backup')
    await loadBackup(${JSON.stringify(JSON.stringify(malformedBackup))}, 'Choose a photo source before saving.')
    if (form.elements.photo_source.value || form.checkValidity()) throw new Error('Unrepresentable filters allowed a save')
    form.requestSubmit()
    if (attempts) throw new Error('USB opened with unresolved photo filters')
    await loadBackup(${JSON.stringify(JSON.stringify(invalidOrientationBackup))}, 'Choose which photo orientations')
    if (form.elements.photo_source.value !== 'album' || form.elements.photo_orientation.value || form.checkValidity())
      throw new Error('Invalid photo orientation was not isolated from the source')
    await loadBackup(${JSON.stringify(JSON.stringify(backup))}, 'Backup loaded.')
    if (form.elements.frame_name.value !== 'Salon été & famille') throw new Error('The backup name was not prefilled')
    if (form.elements.immich_url.value !== 'https://photos.test') throw new Error('Immich URL was not prefilled')
    if (form.elements.immich_key.value !== 'entered-api-key') throw new Error('A backup replaced the entered key')
    if (form.elements.transition.value !== 'Slide Down' || form.elements.transition_speed.value !== 'Slow')
      throw new Error('Slideshow transition controls were not prefilled')
    const homeAssistantKey = form.elements.home_assistant_key
    if (homeAssistantKey.value) throw new Error('A backup prefilled the Home Assistant key')
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
    if (form.elements.portrait_pairing_range.value !== 'Any Time' || !form.elements.portrait_pairing_range.disabled)
      throw new Error('Inactive pairing range was not retained')
    if (form.elements.portrait_pair_framing.value !== 'Fill' || !form.elements.portrait_pair_framing.disabled)
      throw new Error('Inactive pair framing was not retained')
    homeAssistantKey.value = 'invalid'
    form.requestSubmit()
    if (attempts || !document.querySelector('#home-assistant').open || homeAssistantKey.validity.valid)
      throw new Error('USB setup accepted an invalid Home Assistant key')
    const haKey = 'AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA='
    homeAssistantKey.value = haKey
    homeAssistantKey.dispatchEvent(new Event('input'))
    if (form.elements.prepared_slide_url || form.elements.prepared_slide_token)
      throw new Error('Photo preparation settings appeared in the installer')
    form.elements.web_password.value = 'test-admin-password'
    const saved = waitFor(document.querySelector('#status'), 'Settings saved.')
    const find = document.querySelector('#find-frame')
    const networkStatus = document.querySelector('#network-status')
    const foundAfterSave = waitFor(networkStatus, 'Open frame settings')
    form.requestSubmit()
    await saved
    if (!find.disabled || !document.querySelector('#revision').disabled)
      throw new Error('A second USB action was available while waiting for the restart')
    await foundAfterSave
    if (attempts !== 1 || writes !== 1 || addressChecks !== 3 || find.disabled ||
      networkStatus.querySelector('a')?.href !== 'http://192.168.50.10/' ||
      !networkStatus.textContent.includes('sign in as espframe'))
      throw new Error('Setup did not automatically find the same frame after it restarted')
    if (attempts !== 1 || received.immich_key !== 'entered-api-key' || received.ssid !== 'Test Wi-Fi'
      || received.web_password !== 'test-admin-password'
      || received.photo_source !== 'memories' || received.album_id !== '${album}' || received.slide_seconds !== 86400
      || received.memories_window !== 'Within 7 Days' || received.memories_fallback !== false
      || received.portrait_pairing !== false || received.portrait_pairs_only !== true
      || received.portrait_pairing_range !== 'Any Time' || received.portrait_pair_framing !== 'Fill'
      || received.transition !== 'Slide Down' || received.transition_speed !== 'Slow'
      || received.display_mode !== 'Fit' || received.home_assistant_key !== haKey || Object.hasOwn(received, 'backup'))
      throw new Error('Unexpected migrated USB settings')
    if (received.frame_name !== 'Salon été & famille' || Object.hasOwn(received, 'hostname'))
      throw new Error('The frame name was not sent independently of the old hostname')
    if (received.show_clock !== false || received.clock_format !== '12 Hour' || received.timezone !== 'America/New_York (GMT-5)')
      throw new Error('The clock settings did not reach USB setup')
    if (received.ntp_server_1 !== 'time.example.test' || received.ntp_server_2 !== '' || received.ntp_server_3 !== '2001:db8::5')
      throw new Error('The ordered time servers did not reach USB setup')
    if (received.conn_timeout !== '3 minutes')
      throw new Error('The saved connection timeout did not reach USB setup')
    if (JSON.stringify(received.firmware_updates) !== JSON.stringify(${JSON.stringify(backup.firmware_updates)}))
      throw new Error('The update preferences did not reach USB setup')
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
    if (received.warm_tone_override !== true)
      throw new Error('Warm tone override did not reach USB setup')
    if (received.device_language !== 'he')
      throw new Error('The saved photo date language did not reach USB setup')
    if (received.screen_rotation !== '180' || received.photo_orientation !== 'Landscape Only')
      throw new Error('Photo and mounting orientation did not reach USB setup independently')
    if (!received.schedule_enabled || received.schedule_on_hour !== 21 || received.schedule_off_hour !== 6
      || received.schedule_wake_timeout !== 30) throw new Error('Screen schedule did not reach USB setup')
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
    const reused = waitFor(networkStatus, 'Open frame settings')
    form.requestSubmit()
    await reused
    if (attempts !== 4 || received.immich_key !== 'after-improv'
      || Object.hasOwn(received, 'ssid') || Object.hasOwn(received, 'password') || Object.hasOwn(received, 'frame_name')
      || Object.hasOwn(received, 'home_assistant_key') || Object.hasOwn(received, 'display_mode')
      || Object.hasOwn(received, 'web_password')
      || Object.hasOwn(received, 'portrait_pair_framing') || Object.hasOwn(received, 'portrait_pairs_only')
      || Object.hasOwn(received, 'brightness_day') || Object.hasOwn(received, 'warm_tone_override')
      || Object.hasOwn(received, 'prepared_slide_url')
      || Object.hasOwn(received, 'prepared_slide_token'))
      throw new Error('Immich setup sent settings that were left blank')
    await loadBackup(${JSON.stringify(filteredBackup)}, 'Backup loaded.')
    if (form.elements.photo_source.value !== 'custom' || document.querySelector('#filter-controls').hidden
      || !form.elements.people_enabled.checked || form.elements.minimum_rating.value !== '4+')
      throw new Error('Compound photo filters did not appear in the source editor')
    form.elements.portrait_pair_framing.value = 'Follow photo framing'
    form.elements.portrait_pair_framing.dispatchEvent(new Event('change', { bubbles: true }))
    form.elements.immich_key.value = 'filtered-key'
    reuse.checked = true
    reuse.dispatchEvent(new Event('change'))
    const removeKey = document.querySelector('#remove-home-assistant-key')
    removeKey.checked = true
    removeKey.dispatchEvent(new Event('change'))
    if (!homeAssistantKey.disabled) throw new Error('The key field is enabled while removal is selected')
    const filtered = waitFor(networkStatus, 'Open frame settings')
    form.requestSubmit()
    await filtered
    if (attempts !== 5 || received.photo_source !== 'custom' || !received.albums_enabled || !received.people_enabled
      || !received.tags_enabled || received.album_order !== 'Album list order' || received.filter_country !== 'New Zealand'
      || received.excluded_tag_ids !== '66666666-6666-4666-8666-666666666666' || received.minimum_rating !== '4+'
      || received.favorite_mode !== 'Exclude favorites' || received.immich_key !== 'filtered-key'
      || received.home_assistant_key !== '' || received.portrait_pair_framing !== 'Follow photo framing')
      throw new Error('Compound photo filters did not reach USB setup')

    reuse.checked = true
    reuse.dispatchEvent(new Event('change'))
    form.elements.immich_url.value = 'https://photos.test'
    form.elements.immich_key.value = 'migration-key'
    homeAssistantKey.value = 'unsent manually entered value'
    const keepPairing = document.querySelector('#keep-stock-pairing')
    keepPairing.checked = true
    keepPairing.dispatchEvent(new Event('change'))
    if (!homeAssistantKey.disabled || removeKey.checked)
      throw new Error('The stock pairing option allowed conflicting key changes')
    const saveStatus = document.querySelector('#status')
    const unavailable = waitFor(saveStatus, 'could not keep the ESPHome pairing')
    form.requestSubmit()
    await unavailable
    if (attempts !== 6 || lastWrite !== 'MIGRATE' || Object.hasOwn(received, 'home_assistant_key') ||
      form.elements.immich_key.value !== 'migration-key')
      throw new Error('A missing stock pairing lost the installer settings or transmitted a key')
    const migrated = waitFor(saveStatus, 'Home Assistant pairing kept')
    const online = waitFor(networkStatus, 'Open frame settings')
    form.requestSubmit()
    await migrated
    await online
    if (attempts !== 7 || lastWrite !== 'MIGRATE' || Object.hasOwn(received, 'home_assistant_key'))
      throw new Error('The installer did not migrate the existing pairing on the frame')

    const keepWifi = document.querySelector('#keep-stock-wifi')
    const keepImmich = document.querySelector('#keep-stock-immich')
    await loadBackup(${JSON.stringify(JSON.stringify(personBackup))}, 'Backup loaded.')
    form.elements.immich_url.value = 'https://manual.test'
    form.elements.immich_key.value = 'manual-value'
    reuse.checked = true
    reuse.dispatchEvent(new Event('change'))
    for (const option of [keepWifi, keepImmich, keepPairing]) {
      option.checked = true
      option.dispatchEvent(new Event('change'))
    }
    if (reuse.checked || !form.elements.ssid.disabled ||
        !form.elements.immich_url.disabled || !form.elements.immich_key.disabled)
      throw new Error('Keeping stock connections allowed conflicting manual entries')
    for (const source of ['Wi-Fi', 'Immich']) {
      const missing = waitFor(saveStatus, 'could not keep the ESPHome ' + source)
      form.requestSubmit()
      await missing
      if (form.elements.immich_url.value !== 'https://manual.test' || form.elements.immich_key.value !== 'manual-value')
        throw new Error('An unavailable stock connection lost the unsent settings')
    }
    const kept = waitFor(networkStatus, 'Open frame settings')
    form.requestSubmit()
    await kept
    const privateFields = ['ssid', 'password', 'immich_url', 'immich_key', 'home_assistant_key']
    if (attempts !== 10 || lastWrite !== 'IMPORT' || lastFlags !== '7' ||
        privateFields.some(field => Object.hasOwn(received, field)))
      throw new Error('Stock connection migration included credentials in the browser request')
    if (received.photo_source !== 'custom' || received.legacy_photo_preset !== 'Person' ||
        !received.people_enabled || received.person_ids !== '${album}')
      throw new Error('The Person preset and its saved selection did not reach USB setup')
  }
  window.addEventListener('DOMContentLoaded', () => {
    function report(result) {
      return fetch('test-result', { method: 'POST', body: result })
    }
    runSetup().then(() => report('passed'), error =>
      report([error.message, document.querySelector('#status').textContent,
        document.querySelector('#network-status').textContent].join('\\n')))
  })
`
const pages = new Map([
  ['/', ['text/html', html.replace('</head>', '<script src="verify.js"></script></head>')]],
  ['/setup.js', ['text/javascript', bundle]],
  ['/setup.css', ['text/css', css]],
  ['/verify.js', ['text/javascript', verify]],
])
let completeTest
const testResult = new Promise(resolve => { completeTest = resolve })
const server = createServer(async (request, response) => {
  if (request.method === 'POST' && request.url === '/test-result') {
    request.setEncoding('utf8')
    let result = ''
    for await (const chunk of request) result += chunk
    response.writeHead(204).end()
    completeTest(result)
    return
  }
  const page = pages.get(request.url)
  if (page) response.writeHead(200, { 'Content-Type': page[0] }).end(page[1])
  else response.writeHead(404).end()
})
await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
const profile = await mkdtemp(join(tmpdir(), 'espframe-installer-browser-'))
const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
  '--headless', '--no-sandbox', '--disable-gpu', `--user-data-dir=${profile}`,
  `http://127.0.0.1:${server.address().port}/`,
], { signal: AbortSignal.timeout(30_000), stdio: ['ignore', 'ignore', 'pipe'] })
let errors = ''
chrome.stderr.on('data', chunk => { errors += chunk })
const stopped = new Promise(resolve => chrome.once('close', resolve))
try {
  const browserFailure = new Promise((_, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => reject(new Error(`Installer test stopped before reporting a result (${code}).\\n${errors}`)))
  })
  const result = await Promise.race([testResult, browserFailure])
  assert.equal(result, 'passed')
  console.log('Packaged native installer transfers supported backup fields and finds the frame after restart over mocked USB')
} finally {
  chrome.kill()
  await stopped
  server.close()
  await rm(profile, { recursive: true, force: true })
}
