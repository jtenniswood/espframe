import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'

const [installer, previewVersion = ''] = process.argv.slice(2)
assert.ok(installer, 'Pass the directory of the packaged native installer')
const html = await readFile(join(installer, 'Espframe Native Installer.html'), 'utf8')
if (previewVersion) assert.ok(html.includes(`<p>Native Preview ${previewVersion}</p>`))
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
        check(manifest.name === 'Native Espframe ' + board.toUpperCase() &&
          manifest.version === ${JSON.stringify(previewVersion || 'prototype')} && manifest.new_install_prompt_erase &&
          build.chipFamily === 'ESP32-P4' && build.parts.length === 1 && part.offset === 0, 'Unexpected firmware manifest')
        const bytes = await (await fetch(new URL(part.path, location.href))).arrayBuffer()
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))
        const hash = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('')
        check(hash === sha256, 'Wrong embedded image for ' + board)
      }
      const find = document.querySelector('#find-frame')
      const settings = document.querySelector('#settings')
      const repair = document.querySelector('#repair-wifi')
      const wifi = document.querySelector('#wifi-repair')
      const connect = wifi.querySelector('button')
      check(!find.disabled && !settings.querySelector('button[type="submit"]').disabled && !repair.hidden &&
        !connect.disabled, 'USB setup controls are unavailable')
      const encoder = new TextEncoder()
      const decoder = new TextDecoder()
      const header = [...encoder.encode('IMPROV'), 1]
      const packets = []
      let portOpen = false
      let online = false
      let webState = 2
      let removedPassword = false
      let firmwareName = 'Unrelated Improv'
      let rejectsWifi = false
      const port = {
        async open({ baudRate }) {
          check(!portOpen && baudRate === 115200, 'Incorrect serial port state or baud rate')
          portOpen = true
          let output
          const packet = (type, payload) => {
            const frame = [...header, type, payload.length, ...payload]
            frame.push(frame.reduce((sum, byte) => sum + byte, 0) & 255, 10)
            output.enqueue(Uint8Array.from(frame))
          }
          this.readable = new ReadableStream({ start(controller) { output = controller } })
          this.writable = new WritableStream({ write(bytes) {
            if (header.every((byte, index) => bytes[index] === byte)) {
              check(bytes[7] === 3, 'Expected an Improv command')
              packet(2, [0])
              switch (bytes[9]) {
                case 2:
                  packet(1, [2])
                  break
                case 3: {
                  const fields = [firmwareName, 'prototype', 'ESP32-P4', 'Espframe'].flatMap(value => {
                    const data = encoder.encode(value)
                    return [data.length, ...data]
                  })
                  packet(4, [3, fields.length, ...fields])
                  break
                }
                case 1:
                  packets.push([...bytes.slice(11, -2)])
                  packet(1, [3])
                  if (rejectsWifi) {
                    packet(1, [2])
                    packet(2, [3])
                  } else {
                    online = true
                    packet(1, [4])
                    packet(4, [1, 0])
                  }
                  break
                default:
                  throw new Error('Unexpected Improv command')
              }
              return
            }
            const command = decoder.decode(bytes).trim().split(' ')
            if (command[2] === 'HELLO' || command[2] === 'SET') {
              if (command[2] === 'SET') {
                const fields = JSON.parse(command.slice(4).join(' '))
                check(Object.keys(fields).length === 1 && fields.web_password === '',
                  'Password removal sent unrelated settings')
                removedPassword = true
                webState = 2
              }
              const result = command[2] === 'HELLO' ? 'READY' : 'SAVED'
              output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + result + ' ' + command[3] + '\\n'))
              return
            }
            check(command.length === 4 && command[2] === 'STATUS', 'Wi-Fi repair sent a full settings request')
            const reply = online ? 'CONNECTED ' + command[3] + ' 192.168.50.10 ' + webState : 'OFFLINE ' + command[3]
            output.enqueue(encoder.encode('ESPFRAME_SETUP 1 ' + reply + '\\n'))
          } })
        },
        async close() {
          check(!this.readable.locked && !this.writable.locked, 'Serial streams are still locked')
          portOpen = false
        },
      }
      Object.defineProperty(navigator, 'serial', { value: { async requestPort() { return port } } })
      const status = document.querySelector('#network-status')
      const run = async button => {
        button.click()
        check(button.disabled && find.disabled && connect.disabled, 'Serial actions stayed enabled while the port was in use')
        const deadline = performance.now() + 6000
        while (button.disabled && performance.now() < deadline)
          await new Promise(resolve => setTimeout(resolve, 20))
        check(!button.disabled && !portOpen, 'The serial action did not finish and release the port')
      }
      await run(find)
      check(repair.open && status.textContent.includes('waiting for Wi-Fi'), 'Offline lookup did not show USB recovery')

      settings.elements.immich_key.value = 'preserved-immich-key'
      wifi.elements.ssid.value = 'Frame Café'
      wifi.elements.password.value = 'new-password'
      await run(connect)
      check(packets.length === 0 && status.textContent.includes('running Native Espframe'),
        'Wi-Fi repair accepted a different Improv device')

      firmwareName = 'Native Espframe V1'
      rejectsWifi = true
      await run(connect)
      check(packets.length === 1 && wifi.elements.password.value === 'new-password' &&
        /connect/i.test(status.textContent) && repair.open, 'A failed connection did not leave the repair form ready to retry')

      rejectsWifi = false
      await run(connect)
      check(status.querySelector('a')?.href === 'http://192.168.50.10/', 'Wi-Fi repair did not show the settings link')
      const ssid = encoder.encode('Frame Café')
      const password = encoder.encode('new-password')
      check(packets.length === 2 && packets[1].join() === [ssid.length, ...ssid, password.length, ...password].join(),
        'Wi-Fi repair sent incorrect credentials')
      check(!wifi.elements.ssid.value && !wifi.elements.password.value && !repair.open &&
        document.querySelector('#reuse-wifi').checked && settings.elements.immich_key.value === 'preserved-immich-key',
        'Wi-Fi repair did not clear the credentials, reuse Wi-Fi, or preserve the main setup form')
      webState = 0
      await run(find)
      check(status.querySelector('a')?.getAttribute('href') === '#web-admin-password' &&
        document.querySelector('#web-admin-password') === settings.elements.web_password,
        'A frame with its admin disabled did not link to password setup')
      status.querySelector('a').click()
      check(document.querySelector('#web-admin-access').open, 'The legacy password setup link did not open its field')
      await run(document.querySelector('#clear-web-password'))
      check(removedPassword && status.querySelector('a')?.href === 'http://192.168.50.10/' &&
        !status.textContent.includes('sign in'), 'Password removal did not show the direct settings link')
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
    console.log('The standalone installer loaded each factory image and recovered Wi-Fi over USB')
  } finally {
    chrome.kill()
    await stopped
  }
} finally {
  await rm(temp, { recursive: true, force: true })
}
