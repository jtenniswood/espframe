import assert from 'node:assert/strict'
import { spawn, spawnSync } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'

const browser = process.env.CHROME_BIN || 'google-chrome'
const read = path => readFile(new URL(path, import.meta.url), 'utf8')
const [html, script, css, sourceScript, backupScript] = await Promise.all([
  read('../main/web/settings.html'),
  read('../main/web/settings.js'),
  read('../installer/setup.css'),
  read('../installer/photo_source.mjs'),
  read('../installer/backup.mjs'),
])
const albumId = 'c48e2980-18ac-4d7d-876a-a636759a1aec'
const setup = {
  immich_url: 'https://photos.test/api', slide_seconds: 15, portrait_pairing: true,
  photo_source: 'favorites', album_id: '',
}
const fixture = html.replace('</body>', `<script>
  const form = document.querySelector('#settings')
  let downloaded
  let filename
  URL.createObjectURL = blob => { downloaded = blob; return 'blob:mock-backup' }
  HTMLAnchorElement.prototype.click = function () { filename = this.download }
  new MutationObserver(async (_, observer) => {
    if (form.hidden) return
    observer.disconnect()
    const source = form.elements.photo_source
    const album = form.elements.album_id
    if (source.value !== 'favorites' || !album.disabled) throw new Error('Incorrect initial source')
    document.querySelector('#download-backup').click()
    const backup = JSON.parse(await downloaded.text())
    if (!filename.startsWith('espframe-config-') || backup.version !== 3 || !backup.photos.favorites_enabled
      || backup.frequency.interval !== '15 seconds' || Object.keys(backup.connection).join() !== 'immich_url')
      throw new Error('Unexpected saved settings backup')
    source.value = 'album'
    source.dispatchEvent(new Event('change'))
    if (!album.required || album.disabled) throw new Error('Album selection did not enable the album field')
    album.value = 'https://photos.test/share/${albumId}'
    form.requestSubmit()
    if (album.validity.valid) throw new Error('A share link was accepted as an album')
    album.value = 'https://photos.test/immich/albums/${albumId.toUpperCase()}?from=library'
    album.dispatchEvent(new Event('input'))
    form.requestSubmit()
  }).observe(form, {attributes: true})
</script></body>`)
const pages = new Map([
  ['/', ['text/html', fixture]],
  ['/settings.js', ['text/javascript', script]],
  ['/photo_source.mjs', ['text/javascript', sourceScript]],
  ['/backup.mjs', ['text/javascript', backupScript]],
  ['/settings.css', ['text/css', css]],
  ['/api/playback', ['application/json', JSON.stringify(setup)]],
])
const updates = []
const server = createServer(async (request, response) => {
  if (request.method === 'POST' && request.url === '/api/playback') {
    let body = ''
    for await (const chunk of request) body += chunk
    updates.push(JSON.parse(body))
    response.setHeader('Content-Type', 'application/json')
    response.end('{"saved":true}')
  } else if (pages.has(request.url)) {
    const [contentType, body] = pages.get(request.url)
    response.setHeader('Content-Type', contentType)
    response.end(body)
  } else {
    response.writeHead(404).end()
  }
})

assert.equal(spawnSync(browser, ['--version']).status, 0, 'Set CHROME_BIN to a Chromium executable')
await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
try {
  const chrome = spawn(browser, [
    '--headless', '--no-sandbox', '--disable-gpu', '--dump-dom', '--virtual-time-budget=5000',
    `http://127.0.0.1:${server.address().port}/`,
  ], { signal: AbortSignal.timeout(15_000) })
  let page = ''
  let errors = ''
  chrome.stdout.on('data', chunk => { page += chunk })
  chrome.stderr.on('data', chunk => { errors += chunk })
  await new Promise((resolve, reject) => {
    chrome.once('error', reject)
    chrome.once('close', code => code === 0 ? resolve() : reject(new Error(errors)))
  })
  assert.match(page, /Settings saved\. The frame is restarting\./)
  assert.deepEqual(updates, [{ ...setup, photo_source: 'album', album_id: albumId, immich_key: '' }])
  console.log('Native photo source settings accept Immich album links and reject share links')
} finally {
  server.close()
}
