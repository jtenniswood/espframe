import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const web = '../main/web/'
const load = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, bundle, cards, css] = await Promise.all([
  load(web + 'admin_preview.html'),
  load('../../../docs/public/webserver/app.js'),
  load(web + 'admin_cards.js'),
  load('../../../docs/public/webserver/style.css'),
])

const capabilities = '/espframe/api/v1/capabilities'
const configuration = '/espframe/api/v1/configuration'
const releasePath = '/api/firmware/releases'
const writes = []
const releaseRequests = []
const unexpected = []
let restartPending = false
let restartsObserved = 0
let bootChecks = 0
let release = { current: 'v0.1.0-preview.9', versions: [], status: 'idle', available: true }
const snapshot = {
  api_version: 1, reset_epoch: 2, api_key_configured: true,
  values: {
    immich_url: 'https://photos.test/api',
    conn_timeout: '10 minutes',
    interval: '15 seconds',
    photo_orientation: 'Any', display_mode: 'Fit', transition: 'Cross Fade', transition_speed: 'Fast',
    screen_rotation: '0',
    brightness_day: 90,
    brightness_night: 75,
    auto_update: true,
    update_frequency: 'Daily',
    frame_name: 'Living Room',
  },
  unavailable: ['wireguard_enabled', 'warm_tone_override'],
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
    await until(() => card('Frequency') && card('Photo Display') && card('Connection'), 'Immich cards')
    if (tab('Immich').getAttribute('aria-selected') !== 'true') throw new Error('Immich did not open first')
    if (tab('Logs') || card('WireGuard VPN') || card('Screen Tone')) throw new Error('Unavailable controls appeared')
    const select = openCard('Frequency').querySelector('select')
    if (select.value !== '15 seconds') throw new Error('Native configuration was not loaded')
    change(select, '30 seconds')
    await until(async () => {
      const response = await fetch('/espframe/api/v1/configuration')
      return (await response.json()).values.interval === '30 seconds'
    }, 'the interval save')
    const connection = openCard('Connection')
    const url = connection.querySelector('input[type="url"]')
    change(url, 'new-photos.test/api/')
    await until(() => connection.querySelector('#conn-status').textContent.trim() === 'URL saved', 'URL save and reconnect')
    if (url.value !== 'https://new-photos.test/api') throw new Error('Immich URL was not normalized')
    button(connection, 'Change').click()
    connection.querySelector('.input-group input').value = 'replacement-test-key'
    button(connection, 'Save').click()
    await until(() => connection.querySelector('.key-mask'), 'API key confirmation')
    if (connection.querySelector('.input-group input')) throw new Error('Saved secret remained in the form')
    tab('Device').click()
    if (tab('Device').getAttribute('aria-selected') !== 'true' ||
        !document.querySelector('#sp-settings.active') || !card('Rotation') || !card('Screen Brightness'))
      throw new Error('Device tab did not use the shared shell')
    const brightness = openCard('Screen Brightness').querySelector('input[type="range"]')
    if (brightness.value !== '90') throw new Error('Brightness preference was not loaded')
    change(brightness, '85')

    const firmware = openCard('Firmware')
    const disclosures = [...firmware.querySelectorAll('.inline-disclosure-button')]
    const disclosure = name => disclosures.find(el => el.firstChild.textContent === name)
    if (disclosure('WiFi firmware')) throw new Error('Unsupported WiFi updater appeared')
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
    await until(async () => {
      const response = await fetch('/espframe/api/v1/configuration')
      const { values } = await response.json()
      return values.brightness_day === 85 && values.auto_update === false && values.update_frequency === 'Weekly'
    }, 'brightness and update preference saves')
    if (!document.querySelector('a.sp-tab-docs')) throw new Error('Shared Docs link is missing')
    document.body.dataset.result = 'passed'
  }
  verify().catch(error => { document.body.dataset.result = error.message })
`
const server = createServer(async (req, res) => {
  const send = (body, type) => res.writeHead(200, { 'content-type': type }).end(body)
  const json = body => send(JSON.stringify(body), 'application/json')
  switch (req.url) {
    case '/admin':
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
        contract_version: 2, api_version: 1, configuration_path: configuration, update_mode: 'atomic',
        configuration_available: true, configuration_read: true, configuration_write: true, legacy_entity_api: false,
      })
    case configuration:
      if (req.method === 'POST') {
        const chunks = []
        for await (const chunk of req) chunks.push(chunk)
        const update = JSON.parse(new URLSearchParams(Buffer.concat(chunks).toString()).get('configuration'))
        writes.push(update)
        const { api_key, ...values } = update.values
        Object.assign(snapshot.values, values)
        if (api_key) snapshot.api_key_configured = true
        restartPending = !!(api_key || values.immich_url)
        return json({ api_version: 1, status: 'accepted', restart_required: restartPending })
      }
      if (restartPending) {
        restartPending = false
        ++restartsObserved
        return res.writeHead(503).end()
      }
      return json(snapshot)
    case releasePath:
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
    case '/api/firmware':
      ++bootChecks
      return json({ boot_verified: bootChecks > 1 })
    case '/espframe/api/v1/identity':
      return json({ name: 'Living Room', friendly_name: 'Living Room', hostname: 'living-room', ip_address: '192.168.0.12', restart_required: false })
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
    '--headless', '--no-sandbox', '--disable-background-networking', '--dump-dom', '--virtual-time-budget=20000',
    'http://127.0.0.1:' + server.address().port + '/admin',
  ], { signal: AbortSignal.timeout(18000) })
  let page = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  await new Promise((resolve, reject) => {
    chrome.on('error', reject)
    chrome.on('close', code => code === 0 ? resolve() : reject(new Error('Chrome exited with ' + code)))
  })
  assert.equal(page.match(/data-result="([^"]*)"/)?.[1], 'passed')
  assert.deepEqual(writes, [
    { interval: '30 seconds' },
    { immich_url: 'https://new-photos.test/api' },
    { api_key: 'replacement-test-key' },
    { brightness_day: 85 },
    { update_frequency: 'Weekly' },
    { auto_update: false },
  ].map(values => ({ api_version: 1, reset_epoch: 2, values })))
  assert.deepEqual(releaseRequests, ['', '', 'v0.1.0-preview.10', 'v0.1.0-preview.8'])
  assert.equal(restartsObserved, 2)
  assert.ok(bootChecks > 2)
  assert.deepEqual(unexpected, [])
  console.log('Shared admin saved native connection, brightness and update preferences and verified a mock firmware restart')
} finally {
  server.close()
}
