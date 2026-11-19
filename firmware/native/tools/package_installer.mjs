import { copyFile, mkdir, stat, writeFile } from 'node:fs/promises'
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

for (const board of boards) {
  const binary = await stat(join(binaryRoot, board, 'merged-binary.bin'))
  if (!binary.isFile() || binary.size < 0x10000) throw new Error(`Invalid ${board} factory binary`)
}
await mkdir(output)
for (const board of boards) {
  await copyFile(join(binaryRoot, board, 'merged-binary.bin'), join(output, `${board}.bin`))
  const manifest = {
    name: `Native Espframe ${board.toUpperCase()}`,
    version: 'prototype',
    new_install_prompt_erase: true,
    builds: [{ chipFamily: 'ESP32-P4', parts: [{ path: `${board}.bin`, offset: 0 }] }],
  }
  await writeFile(join(output, `${board}.json`), JSON.stringify(manifest, null, 2) + '\n')
}

await build({
  entryPoints: [join(source, 'setup.js')],
  outfile: join(output, 'setup.js'),
  format: 'esm',
  target: 'chrome112',
  bundle: true,
  minify: true,
})
await Promise.all(['index.html', 'setup.css'].map(name => copyFile(join(source, name), join(output, name))))
