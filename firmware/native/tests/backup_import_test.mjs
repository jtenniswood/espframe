import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { createBackup, readBackup } from '../installer/backup.mjs'

const fixture = async name => JSON.parse(await readFile(
  new URL(`../../../tests/fixtures/backup/espframe-config-${name}.json`, import.meta.url), 'utf8'))

const partial = readBackup(await fixture('v1-partial'))
assert.deepEqual(partial.settings, {
  immich_url: 'http://192.168.1.30:2283',
  display_mode: 'Fit',
  brightness_day: 80,
  screen_rotation: '180',
  photo_source: 'favorites',
})
assert.equal(partial.chooseSource, false)
for (const name of ['v1-full', 'v2-full']) {
  const result = readBackup(await fixture(name))
  assert.deepEqual(result.settings, {
    immich_url: 'https://photos.example.com', slide_seconds: 15, portrait_pairing: true, display_mode: 'Fill',
    brightness_day: 100, brightness_night: 75, screen_rotation: '0',
    base_tone_enabled: false, base_tone: 0, warm_tones_enabled: false, warm_tone_intensity: 50,
    schedule_enabled: false, schedule_on_hour: 6, schedule_off_hour: 23, schedule_wake_timeout: 60,
    show_clock: true, clock_format: '24 Hour', timezone: 'Europe/London (GMT+0)',
    date_filter_enabled: true, date_filter_mode: 'Relative Range', date_from: '2024-01-01',
    date_to: name === 'v2-full' ? '2026-08-30' : '2026-06-07', relative_amount: 2, relative_unit: 'Years',
    ...(name === 'v2-full' ? { memories_window: 'Within 2 Days', memories_fallback: true } : {}),
  })
  assert.equal(result.chooseSource, true)
  assert.ok(result.notices.some(text => text.includes('Choose a photo source')))
  assert.ok(result.notices.some(text => text.includes('original backup')))
  assert.doesNotMatch(JSON.stringify(result), /fixture-read-only-api-key/)
}

const album = 'c48e2980-18ac-4d7d-876a-a636759a1aec'
const cleanAlbum = readBackup({
  version: 3,
  connection: { immich_url: 'https://photos.test/proxy', api_key: 'ignore-this' },
  photos: {
    source: 'All Photos', albums_enabled: true, album_ids: album.toUpperCase(),
    portrait_pairing: false,
  },
  frequency: { interval: '24 hours' },
  wifi: { ssid: 'ignore-this', password: 'ignore-this' },
})
assert.deepEqual(cleanAlbum.settings, {
  immich_url: 'https://photos.test/proxy', slide_seconds: 86_400,
  portrait_pairing: false, photo_source: 'album', album_id: album,
})
assert.equal(cleanAlbum.chooseSource, false)
assert.doesNotMatch(JSON.stringify(cleanAlbum), /ignore-this/)
const dateOnly = readBackup({ version: 3, photos: { source: 'Custom', date_filter_enabled: true,
  date_filter_mode: 'Fixed Range', date_from: '2024-02-29', date_to: '' } })
assert.equal(dateOnly.chooseSource, false)
assert.equal(dateOnly.settings.photo_source, 'all')
assert.equal(dateOnly.settings.date_filter_enabled, true)
const invalidDates = readBackup({ version: 3, photos: { source: 'Favorites', date_filter_enabled: true,
  date_from: '2024-03-01', date_to: '2024-02-29', relative_amount: 0 } })
assert.deepEqual(invalidDates.settings, { photo_source: 'favorites', date_filter_enabled: false })
assert.match(invalidDates.notices[0], /capture date filter/)

assert.deepEqual(readBackup({ version: 2, photos: { favorite_mode: 'Favorites only' } }).settings,
  { photo_source: 'favorites' })
assert.equal(readBackup({ version: 2, photos: { excluded_album_ids: album } }).chooseSource, true)
assert.equal(readBackup({ version: 2, photos: { minimum_rating: '4+' } }).chooseSource, true)
assert.deepEqual(readBackup({ version: 3, photos: { source: 'Memories', albums_enabled: true, album_ids: album,
  memories_window: 'Within 7 Days', memories_fallback: false } }).settings, {
  photo_source: 'memories', album_id: album, memories_window: 'Within 7 Days', memories_fallback: false,
})
assert.equal(readBackup({ version: 3, photos: { source: 'Favorites', people_enabled: true } }).chooseSource, true)
assert.equal(readBackup({ version: 3, photos: { source: 'All Photos', orientation: 'Portrait Only' } }).chooseSource, true)
assert.equal(readBackup({
  version: 3,
  photos: { source: 'Album', album_ids: album + ',0406c468-dabf-4777-bb93-ac6fdac62857' },
}).chooseSource, true)
assert.deepEqual(readBackup({ version: 3, frequency: { interval: '1 hour' } }).settings, { slide_seconds: 3600 })
const invalid = readBackup({
  version: 3, connection: { immich_url: 'https://photos.test/?key=secret' }, frequency: { interval: '25 hours' },
})
assert.deepEqual(invalid.settings, {})
assert.deepEqual(invalid.notices, ['Enter your Immich URL.', 'Choose a slideshow interval.'])
assert.deepEqual(readBackup({ version: 3, photos: { display_mode: 'Stretch' } }), {
  settings: {}, notices: ['Choose a display mode.'], chooseSource: false,
})
for (const brightness_day of [9, 100.5, '80']) {
  const result = readBackup({ version: 3, screen: { brightness_day } })
  assert.deepEqual(result.settings, {})
  assert.ok(result.notices.some(text => text.includes('brightness')))
}
const invalidNight = readBackup({ version: 3, screen: { brightness_day: 70, brightness_night: null } })
assert.deepEqual(invalidNight.settings, { brightness_day: 70 })
assert.match(invalidNight.notices[0], /nighttime brightness/)
const portraitMount = readBackup({ version: 3, screen: { rotation: '90' } })
assert.deepEqual(portraitMount.settings, {})
assert.match(portraitMount.notices[0], /mounting orientation/)
const invalidSchedule = readBackup({
  version: 3, screen: { schedule_enabled: true, schedule_on_hour: 24, schedule_off_hour: 0 },
})
assert.deepEqual(invalidSchedule.settings, { schedule_enabled: false, schedule_off_hour: 0 })
assert.match(invalidSchedule.notices[0], /on hour/)
assert.throws(() => readBackup({ version: 4 }), /Espframe settings backup/)
assert.throws(() => readBackup({ version: 3, connection: 'invalid' }), /Espframe settings backup/)
for (const source of ['all', 'favorites', 'album', 'memories']) {
  for (const duration of [2, 15, 60, 120, 3600, 86400]) {
    const settings = {
      immich_url: 'https://photos.test', photo_source: source, portrait_pairing: false,
      slide_seconds: duration, display_mode: duration % 60 ? 'Fit' : 'Fill',
      brightness_day: duration % 60 ? 10 : 100,
      brightness_night: 36,
      base_tone_enabled: false, base_tone: 24, warm_tones_enabled: true, warm_tone_intensity: 65,
      screen_rotation: duration % 60 ? '180' : '0',
      schedule_enabled: duration % 60 !== 0, schedule_on_hour: 22, schedule_off_hour: 0, schedule_wake_timeout: 3600,
      show_clock: false, clock_format: '12 Hour', timezone: 'America/New_York (GMT-5)',
      photo_metadata_date_enabled: false, photo_metadata_location_enabled: true,
      photo_metadata_date_format: 'Relative Date', photo_metadata_date_taken_format: 'January 1, 2026',
      memories_window: 'Within 7 Days', memories_fallback: false,
      date_filter_enabled: true, date_filter_mode: 'Relative Range', date_from: '', date_to: '2024-02-29',
      relative_amount: 2, relative_unit: 'Months',
      ...(['album', 'memories'].includes(source) ? { album_id: album } : {}),
    }
    const exported = createBackup(
      { ...settings, ssid: 'secret', password: 'secret', immich_key: 'secret', web_password: 'secret' },
      new Date('2026-11-18T00:00:00Z'))
    assert.doesNotMatch(JSON.stringify(exported), /secret/)
    assert.deepEqual(readBackup(exported), { settings, notices: [], chooseSource: false })
  }
}
console.log('Native installer imports supported Espframe backup fields and requires review of unsupported filters')
