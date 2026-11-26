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
    check(field('conn_timeout').value === '3 minutes', 'Connection timeout was replaced')
    check(field('device_language').value === 'fr', 'Saved photo date language was replaced')
    check(!field('warm_tone_override').checked, 'Warm tone override did not start off')
    check(field('transition').value === 'Cross Fade' && field('transition_speed').value === 'Fast',
      'Saved slideshow transition was lost')
    check(field('brightness_day').value === '74', 'Daytime brightness was replaced')
    check(field('photo_orientation').value === 'Portrait Only', 'Saved photo orientation was lost')
    check(field('frame_name').value === 'Old frame' && document.title.includes('Old frame'), 'Saved frame name was lost')
    check(!field('brightness_night') && !field('albums_enabled'), 'Unsupported settings were shown')
    check(field('api_key').type === 'password' && !field('api_key').value, 'The key was shown')
    check(field('prepared_slide_url').value === 'https://prepare.test', 'The service URL was lost')
    check(field('prepared_slide_token').type === 'password' && !field('prepared_slide_token').value, 'Service token was shown')
    check(field('portrait_pairing').checked && !field('portrait_pairs_only').disabled,
      'Paired-portrait setting is unavailable while pairing is on')
    check(field('portrait_pairing_range').value === 'Within 2 Days' && !field('portrait_pairing_range').disabled,
      'Pairing range was not shown')
    check(field('portrait_pair_framing').value === 'Follow photo framing' && !field('portrait_pair_framing').disabled,
      'Portrait pair framing was not shown')
    check(save.disabled, 'A clean form allows saving')
    check(getComputedStyle(document.body).getPropertyValue('--bg').trim(), 'Shared styles did not load')
    check(getComputedStyle(save.parentElement).position === 'sticky', 'Save controls are not kept in view')
    check(save.getBoundingClientRect().bottom <= window.innerHeight, 'Save is initially off screen')
    check(document.querySelector('#native-frame-status').hidden, 'Older firmware showed an unsupported status panel')
    check(!document.querySelector('a[href="/settings"]'), 'Obsolete device-tools link is still visible')
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
    edit('frame_name', 'é'.repeat(61))
    check(!field('frame_name').validity.valid, 'A name over the byte limit was accepted')
    edit('frame_name', '  Salon été  ')
    edit('api_key', 'replacement+value&1')
    edit('prepared_slide_url', 'https://new-prepare.test')
    check(field('prepared_slide_token').required, 'Changing the service URL did not require a new token')
    edit('prepared_slide_token', 'service-token-for-browser-regression-test')
    edit('portrait_pairing_range', 'Within 1 Day')
    edit('portrait_pair_framing', 'Fill')
    edit('photo_orientation', 'Landscape Only')
    edit('transition', 'Fade to Black')
    edit('transition_speed', 'Slow')
    field('portrait_pairs_only').checked = true
    field('portrait_pairs_only').dispatchEvent(new Event('change', {bubbles: true}))
    field('portrait_pairing').checked = false
    field('portrait_pairing').dispatchEvent(new Event('change', {bubbles: true}))
    check(field('portrait_pairs_only').checked && field('portrait_pairs_only').disabled,
      'Turning off pairing lost the saved paired-portrait setting')
    check(field('portrait_pairing_range').disabled, 'Pairing range is editable while pairing is off')
    check(field('portrait_pair_framing').disabled, 'Pair framing is editable while pairing is off')
    form.requestSubmit()
    const reconnect = await waitFor(() => form.querySelector('a[href*="salon-t-129a.local"]'))
    check(new URL(reconnect.href).hostname === 'salon-t-129a.local', 'Wrong reconnect hostname')
    const reloaded = await waitFor(() => {
      const next = document.querySelector('#native-settings')
      return next !== form && next
    })
    check(reloaded.elements.namedItem('brightness_day').value === '60', 'New brightness was not read back')
    check(reloaded.elements.namedItem('frame_name').value === 'Salon été' && document.title.includes('Salon été'),
      'New frame name was not read back after restart')
    check(reloaded.elements.namedItem('interval').value === '30 seconds', 'New interval was not read back')
    check(reloaded.elements.namedItem('transition').value === 'Fade to Black' &&
      reloaded.elements.namedItem('transition_speed').value === 'Slow', 'New slideshow transition was not read back')
    check(reloaded.elements.namedItem('photo_orientation').value === 'Landscape Only', 'Photo orientation was not read back')
    check(!reloaded.elements.namedItem('api_key').value, 'The replacement key remained in the form')
    check(reloaded.elements.namedItem('prepared_slide_url').value === 'https://new-prepare.test', 'The new service URL was not read back')
    check(!reloaded.elements.namedItem('prepared_slide_token').value, 'The service token remained in the form')
    const reloadedPairing = reloaded.elements.namedItem('portrait_pairing')
    const reloadedPairsOnly = reloaded.elements.namedItem('portrait_pairs_only')
    const reloadedRange = reloaded.elements.namedItem('portrait_pairing_range')
    const reloadedFraming = reloaded.elements.namedItem('portrait_pair_framing')
    check(!reloadedPairing.checked && reloadedPairsOnly.checked && reloadedPairsOnly.disabled,
      'Reconnection lost the inactive paired-portrait state')
    reloadedPairing.click()
    check(reloadedPairsOnly.checked && !reloadedPairsOnly.disabled, 'Turning pairing on did not restore the option')
    check(reloadedRange.value === 'Within 1 Day' && !reloadedRange.disabled, 'Turning pairing on did not restore its range')
    check(reloadedFraming.value === 'Fill' && !reloadedFraming.disabled, 'The selected pair framing was lost')
    reloadedFraming.value = 'Fit'
    reloadedFraming.dispatchEvent(new Event('change', {bubbles: true}))
    const reloadedTimeout = reloaded.elements.namedItem('conn_timeout')
    reloadedTimeout.value = '45 seconds'
    reloadedTimeout.dispatchEvent(new Event('change', {bubbles: true}))
    reloaded.elements.namedItem('warm_tone_override').click()
    const language = reloaded.elements.namedItem('device_language')
    language.value = 'he'
    language.dispatchEvent(new Event('change', {bubbles: true}))
    const preparationUrl = reloaded.elements.namedItem('prepared_slide_url')
    preparationUrl.value = ''
    preparationUrl.dispatchEvent(new Event('input', {bubbles: true}))
    reloaded.requestSubmit()
    const applied = await waitFor(() => {
      const next = document.querySelector('#native-settings')
      return next !== reloaded && next.textContent.includes('Settings saved.') && next
    })
    check(applied.elements.namedItem('portrait_pair_framing').value === 'Fit', 'Live framing did not reload')
    check(applied.elements.namedItem('portrait_pairing').checked, 'Live pairing did not reload')
    check(applied.elements.namedItem('conn_timeout').value === '45 seconds', 'Live connection timeout did not reload')
    check(applied.elements.namedItem('warm_tone_override').checked, 'Live warm tone override did not reload')
    check(applied.elements.namedItem('device_language').value === 'he', 'Live photo date language did not reload')
    check(!applied.elements.namedItem('prepared_slide_url') && !applied.elements.namedItem('prepared_slide_token') &&
      !applied.textContent.includes('Photo preparation service'), 'The preparation card remained after returning to direct Immich previews')
    check(applied.querySelector('button[type=submit]').disabled, 'The saved form still has pending edits')
    document.body.dataset.testResult = 'passed'
  }
  verify().catch(error => { document.body.dataset.testResult = error.message })
`

const settingsPath = '/espframe/api/v1/configuration'
const statusPath = '/espframe/api/v1/status'
const capabilities = {
  contract_version: 2, api_version: 1, configuration_path: settingsPath, update_mode: 'atomic',
  configuration_available: true, configuration_read: true, configuration_write: true, legacy_entity_api: false,
}
const snapshot = {
  api_version: 1, reset_epoch: 0, api_key_configured: true, unavailable: ['albums_enabled', 'brightness_night'],
  values: {
    immich_url: 'https://photos.test/api', interval: '25 seconds', display_mode: 'Fit', brightness_day: 74,
    conn_timeout: '3 minutes',
    device_language: 'fr',
    warm_tone_override: false,
    brightness_night: 48, screen_rotation: '180', show_clock: false, timezone: 'Europe/London (GMT+0)',
    portrait_pairing: true, portrait_pairs_only: false, portrait_pairing_range: 'Within 2 Days',
    portrait_pair_framing: 'Follow photo framing',
    photo_orientation: 'Portrait Only',
    transition: 'Cross Fade', transition_speed: 'Fast',
    frame_name: 'Old frame',
    prepared_slide_url: 'https://prepare.test', prepared_slide_token_configured: true,
  },
}
const assets = {
  '/': ['text/html', html.replace('</body>', '<script defer src="/verify.js"></script></body>')],
  '/espframe/app.js': ['text/javascript', app],
  '/espframe/style.css': ['text/css', style],
  '/espframe/native.css': ['text/css', nativeStyle],
  '/verify.js': ['text/javascript', testScript],
}
const statusTest = `
  const panel = document.querySelector('#native-frame-status')
  const field = name => panel.querySelector('[data-status-field="' + name + '"]').textContent
  const notice = () => panel.querySelector('[role=status]').textContent
  let tabHidden = false
  Object.defineProperty(document, 'hidden', {get: () => tabHidden})
  function visibility(hidden) {
    tabHidden = hidden
    document.dispatchEvent(new Event('visibilitychange'))
  }
  function waitFor(check) {
    return new Promise(resolve => {
      const observer = new MutationObserver(() => {
        if (!check()) return
        observer.disconnect()
        resolve()
      })
      observer.observe(panel, {attributes: true, subtree: true, childList: true})
      if (check()) { observer.disconnect(); resolve() }
    })
  }
  async function verify() {
    await waitFor(() => !panel.hidden && field('playback') === 'Waiting for setup')
    if (panel.scrollWidth > panel.clientWidth) throw new Error('The frame status overflows the mobile layout')
    visibility(true)
    await new Promise(resolve => setTimeout(resolve, 5500))
    if (field('playback') !== 'Waiting for setup') throw new Error('The hidden tab kept polling')
    visibility(false)
    await waitFor(() => field('immich') === 'Access rejected')
    if (!notice().includes('Check the Immich API key')) throw new Error('A key error was hidden by a filter notice')
    await waitFor(() => notice().includes('Waiting to reconnect'))
    if (!panel.querySelector('dl').hidden) throw new Error('A disconnected frame showed stale values')
    await waitFor(() => field('screen') === 'Asleep')
    if (field('playback') !== 'Paused' || field('source') !== 'On This Day' || notice())
      throw new Error('The recovered frame status is wrong')
    if (panel.querySelector('button')) throw new Error('Frame status offered a screen action')
    await waitFor(() => field('playback') === 'Showing photos')
    if (field('screen') !== 'On' || field('immich') !== 'Responding') throw new Error('The frame did not recover')
    visibility(true)
    document.body.dataset.statusTest = 'passed'
  }
  verify().catch(error => { document.body.dataset.statusTest = error.message })
`
assets['/status-test'] = ['text/html', html.replace('</body>', '<script defer src="/verify-status.js"></script></body>')]
assets['/verify-status.js'] = ['text/javascript', statusTest]

const requests = []
let reconnectFailures = 0
let disconnected = false
let withStatus = false
let statusRequests = 0
const server = createServer(async (request, response) => {
  const json = (status, value) => response.writeHead(status, { 'Content-Type': 'application/json' }).end(JSON.stringify(value))
  if (request.url in assets) {
    const [type, body] = assets[request.url]
    response.writeHead(200, {
      'Content-Type': type,
      'Content-Security-Policy': "default-src 'self'; frame-ancestors 'none'; form-action 'none'",
    }).end(body)
  } else if (request.url === '/espframe/api/v1/capabilities') {
    json(200, {
      ...capabilities,
      ...(withStatus ? { status_path: statusPath, screen_control_path: '/espframe/api/v1/screen' } : {}),
    })
  } else if (request.url === statusPath) {
    assert.equal(request.method, 'GET')
    assert.equal(request.headers['cache-control'], 'no-cache')
    statusRequests++
    const base = { api_version: 1, restarting: false, notice: 'none', screen_pending: false }
    if (statusRequests === 1) {
      json(200, { ...base, screen: 'starting', playback: 'setup', source: 'none', immich: 'not_configured' })
    } else if (statusRequests === 2) {
      json(200, { ...base, screen: 'awake', playback: 'loading', source: 'custom', immich: 'rejected', notice: 'filters_unavailable' })
    } else if (statusRequests === 3) {
      json(503, { error: 'restarting' })
    } else {
      const asleep = statusRequests === 4
      json(200, { ...base, screen: asleep ? 'asleep' : 'awake', playback: asleep ? 'paused' : 'showing',
        source: 'memories', immich: 'responding' })
    }
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
      const { api_key, prepared_slide_token, ...settings } = post.values
      Object.assign(snapshot.values, settings)
      if (settings.prepared_slide_url === '') snapshot.values.prepared_slide_token_configured = false
      disconnected = requests.length === 2
      json(200, { api_version: 1, status: 'accepted', updated: Object.keys(post.values).length,
        ...(disconnected ? { frame_hostname: 'salon-t-129a' } : { restart_required: false }) })
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

async function render(path, budget) {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking', '--window-size=375,690', '--dump-dom',
    `--virtual-time-budget=${budget}`, `http://127.0.0.1:${server.address().port}${path}`,
  ], { signal: AbortSignal.timeout(18_000) })
  let page = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  return { page, errors }
}

await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const { page, errors } = await render('/', 10000)
  assert.match(page, /data-test-result="passed"/, page.match(/data-test-result="[^"]*"/)?.[0] || errors)
  assert.doesNotMatch(page, /replacement\+value/)
  assert.doesNotMatch(page, /service-token-for-browser-regression-test/)
  assert.deepEqual(requests, [
    { api_version: 1, reset_epoch: 0, values: { interval: '30 seconds', brightness_day: 45 } },
    {
      api_version: 1, reset_epoch: 0,
      values: {
        api_key: 'replacement+value&1', interval: '30 seconds', brightness_day: 60,
        frame_name: 'Salon été',
        photo_orientation: 'Landscape Only',
        transition: 'Fade to Black', transition_speed: 'Slow',
        portrait_pairing: false, portrait_pairs_only: true, portrait_pairing_range: 'Within 1 Day',
        portrait_pair_framing: 'Fill',
        prepared_slide_url: 'https://new-prepare.test', prepared_slide_token: 'service-token-for-browser-regression-test',
      },
    },
    { api_version: 1, reset_epoch: 0, values: {
      portrait_pairing: true, portrait_pair_framing: 'Fit', conn_timeout: '45 seconds', warm_tone_override: true, device_language: 'he',
      prepared_slide_url: '',
    } },
  ])
  assert.equal(reconnectFailures, 1)
  assert.equal(statusRequests, 0)
  console.log('Shared settings recover after restart and refresh after applying photo framing live')
  withStatus = true
  const result = await render('/status-test', 32000)
  assert.match(result.page, /data-status-test="passed"/, result.page.match(/data-status-test="[^"]*"/)?.[0] || result.errors)
  assert.equal(statusRequests, 5)
  console.log('Frame status reports setup, key rejection, sleep and recovery, and pauses while hidden')
} finally {
  server.close()
}
