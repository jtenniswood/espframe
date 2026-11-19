import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const read = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, app, style, nativeStyle] = await Promise.all([
  read('../main/web/native_settings.html'),
  read('../main/web/shared_settings.js'),
  read('../../../docs/public/webserver/style.css'),
  read('../main/web/native_settings.css'),
])

const testScript = `
  function check(value, message) {
    if (!value) throw new Error(message)
  }
  function waitFor(read) {
    const existing = read()
    if (existing) return Promise.resolve(existing)
    return new Promise(resolve => {
      const observer = new MutationObserver(() => {
        const value = read()
        if (!value) return
        observer.disconnect()
        resolve(value)
      })
      observer.observe(document.body, {subtree: true, childList: true, characterData: true})
    })
  }
  async function verify() {
    const form = await waitFor(() => document.querySelector('#native-settings'))
    const field = name => form.elements.namedItem(name)
    const save = form.querySelector('button[type="submit"]')
    check(field('interval').value === '25 seconds', 'Custom interval was replaced')
    check(field('brightness_day').value === '74', 'Daytime brightness was replaced')
    check(!field('brightness_night') && !field('albums_enabled'), 'Unsupported settings were shown')
    check(field('api_key').type === 'password' && !field('api_key').value, 'The key was shown')
    check(save.disabled, 'A clean form allows saving')
    check(getComputedStyle(document.body).getPropertyValue('--bg').trim(), 'Shared styles did not load')
    check(getComputedStyle(save.parentElement).position === 'sticky', 'Save controls are not kept in view')
    check(save.getBoundingClientRect().bottom <= window.innerHeight, 'Save is initially off screen')
    check(document.querySelector('a[href="/settings"]'), 'Photo source and firmware tools are unreachable')
    const edit = (name, value) => {
      field(name).value = value
      field(name).dispatchEvent(new Event('input', {bubbles: true}))
    }
    edit('brightness_day', '45')
    check(!save.disabled, 'An edit did not enable saving')
    edit('brightness_day', '74')
    check(save.disabled, 'Reverting an edit did not disable saving')
    edit('brightness_day', '45')
    edit('interval', '30 seconds')
    form.requestSubmit()
    await waitFor(() => form.textContent.includes('Check the value for Daytime brightness (%).'))
    check(!save.disabled, 'The rejected save cannot be retried')
    check(field('interval').value === '30 seconds', 'A rejected save lost pending edits')
    edit('brightness_day', '60')
    edit('api_key', 'replacement+value&1')
    form.requestSubmit()
    const reloaded = await waitFor(() => {
      const next = document.querySelector('#native-settings')
      return next !== form && next
    })
    check(reloaded.elements.namedItem('brightness_day').value === '60', 'New brightness was not read back')
    check(reloaded.elements.namedItem('interval').value === '30 seconds', 'New interval was not read back')
    check(!reloaded.elements.namedItem('api_key').value, 'The replacement key remained in the form')
    document.body.dataset.testResult = 'passed'
  }
  verify().catch(error => { document.body.dataset.testResult = error.message })
`

const settingsPath = '/espframe/api/v1/configuration'
const capabilities = {
  contract_version: 2, api_version: 1, configuration_path: settingsPath, update_mode: 'atomic',
  configuration_available: true, configuration_read: true, configuration_write: true, legacy_entity_api: false,
}
const snapshot = {
  api_version: 1, reset_epoch: 0, api_key_configured: true, unavailable: ['albums_enabled', 'brightness_night'],
  values: {
    immich_url: 'https://photos.test/api', interval: '25 seconds', display_mode: 'Fit', brightness_day: 74,
    brightness_night: 48, screen_rotation: '180', show_clock: false, timezone: 'Europe/London (GMT+0)',
  },
}
const assets = {
  '/': ['text/html', html.replace('</body>', '<script defer src="/verify.js"></script></body>')],
  '/espframe/app.js': ['text/javascript', app],
  '/espframe/style.css': ['text/css', style],
  '/espframe/native.css': ['text/css', nativeStyle],
  '/verify.js': ['text/javascript', testScript],
}
const requests = []
let reconnectFailures = 0
let disconnected = false
const server = createServer(async (request, response) => {
  const json = (status, value) => response.writeHead(status, { 'Content-Type': 'application/json' }).end(JSON.stringify(value))
  if (request.url in assets) {
    const [type, body] = assets[request.url]
    response.writeHead(200, {
      'Content-Type': type,
      'Content-Security-Policy': "default-src 'self'; frame-ancestors 'none'; form-action 'none'",
    }).end(body)
  } else if (request.url === '/espframe/api/v1/capabilities') {
    json(200, capabilities)
  } else if (request.url === settingsPath && request.method === 'POST') {
    assert.equal(request.headers.origin, `http://127.0.0.1:${server.address().port}`)
    assert.equal(request.headers['content-type'], 'application/x-www-form-urlencoded')
    const chunks = []
    for await (const chunk of request) chunks.push(chunk)
    const post = JSON.parse(new URLSearchParams(Buffer.concat(chunks).toString()).get('configuration'))
    requests.push(post)
    if (requests.length === 1) {
      json(422, { error: 'invalid_value', field: 'brightness_day' })
    } else {
      snapshot.values.brightness_day = post.values.brightness_day
      snapshot.values.interval = post.values.interval
      disconnected = true
      json(200, { api_version: 1, status: 'accepted', updated: 3 })
    }
  } else if (request.url === settingsPath) {
    if (disconnected) {
      reconnectFailures++
      disconnected = false
      json(503, { error: 'restarting' })
    } else {
      json(200, snapshot)
    }
  } else {
    assert.equal(request.url, '/favicon.ico', 'The native page used an ESPHome-only route')
    response.writeHead(404).end()
  }
})

await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking', '--window-size=375,690', '--dump-dom',
    '--virtual-time-budget=10000', `http://127.0.0.1:${server.address().port}/`,
  ], { signal: AbortSignal.timeout(18_000) })
  let page = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  assert.match(page, /data-test-result="passed"/, page.match(/data-test-result="[^"]*"/)?.[0] || errors)
  assert.doesNotMatch(page, /replacement\+value/)
  assert.deepEqual(requests, [
    { api_version: 1, reset_epoch: 0, values: { interval: '30 seconds', brightness_day: 45 } },
    {
      api_version: 1, reset_epoch: 0,
      values: { api_key: 'replacement+value&1', interval: '30 seconds', brightness_day: 60 },
    },
  ])
  assert.equal(reconnectFailures, 1)
  console.log('Shared settings load on the native page, batch edits and recover after restart')
} finally {
  server.close()
}
