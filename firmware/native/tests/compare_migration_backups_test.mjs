import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createBackup, readBackup } from '../installer/backup.mjs'

const cli = fileURLToPath(new URL('../tools/compare_migration_backups.mjs', import.meta.url))
const oldBackup = JSON.parse(await readFile(
  new URL('../../../tests/fixtures/backup/espframe-config-v2-full.json', import.meta.url), 'utf8'))
oldBackup.photos.source = 'Person'
oldBackup.connection.api_key = 'older-private-api-key'
const { settings, legacyPreset } = readBackup(oldBackup)
const nativeBackup = createBackup({
  ...settings,
  immich_url: `${settings.immich_url}/api`,
  legacy_photo_preset: legacyPreset,
  device_language: 'en',
})

const workspace = await mkdtemp(join(tmpdir(), 'espframe-compare-backups-'))
try {
  const before = join(workspace, 'before.json')
  const after = join(workspace, 'after.json')
  const run = () => spawnSync(process.execPath, [cli, before, after], { encoding: 'utf8' })
  await writeFile(before, JSON.stringify(oldBackup))
  await writeFile(after, JSON.stringify(nativeBackup))
  const matched = run()
  assert.equal(matched.status, 0, matched.stdout + matched.stderr)
  assert.match(matched.stdout, /Matched (\d+) of \1 recognized settings/)

  delete nativeBackup.clock.ntp_servers
  delete nativeBackup.firmware_updates.wifi_auto_update
  nativeBackup.photos.portrait_pair_framing = 'Fit'
  nativeBackup.photos.source = 'Custom'
  nativeBackup.connection.immich_url = 'https://different-private-origin.test'
  await writeFile(after, JSON.stringify(nativeBackup))
  const mismatch = run()
  assert.equal(mismatch.status, 1)
  assert.match(mismatch.stdout, /Missing settings — firmware_updates\.wifi_auto_update, legacy_photo_preset, ntp_server_1, ntp_server_2, ntp_server_3/)
  assert.match(mismatch.stdout, /Changed settings — immich_url, portrait_pair_framing/)
  assert.doesNotMatch(mismatch.stdout + mismatch.stderr, /older-private-api-key|different-private-origin/)

  await writeFile(after, JSON.stringify({ ...oldBackup, photos: { source: 'Custom' } }))
  const review = run()
  assert.equal(review.status, 1)
  assert.match(review.stdout, /After backup — Review the photo filters/)

  await writeFile(after, '{"connection":"private-syntax-error"')
  const invalid = run()
  assert.equal(invalid.status, 2)
  assert.match(invalid.stderr, /Could not read the after backup as JSON/)
  assert.doesNotMatch(invalid.stdout + invalid.stderr, /private-syntax-error/)

  await writeFile(before, JSON.stringify({ version: 3, connection: { immich_url: 'http://api' } }))
  await writeFile(after, JSON.stringify({ version: 3, connection: { immich_url: 'http://api/api/' } }))
  assert.equal(run().status, 0)

  const writePhotos = (filename, photos) => writeFile(filename, JSON.stringify({ version: 3, photos }))
  const allPhotos = { source: 'All Photos', orientation: 'Any' }
  await writePhotos(before, allPhotos)
  await writePhotos(after, allPhotos)
  assert.equal(run().status, 0)
  await writePhotos(after, { ...allPhotos, orientation: 'Portrait Only' })
  const restricted = run()
  assert.equal(restricted.status, 1)
  assert.match(restricted.stdout, /Changed settings — photo_orientation/)
  await writePhotos(after, { source: 'All Photos' })
  const omitted = run()
  assert.equal(omitted.status, 1)
  assert.match(omitted.stdout, /Missing settings — photo_orientation/)

  const people = { source: 'Custom', people_enabled: true, person_ids: '11111111-1111-4111-8111-111111111111' }
  await writePhotos(before, { ...people, person_matching: 'Any selected person', tags_enabled: false })
  await writePhotos(after, people)
  assert.equal(run().status, 0)
  await writePhotos(after, {
    ...people, person_matching: 'All selected people', tags_enabled: true,
    tag_ids: '22222222-2222-4222-8222-222222222222',
  })
  const changedFilters = run()
  assert.equal(changedFilters.status, 1)
  assert.match(changedFilters.stdout, /Changed settings — person_matching, tag_ids, tags_enabled/)

  for (const target of [before, after]) await writeFile(target, '{"version":3}')
  const empty = run()
  assert.equal(empty.status, 2)
  assert.match(empty.stderr, /no recognized settings to compare/)
} finally {
  await rm(workspace, { recursive: true, force: true })
}

console.log('Migration backup comparison detects missing or changed settings without showing their values')
