import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'

const installer = process.argv[2]
assert.ok(installer, 'Pass the directory of the packaged native installer')
const html = await readFile(join(installer, 'Espframe Native Installer.html'), 'utf8')
const firmware = await Promise.all(['v1', 'v2', 'v3'].map(async board => {
  const bytes = await readFile(join(installer, `${board}.bin`))
  return [board, createHash('sha256').update(bytes).digest('hex')]
}))
const verify = `
  window.addEventListener('DOMContentLoaded', async () => {
    const check = (condition, message) => { if (!condition) throw new Error(message) }
    let result = 'passed'
    try {
      const revision = document.querySelector('#revision')
      const install = document.querySelector('esp-web-install-button')
      check(location.protocol === 'file:' && isSecureContext, 'The installer did not load as a secure local file')
      check(install.hasAttribute('install-supported') && Array.isArray(await navigator.serial.getPorts()),
        'Web Serial is unavailable in the local installer')
      check(install.hidden && !revision.disabled, 'Firmware selection did not initialize')
      for (const [board, sha256] of ${JSON.stringify(firmware)}) {
        revision.value = board
        revision.dispatchEvent(new Event('change'))
        check(!install.hidden && install.manifest.startsWith('blob:'), 'The install button did not select an embedded manifest')
        const manifest = await (await fetch(install.manifest)).json()
        const [build] = manifest.builds
        const [part] = build.parts
        check(manifest.name === 'Native Espframe ' + board.toUpperCase() && manifest.new_install_prompt_erase &&
          build.chipFamily === 'ESP32-P4' && build.parts.length === 1 && part.offset === 0, 'Unexpected firmware manifest')
        const bytes = await (await fetch(new URL(part.path, location.href))).arrayBuffer()
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))
        const hash = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('')
        check(hash === sha256, 'Wrong embedded image for ' + board)
      }
      const find = document.querySelector('#find-frame')
      check(!find.disabled &&
        !document.querySelector('#settings button[type="submit"]').disabled, 'USB setup controls are unavailable')
      Object.defineProperty(navigator, 'serial', { value: {
        async requestPort() {
          let output
          return {
            readable: new ReadableStream({ start(controller) { output = controller } }),
            writable: new WritableStream({ write(bytes) {
              const command = new TextDecoder().decode(bytes).trim().split(' ')
              check(command.length === 4 && command[2] === 'STATUS', 'Unexpected USB request from the local installer')
              output.enqueue(new TextEncoder().encode('ESPFRAME_SETUP 1 CONNECTED ' + command[3] + ' 192.168.50.10 2\\n'))
            } }),
            async open() {},
            async close() {},
          }
        },
      } })
      const status = document.querySelector('#network-status')
      await new Promise(resolve => {
        const observer = new MutationObserver(() => {
          if (find.disabled) return
          observer.disconnect()
          resolve()
        })
        observer.observe(status, { childList: true, subtree: true })
        find.click()
      })
      check(status.querySelector('a')?.href === 'http://192.168.50.10/', 'Local USB setup could not open the frame settings')
    } catch (error) {
      result = error.message
    } finally {
      console.info('ESPFRAME_STANDALONE_RESULT ' + encodeURIComponent(result))
    }
  })
`

const temp = await mkdtemp(join(tmpdir(), 'espframe-standalone-installer-'))
try {
  const page = join(temp, 'Espframe Native Installer.html')
  await writeFile(page, html.replace('</head>', () => `<script>${verify}</script></head>`))
  const chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
    '--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking', '--host-resolver-rules=MAP * ~NOTFOUND',
    '--enable-logging=stderr', `--user-data-dir=${join(temp, 'profile')}`, pathToFileURL(page).href,
  ], { signal: AbortSignal.timeout(20_000), stdio: ['ignore', 'ignore', 'pipe'] })
  const stopped = new Promise(resolve => chrome.once('close', resolve))
  try {
    const result = await new Promise((resolve, reject) => {
      let log = ''
      chrome.stderr.setEncoding('utf8')
      chrome.stderr.on('data', text => {
        log += text
        const encoded = /ESPFRAME_STANDALONE_RESULT ([^"\s]+)"/.exec(log)?.[1]
        if (encoded) resolve(decodeURIComponent(encoded))
      })
      chrome.once('error', reject)
      chrome.once('close', code => reject(new Error(`Chromium stopped before the installer reported a result (${code}).\n${log}`)))
    })
    assert.equal(result, 'passed')
    console.log('The standalone installer opened from disk and loaded each factory image through its ESP Web Tools manifest')
  } finally {
    chrome.kill()
    await stopped
  }
} finally {
  await rm(temp, { recursive: true, force: true })
}
