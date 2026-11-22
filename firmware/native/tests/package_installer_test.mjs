import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { copyFile, mkdir, mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'

const binaries = process.argv[2]
assert.ok(binaries, 'Pass the directory containing v1, v2 and v3 firmware builds')
const packager = fileURLToPath(new URL('../tools/package_installer.mjs', import.meta.url))
const original = await readFile(join(binaries, 'v1', 'merged-binary.bin'))
const partitionOffset = 0x8000
const entrySize = 32
const table = original.subarray(partitionOffset, partitionOffset + 0xc00)
let checksum = 0
let stock = -1
while (checksum + entrySize <= table.length && table.readUInt16LE(checksum) === 0x50aa) {
  if (table.toString('ascii', checksum + 12, checksum + 28).split('\0', 1)[0] === 'stock_prefs')
    stock = checksum
  checksum += entrySize
}
assert.ok(stock >= 0 && checksum + entrySize <= table.length && table.readUInt16LE(checksum) === 0xebeb,
  'The V1 fixture needs its current native partition table')

function writeChecksum(bytes, offset) {
  bytes.writeUInt16LE(0xebeb, offset)
  createHash('md5').update(bytes.subarray(0, offset)).digest().copy(bytes, offset + 16)
}

const temp = await mkdtemp(join(tmpdir(), 'espframe-package-layout-'))
try {
  await Promise.all(['v1', 'v2', 'v3'].map(async board => {
    await mkdir(join(temp, board))
    const names = [`espframe_native_${board}.bin`]
    if (board !== 'v1') names.push('merged-binary.bin')
    await Promise.all(names.map(name => copyFile(join(binaries, board, name), join(temp, board, name))))
  }))
  const cases = [
    ['missing stock settings partition', bytes => {
      bytes.copyWithin(stock, stock + entrySize, checksum)
      bytes.fill(0xff, checksum - entrySize)
      writeChecksum(bytes, checksum - entrySize)
    }],
    ['writable stock settings partition', bytes => {
      bytes.writeUInt32LE(0, stock + 28)
      writeChecksum(bytes, checksum)
    }],
    ['moved stock settings partition', bytes => {
      bytes.writeUInt32LE(bytes.readUInt32LE(stock + 4) - 4096, stock + 4)
      writeChecksum(bytes, checksum)
    }],
    ['corrupt checksum', bytes => { bytes[checksum + 16] ^= 1 }],
  ]
  const destination = join(temp, 'installer')
  for (const [description, mutate] of cases) {
    const factory = Buffer.from(original)
    mutate(factory.subarray(partitionOffset, partitionOffset + table.length))
    await writeFile(join(temp, 'v1', 'merged-binary.bin'), factory)
    const result = spawnSync(process.execPath, [packager, temp, destination], { encoding: 'utf8' })
    assert.equal(result.status, 1, description)
    assert.match(result.stderr, /Invalid v1 factory partition table/, description)
    await assert.rejects(stat(destination), { code: 'ENOENT' })
  }
  await writeFile(join(temp, 'v1', 'merged-binary.bin'), original)
  const version = 'v999.0.0-preview.1'
  const wrongVersion = spawnSync(process.execPath, [packager, temp, destination, version], { encoding: 'utf8' })
  assert.equal(wrongVersion.status, 1)
  assert.match(wrongVersion.stderr, /Expected v[123] firmware version v999\.0\.0-preview\.1/)
  await assert.rejects(stat(destination), { code: 'ENOENT' })
  const stableVersion = spawnSync(process.execPath, [packager, temp, destination, 'v1.0.0'], { encoding: 'utf8' })
  assert.equal(stableVersion.status, 1)
  assert.match(stableVersion.stderr, /Expected a preview version/)
  await assert.rejects(stat(destination), { code: 'ENOENT' })
  console.log('Native installer rejects incompatible flash layouts before creating output')
} finally {
  await rm(temp, { recursive: true, force: true })
}
