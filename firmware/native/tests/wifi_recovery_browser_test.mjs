import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const read = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, script, css] = await Promise.all([
  read('../main/web/recovery.html'), read('../main/web/recovery.js'), read('../installer/setup.css'),
])
const fixture = html.replace('</html>', `<script>
  const form = document.querySelector('#recovery')
  const status = document.querySelector('#status')
  let attempts = 0
  const submit = (name, password) => {
    form.elements.ssid.value = name
    form.elements.password.value = password
    form.requestSubmit()
  }
  new MutationObserver(() => {
    if (attempts === 0 && !form.hidden) {
      attempts++
      submit('é'.repeat(17), 'too many bytes')
      if (!status.textContent.includes('up to 32 bytes')) throw new Error('Invalid byte length accepted')
      submit('Salon été', 'wrong')
    } else if (attempts === 1 && status.textContent.startsWith('Could not join')) {
      if (form.elements.password.value) throw new Error('Tested password was retained in the form')
      attempts++
      submit('Salon été', '')
    }
  }).observe(status, { childList: true })
</script></html>`)

const requests = []
let pending = false
const server = createServer(async (request, response) => {
  const assets = {
    '/': ['text/html', fixture], '/recovery.js': ['text/javascript', script], '/settings.css': ['text/css', css],
  }
  if (request.url in assets) {
    const [type, body] = assets[request.url]
    response.writeHead(200, { 'Content-Type': type }).end(body)
  } else if (request.url === '/api/recovery' && request.method === 'POST') {
    assert.equal(request.headers.origin, `http://127.0.0.1:${server.address().port}`)
    assert.equal(request.headers['content-type'], 'application/octet-stream')
    const body = []
    for await (const chunk of request) body.push(chunk)
    requests.push(Buffer.concat(body))
    pending = true
    response.writeHead(202).end('Connecting to Wi-Fi.')
  } else if (request.url === '/api/recovery') {
    const result = pending ? { status: 'joining' } : requests.length === 0 ? { status: 'ready' }
      : requests.length === 1 ? { status: 'failed' } : { status: 'connected', ipv4: '192.168.6.17', web_settings: false }
    pending = false
    response.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify(result))
  } else {
    response.writeHead(404).end()
  }
})

await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking', '--dump-dom',
    '--virtual-time-budget=6000', `http://127.0.0.1:${server.address().port}/`,
  ], { signal: AbortSignal.timeout(18_000) })
  let page = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  const ssid = Buffer.from('Salon été')
  assert.deepEqual(requests, [Buffer.from([ssid.length, ...ssid, 5, ...Buffer.from('wrong')]),
    Buffer.from([ssid.length, ...ssid, 0])])
  assert.match(page, /Connected\. Rejoin your usual Wi-Fi\. The frame’s address is 192\.168\.6\.17/)
  assert.match(page, /To set up Immich or enable web settings, connect by USB/)
  assert.match(page, /<form id="recovery" hidden="">/)
  console.log('Native Wi-Fi recovery handles encoded credentials, a rejected trial, and a successful retry')
} finally {
  server.close()
}
