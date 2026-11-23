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
const writes = []
const unexpected = []
const snapshot = {
  api_version: 1, reset_epoch: 2, api_key_configured: true,
  values: {
    immich_url: 'https://photos.test/api',
    interval: '15 seconds',
    photo_orientation: 'Any', display_mode: 'Fit', transition: 'Cross Fade', transition_speed: 'Fast',
    screen_rotation: '0',
    frame_name: 'Living Room',
  },
  unavailable: ['wireguard_enabled', 'warm_tone_override'],
}
const browserTest = String.raw`
  async function until(condition) {
    for (let i = 0; i < 120; ++i) {
      if (await condition()) return
      await new Promise(resolve => setTimeout(resolve, 25))
    }
    throw new Error('Timed out waiting for shared admin')
  }

  async function verify() {
    const tab = name => [...document.querySelectorAll('nav [role=tab]')].find(el => el.textContent === name)
    const card = name => [...document.querySelectorAll('.card-toggle')].find(el => el.textContent.trim() === name)
    await until(() => card('Frequency') && card('Photo Display'))
    if (tab('Immich').getAttribute('aria-selected') !== 'true') throw new Error('Immich did not open first')
    if (tab('Logs') || card('WireGuard VPN') || card('Screen Tone')) throw new Error('Unavailable controls appeared')
    if (card('Frequency').getAttribute('aria-expanded') !== 'true') card('Frequency').click()
    const select = card('Frequency').closest('.card').querySelector('select')
    if (select.value !== '15 seconds') throw new Error('Native configuration was not loaded')
    select.value = '30 seconds'
    select.dispatchEvent(new Event('change', { bubbles: true }))
    await until(async () => {
      const response = await fetch('/espframe/api/v1/configuration')
      return (await response.json()).values.interval === '30 seconds'
    })
    tab('Device').click()
    if (tab('Device').getAttribute('aria-selected') !== 'true' ||
        !document.querySelector('#sp-settings.active') || !card('Rotation'))
      throw new Error('Device tab did not use the shared shell')
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
        Object.assign(snapshot.values, update.values)
        return json({ api_version: 1, status: 'accepted', restart_required: false })
      }
      return json(snapshot)
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
    '--headless', '--no-sandbox', '--disable-background-networking', '--dump-dom', '--virtual-time-budget=12000',
    'http://127.0.0.1:' + server.address().port + '/admin',
  ], { signal: AbortSignal.timeout(18000) })
  let page = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  await new Promise((resolve, reject) => {
    chrome.on('error', reject)
    chrome.on('close', code => code === 0 ? resolve() : reject(new Error('Chrome exited with ' + code)))
  })
  assert.equal(page.match(/data-result="([^"]*)"/)?.[1], 'passed')
  assert.deepEqual(writes, [{ api_version: 1, reset_epoch: 2, values: { interval: '30 seconds' } }])
  assert.deepEqual(unexpected, [])
  console.log('Shared admin rendered Immich and Device cards and saved a native setting through the atomic API')
} finally {
  server.close()
}
