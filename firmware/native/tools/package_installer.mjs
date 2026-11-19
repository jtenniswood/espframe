import { createHash } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { build } from 'esbuild'

const [binaryRoot, destination] = process.argv.slice(2)
if (!binaryRoot || !destination) {
  console.error('Usage: node firmware/native/tools/package_installer.mjs BINARY_ROOT NEW_OUTPUT_DIRECTORY')
  process.exit(2)
}

const source = fileURLToPath(new URL('../installer/', import.meta.url))
const output = resolve(destination)
const boards = ['v1', 'v2', 'v3']
const factoryOffset = 0x10000
const settingsOffset = 0x410000
const appSize = settingsOffset - factoryOffset

function matchesBoard(image, board) {
  if (image.length < 288 || image[0] !== 0xe9 || image.readUInt16LE(12) !== 0x12 || image[23] !== 1 ||
      image.readUInt32LE(32) !== 0xabcd5432) return false
  // ESP-IDF places the project name 48 bytes into the descriptor, which
  // follows the 24-byte image header and 8-byte first segment header.
  return image.subarray(80, 112).toString('ascii').split('\0', 1)[0] === `espframe_native_${board}`
}

const images = await Promise.all(boards.map(async board => {
  const [factory, ota] = await Promise.all([
    readFile(join(binaryRoot, board, 'merged-binary.bin')),
    readFile(join(binaryRoot, board, `espframe_native_${board}.bin`)),
  ])
  if (factory.length > settingsOffset || !matchesBoard(factory.subarray(factoryOffset), board))
    throw new Error(`Invalid or oversized ${board} factory binary`)
  if (ota.length > appSize || !matchesBoard(ota, board))
    throw new Error(`Invalid or oversized ${board} update binary`)
  if (!factory.subarray(factoryOffset).equals(ota))
    throw new Error(`The ${board} factory and update images must come from the same build`)
  return { board, factory, ota }
}))
await mkdir(output)
const hashes = []
for (const { board, factory, ota } of images) {
  await writeFile(join(output, `${board}.bin`), factory)
  await writeFile(join(output, `${board}-ota.bin`), ota)
  hashes.push(`${createHash('sha256').update(ota).digest('hex')}  ${board}-ota.bin`)
  const manifest = {
    name: `Native Espframe ${board.toUpperCase()}`,
    version: 'prototype',
    new_install_prompt_erase: true,
    builds: [{ chipFamily: 'ESP32-P4', parts: [{ path: `${board}.bin`, offset: 0 }] }],
  }
  await writeFile(join(output, `${board}.json`), JSON.stringify(manifest, null, 2) + '\n')
}
await writeFile(join(output, 'SHA256SUMS'), hashes.join('\n') + '\n')

await build({
  entryPoints: [join(source, 'setup.js')],
  outfile: join(output, 'setup.js'),
  format: 'esm',
  target: 'chrome112',
  bundle: true,
  minify: true,
})
const clockGenerator = fileURLToPath(new URL('./generate_clock_zones.py', import.meta.url))
const clockOptions = execFileSync('python3', [clockGenerator, '--options'], { encoding: 'utf8' })
const html = await readFile(join(source, 'index.html'), 'utf8')
await Promise.all([
  writeFile(join(output, 'index.html'), html.replace('<!-- CLOCK_TIMEZONES -->', clockOptions.trim())),
  copyFile(join(source, 'setup.css'), join(output, 'setup.css')),
])
