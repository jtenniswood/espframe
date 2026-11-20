import { readFile } from 'node:fs/promises'
import { readBackup } from '../installer/backup.mjs'

async function loadBackup(filename, phase) {
  let data
  try {
    data = JSON.parse(await readFile(filename, 'utf8'))
  } catch {
    throw new Error(`Could not read the ${phase} backup as JSON.`)
  }
  try {
    return readBackup(data)
  } catch {
    throw new Error(`The ${phase} backup must be an Espframe settings backup in version 1, 2 or 3 format.`)
  }
}

function comparableSettings({ settings, legacyPreset }) {
  const { firmware_updates, immich_url, ...fields } = settings
  // Native firmware stores an /api suffix even if the stock backup omits it.
  if (immich_url) fields.immich_url = immich_url.replace(/\/+$/, '').replace(/\/api$/, '')
  if (legacyPreset) fields.legacy_photo_preset = legacyPreset
  for (const [key, value] of Object.entries(firmware_updates ?? {})) {
    fields[`firmware_updates.${key}`] = value
  }
  return fields
}

async function main(args) {
  if (args.length !== 2) {
    throw new Error('Run node firmware/native/tools/compare_migration_backups.mjs BEFORE.json AFTER.json.')
  }
  const before = await loadBackup(args[0], 'before')
  const after = await loadBackup(args[1], 'after')
  const expected = comparableSettings(before)
  const actual = comparableSettings(after)
  const keys = Object.keys(expected).sort()
  if (!keys.length) throw new Error('The before backup contains no recognized settings to compare.')

  const missing = keys.filter(key => !Object.hasOwn(actual, key))
  const changed = keys.filter(key => Object.hasOwn(actual, key) && actual[key] !== expected[key])
  console.log(`Matched ${keys.length - missing.length - changed.length} of ${keys.length} recognized settings.`)
  if (missing.length) console.log(`Missing settings — ${missing.join(', ')}`)
  if (changed.length) console.log(`Changed settings — ${changed.join(', ')}`)
  for (const [phase, backup] of [['Before', before], ['After', after]]) {
    for (const notice of backup.notices) console.log(`${phase} backup — ${notice}`)
  }
  return missing.length || changed.length || before.notices.length || after.notices.length ? 1 : 0
}

try {
  process.exitCode = await main(process.argv.slice(2))
} catch (error) {
  console.error(error.message)
  process.exitCode = 2
}
