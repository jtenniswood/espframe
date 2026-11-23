import { createHash } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { build } from 'esbuild'

const [binaryRoot, destination, previewVersion = ''] = process.argv.slice(2)
if (!binaryRoot || !destination) {
  console.error('Usage: node firmware/native/tools/package_installer.mjs BINARY_ROOT NEW_OUTPUT_DIRECTORY [PREVIEW_VERSION]')
  process.exit(2)
}
if (previewVersion && (!/^v[0-9]+\.[0-9]+\.[0-9]+-preview\.[0-9]+$/.test(previewVersion) || previewVersion.length > 31)) {
  throw new Error('Expected a preview version such as v0.1.0-preview.1, at most 31 bytes')
}

const source = fileURLToPath(new URL('../installer/', import.meta.url))
const output = resolve(destination)
const wireguardLicense = await readFile(join(source, 'LICENSE-WireGuard.txt'), 'utf8')
const boards = ['v1', 'v2', 'v3']
const partitionTableOffset = 0x8000
const partitionTableSize = 0xc00
const partitionEntrySize = 32
const partitionTypes = new Map([
  ['app/factory', [0, 0]], ['app/ota_0', [0, 0x10]], ['app/ota_1', [0, 0x11]],
  ['data/ota', [1, 0]], ['data/phy', [1, 1]], ['data/nvs', [1, 2]],
])

function partitionBytes(value) {
  if (/^0x[\da-f]+$/i.test(value)) return Number(value)
  const size = /^(\d+)([KM]?)$/i.exec(value)
  return size ? Number(size[1]) * ({ K: 1024, M: 1024 * 1024 }[size[2].toUpperCase()] ?? 1) : NaN
}

async function readPartitions(name) {
  const csv = await readFile(new URL(`../${name}`, import.meta.url), 'utf8')
  const partitions = new Map()
  for (const line of csv.split(/\r?\n/)) {
    const row = line.split('#', 1)[0].trim()
    if (!row) continue
    const fields = row.split(',').map(field => field.trim())
    const [label, type, subtype, start, length, flag = ''] = fields
    const codes = partitionTypes.get(`${type}/${subtype}`)
    const offset = partitionBytes(start)
    const size = partitionBytes(length)
    if (fields.length < 5 || fields.length > 6 || !/^[a-z0-9_]{1,16}$/.test(label) || partitions.has(label) ||
        !codes || !['', 'readonly'].includes(flag) || !Number.isSafeInteger(offset) || !Number.isSafeInteger(size) ||
        offset < 0 || offset % 4096 !== 0 || size <= 0 || size % 4096 !== 0 || offset + size > 0x1000000)
      throw new Error(`Invalid partition in ${name}: ${row}`)
    partitions.set(label, { codes, offset, size, flags: flag === 'readonly' ? 2 : 0 })
  }
  return partitions
}

function encodePartitionTable(partitions) {
  const checksumOffset = partitions.size * partitionEntrySize
  if (checksumOffset + partitionEntrySize > partitionTableSize)
    throw new Error('Native partition table exceeds its reserved space')
  const table = Buffer.alloc(partitionTableSize, 0xff)
  let offset = 0
  for (const [label, partition] of partitions) {
    table.fill(0, offset, offset + partitionEntrySize)
    table.writeUInt16LE(0x50aa, offset)
    table.set(partition.codes, offset + 2)
    table.writeUInt32LE(partition.offset, offset + 4)
    table.writeUInt32LE(partition.size, offset + 8)
    table.write(label, offset + 12, 'ascii')
    table.writeUInt32LE(partition.flags, offset + 28)
    offset += partitionEntrySize
  }
  table.writeUInt16LE(0xebeb, checksumOffset)
  createHash('md5').update(table.subarray(0, checksumOffset)).digest().copy(table, checksumOffset + 16)
  return table
}

const [partitions, stockPartitions] = await Promise.all([
  readPartitions('partitions.csv'),
  readPartitions('partitions.esphome-v1.csv'),
])
const nativeFactory = partitions.get('factory')
const nativeSettings = partitions.get('native_config')
const stockPreferences = partitions.get('stock_prefs')
const stockNvs = stockPartitions.get('nvs')
if (!nativeFactory || !nativeSettings || !stockPreferences || !stockNvs ||
    stockPreferences.offset !== stockNvs.offset || stockPreferences.size !== stockNvs.size ||
    stockPreferences.codes.join() !== '1,2' || stockPreferences.flags !== 2 ||
    nativeSettings.offset < nativeFactory.offset + nativeFactory.size ||
    nativeSettings.offset + nativeSettings.size > stockPreferences.offset)
  throw new Error('Native flash layout must preserve the stock settings partition as read-only')
const expectedTable = encodePartitionTable(partitions)

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
  if (!factory.subarray(partitionTableOffset, partitionTableOffset + partitionTableSize).equals(expectedTable))
    throw new Error(`Invalid ${board} factory partition table. Rebuild the native firmware and package matching images.`)
  if (factory.length > nativeFactory.offset + nativeFactory.size || !matchesBoard(factory.subarray(nativeFactory.offset), board))
    throw new Error(`Invalid or oversized ${board} factory binary`)
  if (ota.length > nativeFactory.size || !matchesBoard(ota, board))
    throw new Error(`Invalid or oversized ${board} update binary`)
  if (!factory.subarray(nativeFactory.offset).equals(ota))
    throw new Error(`The ${board} factory and update images must come from the same build`)
  // ESP-IDF writes the 32-byte project version before the project name.
  const version = ota.toString('ascii', 48, 80).split('\0', 1)[0]
  if (previewVersion && version !== previewVersion)
    throw new Error(`Expected ${board} firmware version ${previewVersion}, found ${version}`)
  return { board, factory, ota }
}))
await mkdir(output)
const hashes = []
const embedded = {}
for (const { board, factory, ota } of images) {
  await writeFile(join(output, `${board}.bin`), factory)
  await writeFile(join(output, `${board}-ota.bin`), ota)
  hashes.push(`${createHash('sha256').update(factory).digest('hex')}  ${board}.bin`,
              `${createHash('sha256').update(ota).digest('hex')}  ${board}-ota.bin`)
  const manifest = {
    name: `Native Espframe ${board.toUpperCase()}`,
    version: previewVersion || 'prototype',
    new_install_prompt_erase: true,
    builds: [{ chipFamily: 'ESP32-P4', parts: [{ path: `${board}.bin`, offset: 0 }] }],
  }
  await writeFile(join(output, `${board}.json`), JSON.stringify(manifest, null, 2) + '\n')
  embedded[board] = { manifest, image: factory.toString('base64') }
}
await writeFile(join(output, 'SHA256SUMS'), hashes.join('\n') + '\n')

const { outputFiles: [script] } = await build({
  entryPoints: [join(source, 'setup.js')],
  outfile: join(output, 'setup.js'),
  format: 'esm',
  target: 'chrome112',
  bundle: true,
  minify: true,
  write: false,
})
const clockGenerator = fileURLToPath(new URL('./generate_clock_zones.py', import.meta.url))
const clockOptions = execFileSync('python3', [clockGenerator, '--options'], { encoding: 'utf8' })
const [template, css] = await Promise.all([
  readFile(join(source, 'index.html'), 'utf8'),
  readFile(join(source, 'setup.css'), 'utf8'),
])
const html = template.replace('<!-- CLOCK_TIMEZONES -->', () => clockOptions.trim())
  .replace('<!-- NATIVE_PREVIEW_VERSION -->', previewVersion ? `<p>Native Preview ${previewVersion}</p>` : '')
// Keep the hosted bundle and provide a self-contained copy for file://, where
// Chromium's module loader and fetch cannot read neighboring local assets.
const standalone = html
  .replace('<link rel="stylesheet" href="setup.css">', () => `<style>${css}</style>`)
  .replace('<script type="module" src="setup.js"></script>', () =>
    `<script id="embedded-firmware" type="application/json">${JSON.stringify(embedded)}</script>\n` +
    `  <script type="module">${script.text}</script>`) +
    `\n<!-- WireGuard library license\n${wireguardLicense}\n-->\n`
await Promise.all([
  writeFile(join(output, 'index.html'), html),
  writeFile(join(output, 'setup.css'), css),
  writeFile(join(output, 'setup.js'), script.contents),
  writeFile(join(output, 'Espframe Native Installer.html'), standalone),
  writeFile(join(output, 'LICENSE-WireGuard.txt'), wireguardLicense),
])
