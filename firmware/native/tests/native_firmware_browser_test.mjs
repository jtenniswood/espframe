import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const load = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, js, css] = await Promise.all([
  load('../main/web/native_settings.html'),
  load('../main/web/shared_settings.js'),
  load('../../../docs/public/webserver/style.css'),
])
const configuration = '/espframe/api/v1/configuration'
const firmware = '/api/firmware/releases'
const browserTest = String.raw`
  async function waitFor(test) {
    for (let i = 0; i < 200 && !test(); ++i) await new Promise(resolve => setTimeout(resolve, 15))
    if (!test()) throw new Error('Timed out waiting for the firmware panel')
  }

  async function verify() {
    const button = label => [...document.querySelectorAll('.native-firmware button')].find(node => node.textContent === label)
    await waitFor(() => button('Check for Update') && !button('Check for Update').disabled)
    const panel = document.querySelector('.native-firmware')
    if (panel.querySelector('a[href="/settings#firmware"]')) throw new Error('Obsolete firmware upload link is still visible')
    if (!panel.textContent.includes('v0.1.0-preview.8')) throw new Error('Current version missing')
    button('Check for Update').click()
    await waitFor(() => !button('Install Update').hidden && !button('Install Update').disabled)
    const select = panel.querySelector('select')
    if (select.value !== 'v0.1.0-preview.7') throw new Error('Older version missing')
    const settings = document.querySelector('#native-settings')
    if (!settings.elements.auto_update.checked || settings.elements.update_frequency.value !== 'Daily')
      throw new Error('Automatic update preferences missing')
    settings.elements.auto_update.click()
    settings.elements.update_frequency.value = 'Weekly'
    settings.elements.update_frequency.dispatchEvent(new Event('change', { bubbles: true }))
    settings.requestSubmit()
    await waitFor(() => document.querySelector('#native-settings') !== settings)
    const saved = document.querySelector('#native-settings')
    if (saved.elements.auto_update.checked || saved.elements.update_frequency.value !== 'Weekly')
      throw new Error('Automatic update preferences were not saved')
    await waitFor(() => !button('Install Update').hidden && !button('Install Update').disabled)
    button('Install Update').click()
    await waitFor(() => document.querySelector('.native-firmware').textContent.includes('Firmware updated.'))
    document.body.dataset.result = 'passed'
  }
  verify().catch(error => { document.body.dataset.result = error.message })
`

let current = 'v0.1.0-preview.8'
let versions = []
const actions = []
const settings = { values: { auto_update: true, update_frequency: 'Daily' }, reset_epoch: 0,
  unavailable: ['api_key'], api_version: 1, api_key_configured: true }
const server = createServer(async (request, response) => {
  const json = body => response.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify(body))
  const data = async () => {
    const chunks = []
    for await (const chunk of request) chunks.push(chunk)
    return Buffer.concat(chunks).toString()
  }
  if (request.url === '/') {
    response.writeHead(200, { 'content-type': 'text/html' }).end(
      html.replace('</body>', '<script defer src="/verify.js"></script></body>'))
  } else if (request.url === '/verify.js') {
    response.writeHead(200, { 'content-type': 'text/javascript' }).end(browserTest)
  } else if (request.url === '/espframe/app.js') {
    response.writeHead(200, { 'content-type': 'text/javascript' }).end(js)
  } else if (request.url === '/espframe/style.css' || request.url === '/espframe/native.css') {
    response.writeHead(200, { 'content-type': 'text/css' }).end(css)
  } else if (request.url === '/espframe/api/v1/capabilities') {
    json({ contract_version: 2, api_version: 1, update_mode: 'atomic', configuration_path: configuration,
      configuration_available: true, configuration_read: true, configuration_write: true, legacy_entity_api: false })
  } else if (request.url === configuration) {
    if (request.method === 'POST') {
      const submitted = JSON.parse(new URLSearchParams(await data()).get('configuration'))
      actions.push(submitted.values)
      Object.assign(settings.values, submitted.values)
      json({ status: 'accepted', restart_required: false })
    } else {
      json(settings)
    }
  } else if (request.url === firmware) {
    if (request.method === 'POST') {
      const version = await data()
      actions.push(version)
      if (version) current = version
      else versions = ['v0.1.0-preview.10', 'v0.1.0-preview.8', 'v0.1.0-preview.7']
      response.writeHead(202).end()
    } else {
      json({ current, versions, available: true, status: 'ready' })
    }
  } else {
    response.writeHead(404).end()
  }
})

await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
try {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-background-networking', '--dump-dom', '--virtual-time-budget=15000',
    'http://127.0.0.1:' + server.address().port,
  ], { signal: AbortSignal.timeout(18_000) })
  let page = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  await new Promise((resolve, reject) => {
    chrome.on('error', reject)
    chrome.on('close', code => code === 0 ? resolve() : reject(new Error('Chrome exited with ' + code)))
  })
  assert.match(page, /data-result="passed"/, page.match(/data-result="[^"]*"/)?.[0])
  assert.deepEqual(actions, ['', { auto_update: false, update_frequency: 'Weekly' }, 'v0.1.0-preview.10'])
  console.log('Native firmware controls checked releases, saved auto-update choices and confirmed a mock update')
} finally {
  server.close()
}
