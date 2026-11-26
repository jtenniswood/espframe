import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const web = '../main/web/'
const load = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, bundle, cards, css] = await Promise.all([
  load(web + 'admin_preview.html'),
  load(web + 'admin/app.js'),
  load(web + 'admin_cards.js'),
  load('../../../docs/public/webserver/style.css'),
])

const capabilities = '/espframe/api/v1/capabilities'
const configuration = '/espframe/api/v1/configuration'
const resetPath = '/espframe/api/v1/reset'
const releasePath = '/api/firmware/releases'
const wifiPath = '/api/firmware/wifi'
const writes = []
const deviceActions = []
const logPositions = []
const releaseRequests = []
const wifiRequests = []
const uploadedImages = []
const unexpected = []
let restartPending = false
let restartsObserved = 0
let bootChecks = 0
let configurationReads = 0
let configurationBoot = 1
let configurationTag = 0
let lastConfiguration
let unchangedReads = 0
let release = { current: 'v0.1.0-preview.9', versions: [], status: 'idle', available: true }
let releaseFeedOffline = false
let wifi = { current: '2.12.8', latest: '', status: 'idle' }
let wireguard = { running: false, connected: false, restart_required: false }
const uploadedTarget = { offset: 0x410000, elf_sha256: 'a'.repeat(64) }
let uploadedBootChecks = 0
const snapshot = {
  api_version: 1, reset_epoch: 2, api_key_configured: true,
  secrets_configured: { wireguard_private_key: false },
  values: {
    immich_url: 'https://photos.test/api',
    conn_timeout: '10 minutes',
    immich_filter_support: 'unknown',
    photo_source: 'All Photos',
    memories_window: 'Within 2 Days',
    memories_fallback: true,
    albums_enabled: true,
    album_ids: '11111111-1111-4111-8111-111111111111',
    album_matching: 'Any selected album',
    inclusion_matching: 'Match all enabled groups',
    album_id: '',
    photo_source_mode: 'custom',
    album_labels: '["Summer"]',
    excluded_tag_labels: '',
    portrait_pairing: true,
    portrait_pairs_only: false,
    portrait_pairing_range: 'Same Day',
    portrait_pair_framing: 'Follow photo framing',
    portrait_pair_divider: 'Black',
    interval: '15 seconds',
    photo_orientation: 'Any', display_mode: 'Fit', transition: 'Cross Fade', transition_speed: 'Fast',
    screen_rotation: '0',
    developer_features_enabled: false,
    brightness_day: 90,
    brightness_night: 75,
    schedule_enabled: true, schedule_on_hour: 6, schedule_off_hour: 23, schedule_wake_timeout: 60,
    sunrise: '',
    sunset: '',
    show_clock: true,
    clock_format: '24 Hour',
    timezone: 'Europe/London (GMT+0)',
    ntp_server_1: 'pool.ntp.org',
    ntp_server_2: '',
    ntp_server_3: '',
    auto_update: true,
    c6_auto_update: true,
    update_frequency: 'Daily',
    frame_name: 'Living Room',
    wireguard_enabled: false,
    wireguard_endpoint: '',
    wireguard_port: 51820,
  },
  unavailable: ['warm_tone_override'],
}
const browserTest = String.raw`
  async function until(condition, description) {
    for (let i = 0; i < 220; ++i) {
      if (await condition()) return
      await new Promise(resolve => setTimeout(resolve, 25))
    }
    throw new Error('Timed out waiting for ' + description)
  }

  async function verify() {
    const tab = name => [...document.querySelectorAll('nav [role=tab]')].find(el => el.textContent === name)
    const card = name => [...document.querySelectorAll('.card-toggle')].find(el => el.textContent.trim() === name)
    const openCard = name => {
      const toggle = card(name)
      if (toggle.getAttribute('aria-expanded') !== 'true') toggle.click()
      return toggle.closest('.card')
    }
    const change = (control, value) => {
      control.value = value
      control.dispatchEvent(new Event('change', { bubbles: true }))
    }
    const button = (root, label) => [...root.querySelectorAll('button')].find(el => el.textContent.trim() === label)
    const setting = (root, label) => root.querySelector('[aria-label="' + label + '"]')
    const field = (root, label) => [...root.querySelectorAll('label')].find(el => el.textContent === label).control
    const savedValue = async (key, expected) => {
      const response = await fetch('/espframe/api/v1/configuration')
      return (await response.json()).values[key] === expected
    }
    if (sessionStorage.getItem('test-after-reset')) {
      await until(() => button(document, 'Connect') && card('Import Settings'), 'the shared setup wizard')
      document.querySelector('input[type="url"]').value = 'setup-photos.test'
      document.querySelector('input[type="password"]').value = 'setup-key'
      button(document, 'Connect').click()
      await until(() => button(document, 'Done'), 'the wizard to advance after the frame reconnects')
      button(document, 'Done').click()
      await until(() => card('Backup') && card('Connection'), 'the configured admin')

      const click = HTMLInputElement.prototype.click
      HTMLInputElement.prototype.click = function () {
        if (this.type !== 'file') return click.call(this)
        const transfer = new DataTransfer()
        transfer.items.add(new File([JSON.stringify({
          version: 3,
          connection: { immich_url: 'backup-photos.test', api_key: 'ignore-test-key' },
          photos: { portrait_pair_framing: 'Fit' },
          frequency: { interval: '47 seconds' },
          screen: { brightness_day: 77, schedule_wake_timeout: 83 },
          firmware_updates: { wifi_auto_update: true },
        })], 'fixture.json'))
        this.files = transfer.files
        this.dispatchEvent(new Event('change'))
      }
      tab('Device').click()
      button(openCard('Backup'), 'Import').click()
      await until(() => document.body.textContent.includes('Backup restored successfully'), 'the backup restore')
      if (!await savedValue('immich_url', 'https://backup-photos.test/api') ||
          !await savedValue('portrait_pair_framing', 'Fit') || !await savedValue('c6_auto_update', true) ||
          !await savedValue('interval', '47 seconds') || !await savedValue('brightness_day', 77) ||
          !await savedValue('schedule_wake_timeout', 83)) {
        throw new Error('Backup did not restore connection, playback, display and update preferences')
      }
      document.body.dataset.result = 'passed'
      return
    }
    await until(() => card('Frequency') && card('Photo Display') && card('Connection'), 'Immich cards')
    if (tab('Immich').getAttribute('aria-selected') !== 'true') throw new Error('Immich did not open first')
    if (!tab('Logs') || card('WireGuard VPN') || card('Screen Tone')) throw new Error('Incorrect controls appeared')
    const rating = () => setting(card('Filters').closest('.card'), 'Filter by Rating')
    if (rating().tabIndex !== -1) throw new Error('Rating became available before discovery')
    await until(() => rating().tabIndex === 0, 'Immich to report support for structured filters')
    const vpnSwitch = setting(openCard('Connection'), 'Connect through WireGuard')
    const vpn = vpnSwitch.closest('.wireguard')
    if (getComputedStyle(vpn).borderTopWidth !== '1px') throw new Error('Missing WireGuard section divider')
    const tunnel = vpnSwitch.parentElement.querySelector('#wireguard-status')
    const notice = vpn.querySelector('.reset-backup-notice')
    await until(() => tunnel.textContent.includes('Tunnel is off'), 'the offline WireGuard badge')
    if (vpnSwitch.getAttribute('aria-checked') !== 'false' || getComputedStyle(notice).display !== 'none')
      throw new Error('WireGuard began enabled or requested a restart before a change')
    if (getComputedStyle(tunnel.querySelector('.dot')).backgroundColor !== getComputedStyle(tunnel).color)
      throw new Error('The offline WireGuard badge is missing its grey indicator')
    const panel = vpn.querySelector('.inline-disclosure')
    const panelToggle = panel.querySelector('button')
    if (panelToggle.textContent.trim() !== 'Wireguard Vpn Settings') throw new Error('Incorrect VPN panel title')
    if (getComputedStyle(panel).display !== 'none') throw new Error('The VPN settings panel appeared while disabled')
    vpnSwitch.click()
    await until(() => getComputedStyle(notice).display !== 'none', 'restart notice after saving WireGuard')
    if (getComputedStyle(panel).display === 'none' || panelToggle.getAttribute('aria-expanded') !== 'false')
      throw new Error('VPN settings did not start in a collapsed panel')
    panelToggle.click()
    if (getComputedStyle(panel.querySelector('.inline-disclosure-body')).display === 'none')
      throw new Error('The VPN settings panel did not open')
    const port = [...vpn.querySelectorAll('label')].find(label => label.textContent === 'Server Port').control
    change(port, '51820')
    const serverAddress = [...vpn.querySelectorAll('label')].find(label => label.textContent === 'Server Address').control
    change(serverAddress, 'vpn.example.test')
    const privateKey = 'A'.repeat(43) + '='
    vpn.querySelector('input[type="password"]').value = privateKey
    button(vpn, 'Save').click()
    await until(() => vpn.querySelector('.key-mask'), 'the saved WireGuard key to be masked')
    if (vpn.querySelector('input[type="password"]')) throw new Error('WireGuard key remained in the form')
    button(vpn, 'Restart now').click()
    await until(() => vpn.querySelector('#wireguard-status').textContent.includes('Connected'), 'WireGuard status after restart')
    if (!tunnel.title.includes('handshake')) throw new Error('WireGuard handshake age was missing')
    if (getComputedStyle(notice).display !== 'none') throw new Error('Restart notice remained after reboot')
    const frequencyCard = openCard('Frequency')
    const select = frequencyCard.querySelector('select')
    if (select.value !== '15 seconds') throw new Error('Native configuration was not loaded')
    change(select, '30 seconds')
    await until(async () => {
      const response = await fetch('/espframe/api/v1/configuration')
      return (await response.json()).values.interval === '30 seconds'
    }, 'the interval save')
    change(select, 'Custom')
    const interval = field(frequencyCard, 'Exact duration (seconds)')
    if (interval.min !== '2' || interval.max !== '86400' || interval.value !== '30') throw new Error('Exact interval range is wrong')
    change(interval, '43')
    await until(() => savedValue('interval', '43 seconds'), 'the exact interval save')
    interval.blur()
    const pairing = openCard('Portrait Pairing')
    const framing = [...pairing.querySelectorAll('select')].find(control =>
      [...control.options].some(option => option.textContent === 'Fill each half'))
    if (!framing) throw new Error('Portrait fill control was missing')
    change(framing, 'Fill')
    await until(() => savedValue('portrait_pair_framing', 'Fill'), 'portrait fill save')
    const divider = [...pairing.querySelectorAll('select')].find(control =>
      [...control.options].some(option => option.value === 'White'))
    if (!divider || divider.value !== 'Black' || divider.options.length !== 2)
      throw new Error('Portrait divider did not default to Black')
    change(divider, 'White')
    await until(() => savedValue('portrait_pair_divider', 'White'), 'portrait divider save')

    const memories = openCard('Memories')
    const filters = openCard('Filters')
    const memoryToggle = setting(memories, 'Show Memories Only')
    const ratingToggle = setting(filters, 'Filter by Rating')
    if (ratingToggle.getAttribute('aria-disabled') === 'true') throw new Error('Structured filters were disabled')
    memoryToggle.click()
    if (!filters.classList.contains('memory-filter-disabled') || ratingToggle.getAttribute('aria-disabled') !== 'true') {
      throw new Error('Memories did not lock photo filters')
    }
    await until(() => savedValue('photo_source', 'Memories'), 'Memories to save')
    memoryToggle.click()
    await until(() => savedValue('photo_source', 'Custom'), 'saved filters to resume')
    if (filters.classList.contains('memory-filter-disabled')) throw new Error('Filters stayed disabled after Memories')
    const album = [...filters.querySelectorAll('details')].find(panel => panel.querySelector('summary').textContent === 'Included Albums')
    album.open = true
    const id = album.querySelector('input[placeholder^="Paste album UUID"]')
    const label = album.querySelector('input[placeholder="Optional label"]')
    id.value = '22222222-2222-4222-8222-222222222222'
    id.dispatchEvent(new Event('input', { bubbles: true }))
    label.value = 'Family trip'
    label.dispatchEvent(new Event('input', { bubbles: true }))
    await until(() => savedValue('album_labels', '["Family trip"]'), 'the album IDs and labels to save as a pair')
    change(field(album, 'Matching Albums'), 'All selected albums')
    await until(() => savedValue('album_matching', 'All selected albums'), 'album matching save')
    change(field(filters, 'Inclusion Groups'), 'Match any enabled group')
    await until(() => savedValue('inclusion_matching', 'Match any enabled group'), 'inclusion matching save')
    setting(filters, 'Filter by Date').click()
    change(field(filters, 'From'), '0096-02-29')
    await until(() => savedValue('date_from', '0096-02-29'), 'the date range to save')

    const presets = () => button(openCard('Filters'), 'Photo source presets').closest('.inline-disclosure')
    button(filters, 'Photo source presets').click()
    change(field(presets(), 'Photo source'), 'album')
    field(presets(), 'Immich album link or ID').value = 'https://photos.test/albums/AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA'
    button(presets(), 'Save photo source').click()
    await until(async () => !button(presets(), 'Save photo source').disabled && await savedValue('photo_source_mode', 'album'),
      'the standalone album source')
    if (!await savedValue('album_id', 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa')) throw new Error('The album link did not save as an ID')
    change(field(presets(), 'Photo source'), 'custom')
    button(presets(), 'Save photo source').click()
    await until(() => savedValue('photo_source_mode', 'custom'), 'the custom source to resume')
    const connection = openCard('Connection')
    const url = connection.querySelector('input[type="url"]')
    change(url, 'new-photos.test')
    await until(() => connection.querySelector('#conn-status').textContent.trim() === 'URL saved', 'URL save and reconnect')
    if (url.value !== 'https://new-photos.test/api') throw new Error('Immich URL was not normalized')
    const immichKey = [...connection.querySelectorAll('label')].find(label => label.textContent === 'API Key').closest('.field')
    button(immichKey, 'Change').click()
    immichKey.querySelector('.input-group input').value = 'replacement-test-key'
    button(immichKey, 'Save').click()
    await until(() => immichKey.querySelector('.key-mask'), 'API key confirmation')
    if (immichKey.querySelector('.input-group input')) throw new Error('Saved secret remained in the form')
    tab('Device').click()
    if (tab('Device').getAttribute('aria-selected') !== 'true' ||
        !document.querySelector('#sp-settings.active') || !card('Rotation') || !card('Screen Brightness'))
      throw new Error('Device tab did not use the shared shell')
    if (document.querySelector('a[href="/settings"]') || document.body.textContent.includes('Photo sources & device tools'))
      throw new Error('Obsolete device-tools link is still visible')
    const brightness = openCard('Screen Brightness').querySelector('input[type="range"]')
    if (brightness.value !== '90') throw new Error('Brightness preference was not loaded')
    if (document.querySelector('#sun-info').style.display !== 'none') throw new Error('Solar times appeared before clock sync')
    change(brightness, '87')
    const schedule = openCard('Night Schedule')
    change(field(schedule, 'When Woken, Idle Time To Screen Off'), 'Custom')
    const wake = field(schedule, 'Exact duration (seconds)')
    if (wake.min !== '10' || wake.max !== '3600' || wake.value !== '60') throw new Error('Exact wake duration range is wrong')
    change(wake, '73')
    await until(() => savedValue('schedule_wake_timeout', 73), 'the exact wake timeout save')
    wake.blur()

    const firmware = openCard('Firmware')
    const disclosures = [...firmware.querySelectorAll('.inline-disclosure-button')]
    const disclosure = name => disclosures.find(el => el.firstChild.textContent === name)
    disclosure('WiFi firmware').click()
    const wifiFirmware = disclosure('WiFi firmware').closest('.inline-disclosure')
    const wifiToggle = wifiFirmware.querySelector('[role="switch"]')
    if (wifiToggle.getAttribute('aria-checked') !== 'true') throw new Error('WiFi auto-update preference was not loaded')
    wifiToggle.click()
    await until(() => savedValue('c6_auto_update', false), 'WiFi auto-update preference save')
    await until(() => wifiFirmware.textContent.includes('2.12.8'), 'current WiFi firmware')
    button(wifiFirmware, 'Check for Update').click()
    await until(() => button(wifiFirmware, 'Update WiFi Firmware'), 'WiFi firmware availability')
    button(wifiFirmware, 'Update WiFi Firmware').click()
    await until(() => document.body.textContent.includes('WiFi firmware update complete.'), 'verified WiFi firmware')
    disclosure('Auto updates').click()
    const automation = disclosure('Auto updates').closest('.inline-disclosure')
    const toggle = automation.querySelector('[role="switch"]')
    if (toggle.getAttribute('aria-checked') !== 'true') throw new Error('Auto-update preference was not loaded')
    change(automation.querySelector('select'), 'Weekly')
    toggle.click()

    disclosure('Firmware updates').click()
    const updates = disclosure('Firmware updates').closest('.inline-disclosure')
    await until(() => button(updates, 'Check for Update')?.disabled === false, 'the initial release check')
    button(updates, 'Check for Update').click()
    await until(() => button(updates, 'Install Update')?.disabled === false, 'an available update')
    if (!updates.textContent.includes('v0.1.0-preview.9') || !updates.textContent.includes('v0.1.0-preview.10')) {
      throw new Error('Current or available version was missing')
    }
    const previous = disclosure('Previous firmware').closest('.inline-disclosure')
    if (previous.style.display === 'none' || previous.querySelector('select').value !== 'v0.1.0-preview.8') {
      throw new Error('Previous release was unavailable')
    }
    button(updates, 'Install Update').click()
    await until(() => document.body.textContent.includes('Firmware update complete.'), 'a verified firmware reboot')
    let downgrading = false
    window.confirm = message => {
      downgrading = message.includes('v0.1.0-preview.8')
      return downgrading
    }
    disclosure('Previous firmware').click()
    change(previous.querySelector('select'), 'v0.1.0-preview.8')
    button(previous, 'Install').click()
    if (!downgrading) throw new Error('Installing an older release did not ask for confirmation')
    await until(() => updates.textContent.includes('Current version v0.1.0-preview.8'), 'the older release')
    disclosure('Upload firmware file').click()
    const upload = disclosure('Upload firmware file').closest('.inline-disclosure')
    const image = field(upload, 'Native firmware image')
    const installFile = button(upload, 'Install update and restart')
    await until(() => !installFile.disabled && upload.textContent.includes('Choose v2-ota.bin') &&
      updates.textContent.includes('Firmware release information is unavailable.'), 'file upload without a release feed')
    const chooseFirmware = (bytes) => {
      const transfer = new DataTransfer()
      transfer.items.add(new File([bytes], 'v2-ota.bin', { type: 'application/octet-stream' }))
      image.files = transfer.files
    }
    chooseFirmware('invalid image')
    installFile.click()
    await until(() => upload.textContent.includes('The firmware image failed validation.') && !installFile.disabled, 'an upload rejection and retry')
    chooseFirmware('native image')
    installFile.click()
    await until(() => upload.textContent.includes('The frame passed its startup checks.'), 'a verified boot of the uploaded image')
    await until(async () => {
      const response = await fetch('/espframe/api/v1/configuration')
      const { values } = await response.json()
      return values.brightness_day === 87 && values.auto_update === false && values.update_frequency === 'Weekly'
    }, 'brightness and update preference saves')
    if (!document.querySelector('a.sp-tab-docs')) throw new Error('Shared Docs link is missing')
    tab('Logs').click()
    const log = document.querySelector('.sp-log-output')
    await until(() => log.querySelector('.sp-log-info')?.textContent.includes('Player ready'), 'the startup log')
    await until(() => log.querySelector('.sp-log-warn')?.textContent.includes('Preview unavailable'), 'the live warning')
    button(document.querySelector('#sp-logs'), 'Clear').click()
    if (log.children.length !== 0) throw new Error('Clearing logs left the previous lines visible')
    await until(() => log.textContent.includes('Wi-Fi connected'), 'the log after clearing')
    await until(() => log.textContent.includes('Frame restarted'), 'the log stream after reboot')
    if (log.children.length !== 2 || log.textContent.includes('Preview unavailable')) throw new Error('Old logs were replayed')
    tab('Device').click()
    let exported
    const createObjectURL = URL.createObjectURL.bind(URL)
    URL.createObjectURL = blob => {
      exported = blob.text().then(JSON.parse)
      return createObjectURL(blob)
    }
    const click = HTMLAnchorElement.prototype.click
    HTMLAnchorElement.prototype.click = function () {
      if (!this.download) click.call(this)
    }
    const exportButton = button(openCard('Backup'), 'Export')
    await until(() => !exportButton.disabled, 'backup export')
    exportButton.click()
    await until(() => exported, 'the exported backup')
    const backup = await exported
    if (backup.version !== 3 || backup.photos.portrait_pair_framing !== 'Fill' ||
        backup.photos.portrait_pair_divider !== 'White' ||
        backup.connection.api_key !== undefined || backup.wireguard !== undefined ||
        backup.firmware_updates.wifi_auto_update !== false || backup.frequency.interval !== '43 seconds' ||
        backup.screen.brightness_day !== 87 || backup.screen.schedule_wake_timeout !== 73) {
      throw new Error('Backup included a secret or lost a native setting')
    }
    tab('Immich').click()
    const frequency = openCard('Frequency')
    const liveSelect = frequency.querySelector('select')
    const albumPanel = [...openCard('Filters').querySelectorAll('details')].find(panel =>
      panel.querySelector('summary').textContent === 'Included Albums')
    albumPanel.open = true
    const draft = [...openCard('Connection').querySelectorAll('label')].find(label => label.textContent === 'Server Address').control
    draft.focus()
    draft.value = 'unsaved.example.test'
    await fetch('/test/remote-settings', { method: 'POST' })
    await new Promise(resolve => setTimeout(resolve, 2200))
    if (document.activeElement !== draft || draft.value !== 'unsaved.example.test' || liveSelect.value !== 'Custom') {
      throw new Error('A remote change interrupted an active edit')
    }
    draft.blur()
    await until(() => card('Frequency').closest('.card').querySelector('select').value === '15 seconds', 'a remote settings change')
    if (card('Frequency').getAttribute('aria-expanded') !== 'true' ||
        ![...card('Filters').closest('.card').querySelectorAll('details')].some(panel =>
          panel.open && panel.querySelector('summary').textContent === 'Included Albums')) {
      throw new Error('Live settings refresh closed an open panel')
    }
    await until(() => document.title.includes('Hallway'), 'the remote frame name after restart')
    tab('Device').click()
    if (!document.querySelector('#sun-info').textContent.includes('Sunrise: 6:30 AM') ||
        !document.querySelector('#sun-info').textContent.includes('Sunset: 7:05 PM')) {
      throw new Error('The device brightness card did not display the frame’s daylight times')
    }
    const rotation = () => card('Rotation').closest('.card').querySelector('select')
    const angles = () => [...rotation().options].map(option => option.value).sort().join(',')
    if (angles() !== '0,180') throw new Error('Portrait rotations were visible before Developer Features was enabled')
    setting(openCard('Developer'), 'Enable in-development features').click()
    await until(async () => await savedValue('developer_features_enabled', true) && angles() === '0,180,270,90',
      'the developer portrait orientations')
    change(rotation(), '90')
    await until(async () => await savedValue('screen_rotation', '90') && await savedValue('portrait_pairing', false),
      'portrait rotation to turn portrait pairing off')
    setting(openCard('Developer'), 'Enable in-development features').click()
    await until(async () => await savedValue('developer_features_enabled', false) && await savedValue('screen_rotation', '0') &&
      angles() === '0,180' && rotation().value === '0', 'disabling developer mode to restore the landscape orientation')
    const clock = openCard('Clock')
    setting(clock, 'Show Clock').click()
    await until(() => savedValue('show_clock', false), 'showing and hiding the clock')
    const zone = 'Asia/Tokyo (GMT+9)'
    change(clock.querySelector('option[value="' + zone + '"]').closest('select'), zone)
    await until(() => savedValue('timezone', zone), 'the saved timezone')
    button(clock, 'Advanced').click()
    change(setting(clock, 'NTP Server 2'), 'time.example.test')
    await until(() => savedValue('ntp_server_2', 'time.example.test'), 'the saved NTP server')
    const reboot = button(openCard('Device Reboot'), 'Reboot Screen')
    reboot.click()
    await until(() => !reboot.disabled, 'the reboot action')
    const reset = openCard('Factory Reset')
    const partial = reset.querySelector('.reset-action-panel button')
    const complete = reset.querySelector('.reset-action-danger button')
    complete.click()
    await until(() => document.querySelector('.reset-confirm-dialog'), 'factory reset confirmation')
    const dialog = document.querySelector('.reset-confirm-dialog')
    if (!button(dialog, 'Complete reset').disabled) throw new Error('Factory reset needs explicit confirmation')
    button(dialog, 'Cancel').click()
    sessionStorage.setItem('test-after-reset', 'true')
    partial.click()
    await until(() => document.querySelector('.reset-confirm-dialog'), 'partial reset confirmation')
    button(document.querySelector('.reset-confirm-dialog'), 'Partial reset').click()
  }
  verify().catch(error => { document.body.dataset.result = error.message })
`
const server = createServer(async (req, res) => {
  const send = (body, type) => res.writeHead(200, { 'content-type': type }).end(body)
  const json = body => send(JSON.stringify(body), 'application/json')
  const route = new URL(req.url, 'http://localhost')
  switch (route.pathname) {
    case '/':
      res.setHeader('content-security-policy', "default-src 'self'; img-src 'self' data:; frame-ancestors 'none'; form-action 'none'")
      return send(html.replace('</body>', '<script defer src="/verify.js"></script></body>'), 'text/html')
    case '/espframe/admin.js':
      return send(bundle, 'text/javascript')
    case '/espframe/admin-cards.js':
      return send(cards, 'text/javascript')
    case '/verify.js':
      return send(browserTest, 'text/javascript')
    case '/espframe/style.css':
      return send(css, 'text/css')
    case capabilities:
      return json({
        contract_version: 2, api_version: 1, configuration_path: configuration, reset_path: resetPath, update_mode: 'atomic',
        configuration_available: true, configuration_read: true, configuration_write: true, legacy_entity_api: false,
      })
    case configuration:
      if (req.method === 'POST') {
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        const update = JSON.parse(new URLSearchParams(Buffer.concat(chunks).toString()).get('configuration'))
        writes.push(update)
        const { api_key, wireguard_private_key, ...values } = update.values
        const wasConfigured = snapshot.api_key_configured && Boolean(snapshot.values.immich_url)
        Object.assign(snapshot.values, values)
        if (values.photo_source_mode) {
          snapshot.values.photo_source = { album: 'Album', custom: 'Custom' }[values.photo_source_mode]
        } else if (values.photo_source) {
          snapshot.values.photo_source_mode = { Memories: 'memories', Custom: 'custom' }[values.photo_source]
        }
        if (['90', '270'].includes(snapshot.values.screen_rotation)) {
          if (snapshot.values.developer_features_enabled) snapshot.values.portrait_pairing = false
          else snapshot.values.screen_rotation = '0'
        }
        if (api_key) snapshot.api_key_configured = true
        if (wireguard_private_key !== undefined) snapshot.secrets_configured.wireguard_private_key = !!wireguard_private_key
        if (Object.keys(update.values).some(key => key.startsWith('wireguard_'))) wireguard.restart_required = true
        restartPending = !wasConfigured && !!(api_key || values.immich_url)
        return json({ api_version: 1, status: 'accepted', restart_required: restartPending })
      }
      if (restartPending) {
        restartPending = false
        ++restartsObserved
        return res.writeHead(503).end()
      }
      if (++configurationReads > 1) snapshot.values.immich_filter_support = 'structured'
      const payload = JSON.stringify(snapshot)
      if (payload !== lastConfiguration) {
        lastConfiguration = payload
        ++configurationTag
      }
      const etag = 'W/"' + configurationBoot + '-' + configurationTag + '"'
      res.setHeader('etag', etag)
      if (req.headers['if-none-match'] === etag) {
        ++unchangedReads
        return res.writeHead(304).end()
      }
      return send(payload, 'application/json')
    case '/test/remote-settings':
      assert.equal(req.method, 'POST')
      snapshot.values.interval = '15 seconds'
      snapshot.values.frame_name = 'Hallway'
      snapshot.values.sunrise = '6:30 AM'
      snapshot.values.sunset = '7:05 PM'
      ++configurationBoot
      return res.writeHead(204).end()
    case '/espframe/api/v1/reboot':
      assert.equal(req.method, 'POST')
      assert.equal(req.headers.origin, 'http://' + req.headers.host)
      deviceActions.push('reboot')
      if (snapshot.values.wireguard_enabled && snapshot.secrets_configured.wireguard_private_key) {
        wireguard = { running: true, connected: true, handshake_age: 5, restart_required: false }
      }
      return json({ status: 'accepted', restart_required: true })
    case '/espframe/api/v1/wireguard':
      return json(wireguard)
    case '/espframe/api/v1/logs': {
      const after = Number(route.searchParams.get('after'))
      logPositions.push(after)
      if (logPositions.length === 2) return res.writeHead(503).end()
      if (logPositions.length >= 5) {
        return json({ boot: 8, lines: after ? [] : [{ id: 1, level: 3, message: 'Frame restarted' }], more: false })
      }
      const messages = ['Player ready', 'Preview unavailable', 'Wi-Fi connected']
      return json({ boot: 7, lines: [{ id: after + 1, level: after === 1 ? 2 : 3, message: messages[after] }], more: false })
    }
    case resetPath:
      if (req.method === 'POST') {
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        const mode = new URLSearchParams(Buffer.concat(chunks).toString()).get('mode')
        deviceActions.push(mode)
        snapshot.values.immich_url = ''
        snapshot.api_key_configured = false
        ++snapshot.reset_epoch
        return json({ status: 'accepted', restart_required: true })
      }
      return json({ status: 'ready', epoch: snapshot.reset_epoch })
    case releasePath:
      if (releaseFeedOffline) return res.writeHead(503).end()
      if (req.method === 'POST') {
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        assert.equal(req.headers['content-type'], 'text/plain')
        const version = Buffer.concat(chunks).toString()
        releaseRequests.push(version)
        if (version) release = { ...release, current: version, status: 'idle' }
        else release = { ...release, status: 'ready', versions: releaseRequests.length > 1
          ? ['v0.1.0-preview.10', 'v0.1.0-preview.9', 'v0.1.0-preview.8', 'v9.9.9'] : [] }
        return res.writeHead(202).end()
      }
      return json(release)
    case wifiPath:
      if (req.method === 'POST') {
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        const action = Buffer.concat(chunks).toString()
        wifiRequests.push(action)
        if (action === 'check') wifi = { ...wifi, latest: '3.1.0', status: 'ready' }
        if (action === 'install') wifi = { ...wifi, current: '3.1.0', status: 'idle' }
        return res.writeHead(202).end()
      }
      return json(wifi)
    case '/api/playback':
      assert.equal(req.method, 'GET')
      releaseFeedOffline = true
      return json({ firmware_update: true, board_revision: 'v2' })
    case '/api/firmware':
      if (req.method === 'POST') {
        assert.equal(req.headers['content-type'], 'application/octet-stream')
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        const bytes = Buffer.concat(chunks).toString()
        uploadedImages.push(bytes)
        if (bytes === 'invalid image') return res.writeHead(400).end('The firmware image failed validation.')
        return json({ staged: true, target: uploadedTarget })
      }
      if (uploadedImages.includes('native image')) {
        return json({ running: uploadedTarget, boot_verified: ++uploadedBootChecks > 1 })
      }
      ++bootChecks
      if (bootChecks > 1) release.status = 'ready'
      return json({ boot_verified: bootChecks > 1 })
    case '/espframe/api/v1/identity':
      return json({ name: snapshot.values.frame_name, friendly_name: snapshot.values.frame_name,
        hostname: snapshot.values.frame_name.toLowerCase().replaceAll(' ', '-'), ip_address: '192.168.0.12', restart_required: false })
    case '/favicon.ico':
      return res.writeHead(404).end()
    default:
      unexpected.push(req.url)
      return res.writeHead(404).end()
  }
})

await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
try {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-background-networking', '--dump-dom', '--virtual-time-budget=60000',
    'http://127.0.0.1:' + server.address().port + '/?developer=experimental',
  ], { signal: AbortSignal.timeout(18000) })
  let page = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  await new Promise((resolve, reject) => {
    chrome.on('error', reject)
    chrome.on('close', code => code === 0 ? resolve() : reject(new Error('Chrome exited with ' + code)))
  })
  assert.equal(page.match(/data-result="([^"]*)"/)?.[1], 'passed')
  assert.deepEqual(writes.slice(0, -2), [
    { wireguard_enabled: true },
    { wireguard_endpoint: 'vpn.example.test' },
    { wireguard_private_key: 'A'.repeat(43) + '=' },
    { interval: '30 seconds' },
    { interval: '43 seconds' },
    { portrait_pair_framing: 'Fill' },
    { portrait_pair_divider: 'White' },
    { photo_source: 'Memories' },
    { photo_source: 'Custom' },
    { album_ids: '22222222-2222-4222-8222-222222222222', album_labels: '["Family trip"]' },
    { album_matching: 'All selected albums' },
    { inclusion_matching: 'Match any enabled group' },
    { date_filter_enabled: true, date_filter_mode: 'Fixed Range', date_from: '0096-02-29', date_to: '', relative_amount: 1, relative_unit: 'Years' },
    { photo_source_mode: 'album', album_id: 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa' },
    { photo_source_mode: 'custom' },
    { immich_url: 'https://new-photos.test/api' },
    { api_key: 'replacement-test-key' },
    { brightness_day: 87 },
    { schedule_wake_timeout: 73 },
    { c6_auto_update: false },
    { update_frequency: 'Weekly' },
    { auto_update: false },
    { developer_features_enabled: true },
    { screen_rotation: '90', portrait_pairing: false },
    { developer_features_enabled: false, screen_rotation: '0' },
    { show_clock: false },
    { timezone: 'Asia/Tokyo (GMT+9)' },
    { ntp_server_2: 'time.example.test' },
  ].map(values => ({ api_version: 1, reset_epoch: 2, values })))
  assert.deepEqual(writes.slice(-2), [
    { api_version: 1, reset_epoch: 3, values: { immich_url: 'https://setup-photos.test/api', api_key: 'setup-key' } },
    { api_version: 1, reset_epoch: 3, values: { immich_url: 'https://backup-photos.test/api', brightness_day: 77,
      interval: '47 seconds', schedule_wake_timeout: 83, portrait_pair_framing: 'Fit', c6_auto_update: true } },
  ])
  assert.deepEqual(uploadedImages, ['invalid image', 'native image'])
  assert.ok(uploadedBootChecks >= 2)
  assert.deepEqual(deviceActions, ['reboot', 'reboot', 'customization'])
  assert.deepEqual(logPositions, [0, 1, 1, 2, 3, 0])
  assert.deepEqual(releaseRequests, ['', '', 'v0.1.0-preview.10', 'v0.1.0-preview.8'])
  assert.deepEqual(wifiRequests, ['check', 'install'])
  assert.equal(restartsObserved, 1)
  assert.ok(unchangedReads > 0, 'idle polling should receive empty 304 replies')
  assert.ok(bootChecks > 2)
  assert.deepEqual(unexpected, [])
  console.log('Shared admin verified photo sources, exact playback and display settings, firmware uploads, backups, logs and resets')
} finally {
  server.close()
}
