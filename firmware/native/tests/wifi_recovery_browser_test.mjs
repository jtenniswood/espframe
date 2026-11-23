import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

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
  const report = result => {
    observer.disconnect()
    return fetch('/test-result', { method: 'POST', body: result })
  }
  const observer = new MutationObserver(() => {
    try {
      if (attempts === 0 && !form.hidden) {
        attempts++
        submit('é'.repeat(17), 'too many bytes')
        if (!status.textContent.includes('up to 32 bytes')) throw new Error('Invalid byte length accepted')
        submit('Salon été', 'wrong')
      } else if (attempts === 1 && status.textContent.startsWith('Could not join')) {
        if (form.elements.password.value) throw new Error('Tested password was retained in the form')
        attempts++
        submit('Salon été', '')
      } else if (attempts === 2 && status.textContent.startsWith('Connected.')) {
        if (!status.textContent.includes('Connected. Rejoin your usual Wi-Fi. The frame’s address is 192.168.6.17')) {
          throw new Error('The recovery page did not report the frame address')
        }
        if (!status.textContent.includes('To set up Immich or enable web settings, connect by USB')) {
          throw new Error('The recovery page did not explain how to open setup')
        }
        if (!form.hidden) throw new Error('The form stayed visible after connecting')
        report('passed')
      }
    } catch (error) {
      report(error.message)
    }
  })
  observer.observe(status, { childList: true })
  observer.observe(form, { attributes: true, attributeFilter: ['hidden'] })
</script></html>`)

const requests = []
let pending = false
const { promise: testResult, resolve: completeTest } = Promise.withResolvers()
const server = createServer(async (request, response) => {
  const assets = {
    '/': ['text/html', fixture], '/recovery.js': ['text/javascript', script], '/settings.css': ['text/css', css],
  }
  if (request.url === '/test-result' && request.method === 'POST') {
    request.setEncoding('utf8')
    let result = ''
    for await (const chunk of request) result += chunk
    response.writeHead(204).end()
    completeTest(result)
  } else if (request.url in assets) {
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
    response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }).end(JSON.stringify(result))
  } else {
    response.writeHead(404).end()
  }
})

const profile = await mkdtemp(join(tmpdir(), 'espframe-recovery-browser-'))
try {
  await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking',
    `--user-data-dir=${profile}`, `http://127.0.0.1:${server.address().port}/`,
  ], { signal: AbortSignal.timeout(45_000), stdio: ['ignore', 'ignore', 'pipe'] })
  let errors = ''
  chrome.stderr.on('data', chunk => { errors += chunk })
  const stopped = new Promise(resolve => chrome.once('close', resolve))
  try {
    const browserFailure = new Promise((_, reject) => {
      chrome.once('error', error => reject(new Error(`Chromium error before Wi-Fi recovery reported a result (${requests.length} submissions).\n${errors}`, { cause: error })))
      chrome.once('close', code => reject(new Error(`Chromium stopped before Wi-Fi recovery reported a result (${code}).\n${errors}`)))
    })
    assert.equal(await Promise.race([testResult, browserFailure]), 'passed')
    const ssid = Buffer.from('Salon été')
    assert.deepEqual(requests, [Buffer.from([ssid.length, ...ssid, 5, ...Buffer.from('wrong')]),
      Buffer.from([ssid.length, ...ssid, 0])])
    console.log('Native Wi-Fi recovery handles encoded credentials, a rejected trial, and a successful retry')
  } finally {
    chrome.kill()
    await stopped
  }
} finally {
  server.close()
  await rm(profile, { recursive: true, force: true })
}
