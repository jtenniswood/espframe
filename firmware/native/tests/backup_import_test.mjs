import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { connectionTimeouts, createBackup, deviceLanguages, readBackup } from '../installer/backup.mjs'

const fixture = async name => JSON.parse(await readFile(
  new URL(`../../../tests/fixtures/backup/espframe-config-${name}.json`, import.meta.url), 'utf8'))

for (const version of [1, 2, 3]) {
  const result = readBackup({ version, identity: { name: '  Salon été & famille  ', hostname: 'other-frame' } })
  assert.deepEqual(result.settings, { frame_name: 'Salon été & famille' })
  assert.deepEqual(result.notices, [])
}
assert.deepEqual(readBackup({ version: 3, identity: { name: '' } }).settings, { frame_name: '' })
for (const name of ['é'.repeat(61), 'bad\x00name', 'bad\ud800', null]) {
  const result = readBackup({ version: 3, identity: { name } })
  assert.deepEqual(result.settings, {})
  assert.ok(result.notices.some(message => message.includes('frame name')))
}
assert.deepEqual(createBackup({ frame_name: 'Salon été', slide_seconds: 20 }).identity, { name: 'Salon été' })

const partial = readBackup(await fixture('v1-partial'))
assert.deepEqual(partial.settings, {
  immich_url: 'http://192.168.1.30:2283',
  display_mode: 'Fit',
  portrait_pair_framing: 'Fill',
  brightness_day: 80,
  screen_rotation: '180',
  photo_source: 'favorites',
})
assert.equal(partial.chooseSource, false)
for (const name of ['v1-full', 'v2-full']) {
  const result = readBackup(await fixture(name))
  assert.deepEqual(result.settings, {
    immich_url: 'https://photos.example.com', slide_seconds: 15, portrait_pairing: true, portrait_pairs_only: true,
    conn_timeout: '10 minutes',
    portrait_pairing_range: 'Within 2 Days', portrait_pair_framing: 'Fill',
    display_mode: 'Fill', photo_orientation: 'Any',
    transition: name === 'v2-full' ? 'Cross Fade' : 'Random',
    transition_speed: name === 'v2-full' ? 'Fast' : 'Slow',
    brightness_day: 100, brightness_night: 75, screen_rotation: '0',
    base_tone_enabled: false, base_tone: 0, warm_tones_enabled: false, warm_tone_intensity: 50,
    warm_tone_override: false,
    schedule_enabled: false, schedule_on_hour: 6, schedule_off_hour: 23, schedule_wake_timeout: 60,
    show_clock: true, clock_format: '24 Hour', timezone: 'Europe/London (GMT+0)',
    ntp_server_1: '0.pool.ntp.org', ntp_server_2: '1.pool.ntp.org', ntp_server_3: '2.pool.ntp.org',
    firmware_updates: { auto_update: true, update_frequency: 'Daily', wifi_auto_update: true },
    date_filter_enabled: true, date_filter_mode: 'Relative Range', date_from: '2024-01-01',
    date_to: name === 'v2-full' ? '2026-08-30' : '2026-06-07', relative_amount: 2, relative_unit: 'Years',
    ...(name === 'v2-full' ? {
      memories_window: 'Within 2 Days', memories_fallback: true, photo_source: 'custom',
      albums_enabled: true, people_enabled: true, tags_enabled: true,
      favorites_enabled: true, rating_enabled: true, location_enabled: true,
      person_matching: 'All selected people', favorite_mode: 'Exclude favorites', minimum_rating: '4+',
      filter_country: 'New Zealand', filter_state: 'Wellington', filter_city: 'Wellington', album_order: 'Album list order',
      album_ids: '11111111-1111-4111-8111-111111111111', person_ids: '22222222-2222-4222-8222-222222222222',
      tag_ids: '33333333-3333-4333-8333-333333333333', excluded_album_ids: '44444444-4444-4444-8444-444444444444',
      excluded_person_ids: '55555555-5555-4555-8555-555555555555', excluded_tag_ids: '66666666-6666-4666-8666-666666666666',
    }
      : { photo_source: 'album', album_id: '11111111-1111-4111-8111-111111111111',
        person_ids: '22222222-2222-4222-8222-222222222222', tag_ids: '33333333-3333-4333-8333-333333333333' }),
  })
  assert.equal(result.chooseSource, false)
  assert.equal(result.notices.some(text => text.includes('Choose a photo source')), false)
  assert.deepEqual(result.notices, [])
  assert.doesNotMatch(JSON.stringify(result), /fixture-read-only-api-key/)
}

const customTime = readBackup({ version: 3, clock: { ntp_servers: [' time.example.test ', '2001:db8::5'] } })
assert.deepEqual(customTime.settings, { ntp_server_1: 'time.example.test', ntp_server_2: '2001:db8::5', ntp_server_3: '' })
assert.deepEqual(customTime.notices, [])
assert.deepEqual(createBackup({ ...customTime.settings, slide_seconds: 15 }).clock.ntp_servers,
  ['time.example.test', '2001:db8::5', ''])
for (const ntp_servers of ['time.example.test', ['good.test', 'http://bad.test'], [17],
                           ['a.test', 'b.test', 'c.test', 'd.test'], ['a'.repeat(254)]]) {
  const malformedTime = readBackup({ version: 3, clock: { show: false, ntp_servers } })
  assert.deepEqual(malformedTime.settings, { show_clock: false })
  assert.deepEqual(malformedTime.notices, ['Review the time servers in the original backup.'])
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
const legacyPhotos = { display_mode: 'Fit', portrait_pairing: false }
const stock = { version: 3, firmware_updates: { update_frequency: 'Daily' }, photos: legacyPhotos }
assert.deepEqual(readBackup(stock).settings, {
  display_mode: 'Fit', portrait_pairing: false, portrait_pair_framing: 'Fill',
  firmware_updates: { update_frequency: 'Daily' },
})
assert.deepEqual(readBackup({ version: 3, photos: legacyPhotos }).settings, {
  display_mode: 'Fit', portrait_pairing: false,
})
for (const framing of ['Follow photo framing', 'Fit', 'Fill']) {
  const result = readBackup({ ...stock, photos: { ...legacyPhotos, portrait_pair_framing: framing } })
  assert.equal(result.settings.portrait_pair_framing, framing)
}
for (const colour of ['Black', 'White']) {
  const restored = readBackup({ version: 3, photos: { portrait_pair_divider: colour } })
  assert.equal(restored.settings.portrait_pair_divider, colour)
}
assert.equal(readBackup({ version: 3, photos: { portrait_pair_divider: 'Grey' } }).settings.portrait_pair_divider, undefined)

const invalidPairFraming = readBackup({ ...stock, photos: { ...legacyPhotos, portrait_pair_framing: 'Stretch' } })
assert.equal(invalidPairFraming.settings.portrait_pair_framing, undefined)
const invalidUpdates = readBackup({
  version: 3, firmware_updates: { auto_update: false, update_frequency: 'Yearly', wifi_auto_update: 'false', unknown: true },
})
assert.deepEqual(invalidUpdates.settings, { firmware_updates: { auto_update: false } })
assert.deepEqual(invalidUpdates.notices, ['Review the firmware update preferences in the original backup.'])
for (const update_frequency of ['Hourly', 'Daily', 'Weekly', 'Monthly']) {
  const firmware_updates = { auto_update: false, update_frequency, wifi_auto_update: false }
  const copied = createBackup({ immich_url: 'https://photos.test', slide_seconds: 15, display_mode: 'Fit', firmware_updates })
  assert.deepEqual(copied.firmware_updates, firmware_updates)
  const restored = readBackup(copied)
  assert.deepEqual(restored.settings.firmware_updates, firmware_updates)
  assert.equal(restored.settings.portrait_pair_framing, 'Follow photo framing')
  assert.deepEqual(restored.notices, [])
}
assert.equal(createBackup({ slide_seconds: 15 }).firmware_updates, undefined)
assert.match(invalidPairFraming.notices[0], /frame portrait pairs/)
const dateOnly = readBackup({ version: 3, photos: { source: 'Custom', date_filter_enabled: true,
  date_filter_mode: 'Fixed Range', date_from: '2024-02-29', date_to: '' } })
assert.equal(dateOnly.chooseSource, false)
assert.equal(dateOnly.settings.photo_source, 'all')
assert.equal(dateOnly.settings.date_filter_enabled, true)
const pairedOnly = readBackup({ version: 3, photos: { source: 'Custom', portrait_pairing: true, portrait_pairs_only: true } })
assert.deepEqual(pairedOnly, {
  settings: { photo_source: 'all', portrait_pairing: true, portrait_pairs_only: true }, chooseSource: false, notices: [],
})
const inactivePairing = readBackup({ version: 3, photos: { source: 'Custom', portrait_pairing: false, portrait_pairs_only: true } })
assert.equal(inactivePairing.chooseSource, true)
assert.equal(inactivePairing.settings.portrait_pairs_only, true)
const invalidPairsOnly = readBackup({ version: 3, photos: { source: 'All Photos', portrait_pairs_only: 'true' } })
assert.deepEqual(invalidPairsOnly.settings, { photo_source: 'all' })
assert.match(invalidPairsOnly.notices[0], /paired portraits/)
for (const range of ['Within 3 Days', 'Within 5 Days', 'Within 7 Days', 'Within 14 Days', 'Within 30 Days', 'Any Time']) {
  const backup = createBackup({ immich_url: 'https://photos.test', slide_seconds: 60, portrait_pairing_range: range })
  assert.equal(backup.photos.portrait_pairing_range, range)
  const restored = readBackup(backup)
  assert.equal(restored.settings.portrait_pairing_range, range)
  assert.deepEqual(restored.notices, [])
}
const invalidPairingRange = readBackup({ version: 3, photos: { portrait_pairing_range: 'Within 4 Days' } })
assert.deepEqual(invalidPairingRange.settings, {})
assert.match(invalidPairingRange.notices[0], /pairing range/)
const invalidDates = readBackup({ version: 3, photos: { source: 'Favorites', date_filter_enabled: true,
  date_from: '2024-03-01', date_to: '2024-02-29', relative_amount: 0 } })
assert.deepEqual(invalidDates.settings, { photo_source: 'favorites', date_filter_enabled: false })
assert.match(invalidDates.notices[0], /capture date filter/)

assert.deepEqual(readBackup({ version: 2, photos: { favorite_mode: 'Favorites only' } }).settings,
  { photo_source: 'favorites' })
assert.deepEqual(readBackup({ version: 2, photos: { excluded_album_ids: album } }).settings,
  { photo_source: 'custom', albums_enabled: true, excluded_album_ids: album })
assert.deepEqual(readBackup({ version: 2, photos: { minimum_rating: '4+' } }).settings,
  { photo_source: 'custom', rating_enabled: true, minimum_rating: '4+' })
assert.deepEqual(readBackup({ version: 3, photos: { source: 'Memories', albums_enabled: true, album_ids: album,
  memories_window: 'Within 7 Days', memories_fallback: false } }).settings, {
  photo_source: 'memories', album_id: album, albums_enabled: true, album_ids: album,
  memories_window: 'Within 7 Days', memories_fallback: false,
})
assert.equal(readBackup({ version: 3, photos: { source: 'Favorites', people_enabled: true } }).chooseSource, true)
for (const version of [1, 2, 3]) {
  const result = readBackup({ version, photos: { source: 'Memories', orientation: 'Portrait Only' },
    screen: { rotation: '180' } })
  assert.deepEqual(result, { settings: { photo_source: 'memories', photo_orientation: 'Portrait Only',
    screen_rotation: '180' }, chooseSource: false, notices: [] })
  const unrestricted = readBackup({ version, photos: { source: 'All Photos', orientation: 'Any' } })
  assert.deepEqual(unrestricted.settings, { photo_source: 'all', photo_orientation: 'Any' })
  assert.equal(createBackup({ ...unrestricted.settings, slide_seconds: 15 }).photos.orientation, 'Any')
}
const invalidOrientation = readBackup({ version: 3, photos: { source: 'Album', album_ids: album, orientation: '90' } })
assert.equal(invalidOrientation.chooseSource, false)
assert.equal(invalidOrientation.settings.photo_source, 'album')
assert.equal(invalidOrientation.settings.photo_orientation, '')
assert.match(invalidOrientation.notices[0], /Choose which photo orientations/)
assert.deepEqual(readBackup({
  version: 3,
  photos: { source: 'Album', album_ids: album + ',0406c468-dabf-4777-bb93-ac6fdac62857' },
}).settings, { photo_source: 'custom', albums_enabled: true, album_ids: album + ',0406c468-dabf-4777-bb93-ac6fdac62857' })
const person = readBackup({ version: 1, photos: { source: 'Person', person_ids: album.toUpperCase() } })
assert.deepEqual(person.settings, { photo_source: 'custom', people_enabled: true, person_ids: album })
assert.equal(person.legacyPreset, 'Person')
const personBackup = createBackup({ ...person.settings, legacy_photo_preset: person.legacyPreset, slide_seconds: 15 })
assert.equal(personBackup.photos.source, 'Person')
assert.equal(readBackup(personBackup).legacyPreset, 'Person')
assert.equal(readBackup({ version: 3, photos: { source: 'Custom', excluded_person_ids: `${album},broken`, people_enabled: true } }).chooseSource, true)
assert.deepEqual(readBackup({ version: 3, frequency: { interval: '1 hour' } }).settings, { slide_seconds: 3600 })
const productSettings = JSON.parse(await readFile(new URL('../../../product/contract/settings.json', import.meta.url), 'utf8'))
assert.deepEqual(connectionTimeouts, productSettings.find(setting => setting.key === 'conn_timeout').options)
assert.deepEqual(deviceLanguages, productSettings.find(setting => setting.key === 'device_language').options)
for (const language of deviceLanguages) {
  const result = readBackup({ version: 3, screen: { language } })
  assert.deepEqual(result.settings, { device_language: language })
  assert.deepEqual(result.notices, [])
  assert.equal(createBackup({ ...result.settings, slide_seconds: 15 }).screen.language, language)
}
const unsupportedLanguage = readBackup({ version: 3, screen: { language: 'unknown' } })
assert.deepEqual(unsupportedLanguage.settings, {})
assert.deepEqual(unsupportedLanguage.notices, ['Choose a supported display language.'])
for (const conn_timeout of connectionTimeouts) {
  const result = readBackup({ version: 3, frequency: { interval: '15 seconds', conn_timeout } })
  assert.deepEqual(result.settings, { slide_seconds: 15, conn_timeout })
  assert.deepEqual(result.notices, [])
  assert.deepEqual(createBackup(result.settings).frequency, { interval: '15 seconds', conn_timeout })
}
const invalidTimeout = readBackup({ version: 3, frequency: { interval: '15 seconds', conn_timeout: '5 seconds' } })
assert.deepEqual(invalidTimeout.settings, { slide_seconds: 15 })
assert.deepEqual(invalidTimeout.notices, ['Review the connection timeout in the original backup.'])
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
for (const warm_tone_override of [true, false]) {
  const result = readBackup({ version: 3, screen: { warm_tone_override } })
  assert.deepEqual(result.settings, { warm_tone_override })
  assert.deepEqual(result.notices, [])
  assert.equal(createBackup({ ...result.settings, slide_seconds: 15 }).screen.warm_tone_override, warm_tone_override)
}
assert.deepEqual(readBackup({ version: 3, screen: { warm_tone_override: 'true' } }).settings, {})
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
const invalidTransition = readBackup({ version: 3, photos: { transition: 'Dissolve', transition_speed: 500 } })
assert.deepEqual(invalidTransition.settings, {})
assert.equal(invalidTransition.notices.filter(message => message.includes('transition')).length, 2)
for (const source of ['all', 'favorites', 'album', 'memories']) {
  for (const duration of [2, 15, 60, 120, 3600, 86400]) {
    const settings = {
      immich_url: 'https://photos.test', photo_source: source, portrait_pairing: duration % 60 !== 0,
      portrait_pairs_only: duration !== 2,
      portrait_pairing_range: duration % 60 ? 'Within 1 Day' : 'Within 2 Days',
      portrait_pair_framing: duration % 60 ? 'Fill' : 'Follow photo framing',
      slide_seconds: duration, display_mode: duration % 60 ? 'Fit' : 'Fill',
      transition: duration % 60 ? 'Slide Up' : 'None', transition_speed: 'Normal',
      brightness_day: duration % 60 ? 10 : 100,
      brightness_night: 36,
      base_tone_enabled: false, base_tone: 24, warm_tones_enabled: true, warm_tone_intensity: 65,
      screen_rotation: duration % 60 ? '180' : '0',
      schedule_enabled: duration % 60 !== 0, schedule_on_hour: 22, schedule_off_hour: 0, schedule_wake_timeout: 3600,
      show_clock: false, clock_format: '12 Hour', timezone: 'America/New_York (GMT-5)',
      photo_metadata_date_enabled: false, photo_metadata_location_enabled: true,
      photo_metadata_date_format: 'Relative Date', photo_metadata_date_taken_format: 'January 1, 2026',
      memories_window: 'Within 7 Days', memories_fallback: false,
      photo_orientation: duration % 60 ? 'Portrait Only' : 'Landscape Only',
      date_filter_enabled: true, date_filter_mode: 'Relative Range', date_from: '', date_to: '2024-02-29',
      relative_amount: 2, relative_unit: 'Months',
      ...(['album', 'memories'].includes(source) ? { album_id: album } : {}),
    }
    const exported = createBackup(
      { ...settings, ssid: 'secret', password: 'secret', immich_key: 'secret', web_password: 'secret' },
      new Date('2026-11-18T00:00:00Z'))
    assert.doesNotMatch(JSON.stringify(exported), /secret/)
    const expected = source === 'memories' ? { ...settings, album_ids: album } : settings
    assert.deepEqual(readBackup(exported), { settings: expected, notices: [], chooseSource: false })
  }
}
const migrated = readBackup(await fixture('v2-full')).settings
const exported = createBackup(migrated, new Date('2026-11-18T00:00:00Z'))
assert.deepEqual(readBackup(exported), { settings: migrated, notices: [], chooseSource: false })
console.log('Native installer round-trips compound filters and requires review of malformed filters')
