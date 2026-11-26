import { normalizeAlbum, normalizePhotoFilter, photoFilterDefaults, validCaptureDay } from './photo_source.mjs'

const legacySources = ['All Photos', 'Album', 'Favorites', 'Person', 'Tag', 'Memories', 'Custom']
const compoundPresets = ['Album', 'Person', 'Tag']
const groupFlags = { album: 'albums_enabled', person: 'people_enabled', tag: 'tags_enabled' }
const placeFields = { filter_country: 'country', filter_state: 'state', filter_city: 'city' }
const displayModes = ['Fill', 'Fit']
const transitions = ['None', 'Cross Fade', 'Fade to Black', 'Fade to White',
  'Slide Left', 'Slide Right', 'Slide Up', 'Slide Down', 'Random']
const transitionSpeeds = ['Fast', 'Normal', 'Slow']
const pairFramings = ['Follow photo framing', ...displayModes]
const pairDividers = ['Black', 'White']
const photoOrientations = ['Any', 'Portrait Only', 'Landscape Only']
const clockFormats = ['24 Hour', '12 Hour']
const firmwareUpdateFrequencies = ['Hourly', 'Daily', 'Weekly', 'Monthly']
const firmwareUpdateFields = {
  auto_update: value => typeof value === 'boolean',
  update_frequency: value => firmwareUpdateFrequencies.includes(value),
  wifi_auto_update: value => typeof value === 'boolean',
}
export const connectionTimeouts = [
  '30 seconds', '45 seconds', '1 minute', '2 minutes', '3 minutes', '5 minutes',
  '10 minutes', '15 minutes', '20 minutes', '30 minutes',
]
const pairingRanges = ['Same Day', 'Within 1 Day', 'Within 2 Days', 'Within 3 Days',
  'Within 5 Days', 'Within 7 Days', 'Within 14 Days', 'Within 30 Days', 'Any Time']
const memoriesWindows = ['Same Day', 'Within 1 Day', 'Within 2 Days', 'Within 3 Days', 'Within 7 Days']
const groups = ['connection', 'photos', 'frequency', 'screen', 'clock', 'metadata', 'firmware_updates', 'identity']
export const ntpServerFields = ['ntp_server_1', 'ntp_server_2', 'ntp_server_3']
export const deviceLanguages = [
  'en', 'cs', 'da', 'de', 'es', 'fi', 'fr', 'he', 'hu', 'it', 'nb', 'nl',
  'pl', 'pt', 'pt-br', 'ro', 'ru', 'sk', 'sl', 'sv', 'tr', 'uk',
]
export const deviceLanguageLabel = code => new Intl.DisplayNames([code], { type: 'language', languageDisplay: 'standard' }).of(code)
const own = (object, key) => Object.hasOwn(object, key)
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value)
const integerIn = (min, max) => value => Number.isInteger(value) && value >= min && value <= max
function firmwareUpdatePreferences(values, notices) {
  if (!object(values)) return {}
  const preferences = {}
  let needsReview = false
  for (const [key, value] of Object.entries(values)) {
    if (own(firmwareUpdateFields, key) && firmwareUpdateFields[key](value))
      preferences[key] = value
    else
      needsReview = true
  }
  if (needsReview) notices?.push('Review the firmware update preferences in the original backup.')
  return preferences
}
const validNtpServer = value => typeof value === 'string' &&
  new TextEncoder().encode(value.trim()).length <= 253 && !/[\s,/?#]/.test(value.trim())
const captureDateFields = {
  date_filter_enabled: value => typeof value === 'boolean',
  date_filter_mode: value => ['Fixed Range', 'Relative Range'].includes(value),
  date_from: validCaptureDay,
  date_to: validCaptureDay,
  relative_amount: integerIn(1, 120),
  relative_unit: value => ['Years', 'Months'].includes(value),
}

function captureDateFieldsFrom(source, notices) {
  const fields = {}
  let invalid = false
  for (const [key, valid] of Object.entries(captureDateFields)) {
    if (!own(source, key)) continue
    if (valid(source[key])) fields[key] = source[key]
    else invalid = true
  }
  if (fields.date_from && fields.date_to && fields.date_from > fields.date_to) {
    delete fields.date_from
    delete fields.date_to
    invalid = true
  }
  if (invalid) {
    fields.date_filter_enabled = false
    notices?.push('Review the capture date filter before turning it on.')
  }
  return fields
}
const metadataFields = {
  show_date: [value => typeof value === 'boolean', 'photo_metadata_date_enabled'],
  show_location: [value => typeof value === 'boolean', 'photo_metadata_location_enabled'],
  date_format: [value => ['Date Taken', 'Relative Date'].includes(value), 'photo_metadata_date_format'],
  date_taken_format: [value => ['1 January, 2026', 'January 1, 2026'].includes(value), 'photo_metadata_date_taken_format'],
}
const screenFields = {
  rotation: [value => ['0', '180'].includes(value), 'Choose Normal or Upside down for mounting orientation.', 'screen_rotation'],
  brightness_day: [integerIn(10, 100), 'Choose a daytime brightness from 10% to 100%.'],
  brightness_night: [integerIn(10, 100), 'Choose a nighttime brightness from 10% to 100%.'],
  base_tone_enabled: [value => typeof value === 'boolean', 'Review whether base tone is enabled.'],
  base_tone: [integerIn(0, 100), 'Choose a base warmth from 0% to 100%.'],
  warm_tones_enabled: [value => typeof value === 'boolean', 'Review whether night tone is enabled.'],
  warm_tone_intensity: [integerIn(10, 100), 'Choose a night warmth from 10% to 100%.'],
  warm_tone_override: [value => typeof value === 'boolean', 'Review whether warm tone is forced until sunrise.'],
  language: [value => deviceLanguages.includes(value), 'Choose a supported display language.', 'device_language'],
  schedule_enabled: [value => typeof value === 'boolean', 'Review whether the screen schedule is enabled.'],
  schedule_on_hour: [integerIn(0, 23), 'Choose an on hour from 0 to 23.'],
  schedule_off_hour: [integerIn(0, 23), 'Choose an off hour from 0 to 23.'],
  schedule_wake_timeout: [integerIn(10, 3600), 'Choose a wake duration from 10 to 3600 seconds.'],
}

function immichUrl(value) {
  if (typeof value !== 'string') return ''
  const url = value.trim()
  // Match the firmware's connection input. Never bring URL credentials,
  // an embedded API key query, or a fragment along from an older backup.
  if (url.length > 256 || !/^(https?:\/\/)[^/]/.test(url) || /[\s@?#\\]|[^!-~]/.test(url)) return ''
  try {
    return new URL(url).hostname ? url : ''
  } catch {
    return ''
  }
}

function intervalSeconds(value) {
  const match = typeof value === 'string' && /^(\d+) (seconds?|minutes?|hours?)$/.exec(value)
  if (!match) return 0
  const unit = match[2].replace(/s$/, '')
  const seconds = Number(match[1]) * { second: 1, minute: 60, hour: 3600 }[unit]
  return Number.isSafeInteger(seconds) && seconds >= 2 && seconds <= 86_400 ? seconds : 0
}

function importSource(photos, version) {
  const legacy = photos.source
  const supplied = Object.keys(photoFilterDefaults).some(key => own(photos, placeFields[key] ?? key))
  const pairedPortraits = photos.portrait_pairs_only === true && photos.portrait_pairing !== false
  const dateFilter = own(photos, 'date_filter_enabled')
  if (legacy === undefined && !supplied && !dateFilter && !pairedPortraits) return {}

  const filters = {}
  let invalid = legacy !== undefined && !legacySources.includes(legacy)
  for (const key of Object.keys(photoFilterDefaults)) {
    const backupKey = placeFields[key] ?? key
    if (!own(photos, backupKey)) continue
    const value = normalizePhotoFilter(key, photos[backupKey])
    if (value === undefined) invalid = true
    else filters[key] = value
  }
  if (legacy === 'Album') filters.albums_enabled ??= true
  if (legacy === 'Person') filters.people_enabled ??= true
  if (legacy === 'Tag') filters.tags_enabled ??= true
  if (legacy === 'Favorites') {
    filters.favorites_enabled ??= true
    filters.favorite_mode ??= 'Favorites only'
  }
  if (version === 2) {
    for (const [kind, flag] of Object.entries(groupFlags)) {
      if (filters[`excluded_${kind}_ids`]) filters[flag] = true
    }
    filters.favorites_enabled ??= (filters.favorite_mode ?? 'Any') !== 'Any'
    filters.rating_enabled ??= (filters.minimum_rating ?? 'Any') !== 'Any'
    filters.location_enabled ??= Object.keys(placeFields).some(key => !!filters[key])
  }

  const saved = Object.fromEntries(Object.entries(filters).filter(([key, value]) => value !== photoFilterDefaults[key]))
  if (legacy === 'Memories') {
    const album = normalizeAlbum(photos.album_ids)
    return { ...saved, ...(album ? { album_id: album } : {}), ...(
      invalid ? { chooseSource: true } : { photo_source: 'memories' }) }
  }
  const incomplete = Object.entries(groupFlags).some(([kind, flag]) =>
    filters[flag] && !filters[`${kind}_ids`] && !filters[`excluded_${kind}_ids`])
  if (invalid || incomplete || (legacy === 'Custom' && !supplied && !dateFilter && !pairedPortraits)) {
    return { ...saved, chooseSource: true }
  }

  const other = filters.people_enabled || filters.tags_enabled || filters.rating_enabled || filters.location_enabled
  const album = normalizeAlbum(filters.album_ids)
  if (filters.albums_enabled && !other && !filters.favorites_enabled && album && !filters.excluded_album_ids) {
    delete saved.albums_enabled
    delete saved.album_ids
    return { ...saved, photo_source: 'album', album_id: album }
  }
  if (filters.favorites_enabled && !other && !filters.albums_enabled && filters.favorite_mode === 'Favorites only') {
    delete saved.favorites_enabled
    delete saved.favorite_mode
    return { ...saved, photo_source: 'favorites' }
  }
  const custom = other || filters.albums_enabled || filters.favorites_enabled || (legacy === 'Custom' && supplied)
  return { ...saved, photo_source: custom ? 'custom' : 'all' }
}

export function normalizeFrameName(value) {
  if (typeof value !== 'string') return undefined
  const name = value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, '')
  try { encodeURIComponent(name) } catch (_) { return undefined }
  if (new TextEncoder().encode(name).length > 120 || /[\u0000-\u001f\u007f-\u009f]/.test(name)) return undefined
  return name
}

export function readBackup(data) {
  if (!object(data) || ![1, 2, 3].includes(data.version) ||
      groups.some(key => own(data, key) && !object(data[key])))
    throw new Error('Choose an Espframe settings backup in version 1, 2 or 3 format.')

  const settings = {}
  const notices = []
  const photos = data.photos ?? {}
  const connection = data.connection ?? {}
  const frequency = data.frequency ?? {}
  const clock = data.clock ?? {}
  const screen = data.screen ?? {}
  const metadata = data.metadata ?? {}
  if (data.identity) {
    const name = normalizeFrameName(data.identity.name)
    if (name === undefined) notices.push('Review the frame name in the original backup.')
    else settings.frame_name = name
  }
  if (memoriesWindows.includes(photos.memories_window)) settings.memories_window = photos.memories_window
  if (typeof photos.memories_fallback === 'boolean') settings.memories_fallback = photos.memories_fallback
  Object.assign(settings, captureDateFieldsFrom(photos, notices))
  for (const [key, [valid, field]] of Object.entries(metadataFields)) {
    if (valid(metadata[key])) settings[field] = metadata[key]
  }
  if (own(connection, 'immich_url')) {
    const url = immichUrl(connection.immich_url)
    if (url) settings.immich_url = url
    else notices.push('Enter your Immich URL.')
  }
  if (own(frequency, 'interval')) {
    const seconds = intervalSeconds(frequency.interval)
    if (seconds) settings.slide_seconds = seconds
    else notices.push('Choose a slideshow interval.')
  }
  if (own(frequency, 'conn_timeout')) {
    if (connectionTimeouts.includes(frequency.conn_timeout)) settings.conn_timeout = frequency.conn_timeout
    else notices.push('Review the connection timeout in the original backup.')
  }
  for (const [key, options] of [['transition', transitions], ['transition_speed', transitionSpeeds]]) {
    if (!own(photos, key)) continue
    if (options.includes(photos[key])) settings[key] = photos[key]
    else notices.push(`Review the ${key.replace('_', ' ')} in the original backup.`)
  }
  if (typeof photos.portrait_pairing === 'boolean') settings.portrait_pairing = photos.portrait_pairing
  if (own(photos, 'portrait_pairs_only')) {
    if (typeof photos.portrait_pairs_only === 'boolean') settings.portrait_pairs_only = photos.portrait_pairs_only
    else notices.push('Review whether to show paired portraits only.')
  }
  if (own(photos, 'portrait_pairing_range')) {
    if (pairingRanges.includes(photos.portrait_pairing_range)) settings.portrait_pairing_range = photos.portrait_pairing_range
    else notices.push('Choose a portrait pairing range.')
  }
  if (own(photos, 'display_mode')) {
    if (displayModes.includes(photos.display_mode)) settings.display_mode = photos.display_mode
    else notices.push('Choose a display mode.')
  }
  if (own(photos, 'portrait_pair_framing')) {
    if (pairFramings.includes(photos.portrait_pair_framing)) settings.portrait_pair_framing = photos.portrait_pair_framing
    else notices.push('Choose how to frame portrait pairs.')
  } else if ((data.version < 3 || own(data, 'firmware_updates')) &&
             (typeof photos.portrait_pairing === 'boolean' || displayModes.includes(photos.display_mode))) {
    // ESPHome exports version 1/2 or includes firmware_updates. Its paired
    // portraits always fill their halves, independently of display_mode.
    settings.portrait_pair_framing = 'Fill'
  }
  if (own(photos, 'portrait_pair_divider')) {
    if (pairDividers.includes(photos.portrait_pair_divider)) settings.portrait_pair_divider = photos.portrait_pair_divider
    else notices.push('Choose a portrait pair divider colour.')
  }
  if (own(photos, 'orientation')) {
    if (photoOrientations.includes(photos.orientation)) settings.photo_orientation = photos.orientation
    else {
      settings.photo_orientation = ''
      notices.push('Choose which photo orientations to display before saving.')
    }
  }
  if (typeof clock.show === 'boolean') settings.show_clock = clock.show
  if (clockFormats.includes(clock.format)) settings.clock_format = clock.format
  if (typeof clock.timezone === 'string' && clock.timezone.length <= 96) {
    settings.timezone = clock.timezone === 'Asia/Almaty (GMT+6)' ? 'Asia/Almaty (GMT+5)' : clock.timezone
  }
  if (own(clock, 'ntp_servers')) {
    if (Array.isArray(clock.ntp_servers) && clock.ntp_servers.length <= ntpServerFields.length &&
        clock.ntp_servers.every(validNtpServer)) {
      ntpServerFields.forEach((field, index) => {
        settings[field] = clock.ntp_servers[index]?.trim() ?? ''
      })
    } else {
      notices.push('Review the time servers in the original backup.')
    }
  }
  for (const [key, [valid, notice, field = key]] of Object.entries(screenFields)) {
    if (!own(screen, key)) continue
    if (valid(screen[key])) settings[field] = screen[key]
    else {
      notices.push(notice)
      // Keep a malformed schedule from activating with unexpected hours.
      if (key.startsWith('schedule_')) settings.schedule_enabled = false
    }
  }
  const firmwareUpdates = firmwareUpdatePreferences(data.firmware_updates, notices)
  if (Object.keys(firmwareUpdates).length) settings.firmware_updates = firmwareUpdates
  const { chooseSource = false, ...sourceSettings } = importSource(photos, data.version)
  Object.assign(settings, sourceSettings)
  if (chooseSource)
    notices.push('Review the photo filters in this backup. Choose a photo source before saving.')
  const skipped = []
  if (Object.keys(screen).some(key => !own(screenFields, key))) skipped.push('screen settings')
  if (skipped.length) notices.push(`Keep the original backup for ${skipped.join(', ')}.`)
  const legacyPreset = settings.photo_source === 'custom' && compoundPresets.includes(photos.source) ? photos.source : null
  return { settings, notices, chooseSource, ...(legacyPreset ? { legacyPreset } : {}) }
}

function formatInterval(seconds) {
  const [factor, unit] = seconds % 3600 === 0 ? [3600, 'hour']
    : seconds % 60 === 0 ? [60, 'minute'] : [1, 'second']
  const value = seconds / factor
  return `${value} ${unit}${value === 1 ? '' : 's'}`
}

// Export the supported settings using the same groups as the Espframe web app.
export function createBackup(settings, date = new Date()) {
  const { photo_source: source } = settings
  const name = normalizeFrameName(settings.frame_name)
  const filters = {}
  for (const [key, fallback] of Object.entries(photoFilterDefaults)) {
    const value = normalizePhotoFilter(key, settings[key])
    if (value === undefined || value === fallback) continue
    if (typeof value === 'boolean' && !['custom', 'memories'].includes(source)) continue
    filters[placeFields[key] ?? key] = value
  }
  if (source === 'favorites') {
    filters.favorites_enabled = true
    filters.favorite_mode = 'Favorites only'
  }
  if (source === 'album') filters.albums_enabled = true
  if ((source === 'album' || (source === 'memories' && !filters.album_ids)) && normalizeAlbum(settings.album_id))
    filters.album_ids = settings.album_id
  const clock = {
    ...(typeof settings.show_clock === 'boolean' ? { show: settings.show_clock } : {}),
    ...(clockFormats.includes(settings.clock_format) ? { format: settings.clock_format } : {}),
    ...(typeof settings.timezone === 'string' ? { timezone: settings.timezone } : {}),
  }
  if (ntpServerFields.some(field => own(settings, field))) {
    const ntpServers = ntpServerFields.map(field => settings[field] ?? '')
    if (ntpServers.every(validNtpServer))
      clock.ntp_servers = ntpServers.map(server => server.trim())
  }
  const screen = {}
  const metadata = {}
  const firmwareUpdates = firmwareUpdatePreferences(settings.firmware_updates)
  const hasFirmwareUpdates = Object.keys(firmwareUpdates).length !== 0
  for (const [key, [valid, field]] of Object.entries(metadataFields)) {
    if (valid(settings[field])) metadata[key] = settings[field]
  }
  for (const [key, [valid, , field = key]] of Object.entries(screenFields)) {
    if (valid(settings[field])) screen[key] = settings[field]
  }
  return {
    version: 3,
    exported_at: date.toISOString(),
    connection: { immich_url: settings.immich_url },
    photos: {
      source: source === 'memories' ? 'Memories'
        : source === 'custom' ? (compoundPresets.includes(settings.legacy_photo_preset) ? settings.legacy_photo_preset : 'Custom')
        : 'All Photos',
      favorites_enabled: false,
      albums_enabled: false,
      ...filters,
      ...(memoriesWindows.includes(settings.memories_window) ? { memories_window: settings.memories_window } : {}),
      ...(typeof settings.memories_fallback === 'boolean' ? { memories_fallback: settings.memories_fallback } : {}),
      ...captureDateFieldsFrom(settings),
      portrait_pairing: settings.portrait_pairing,
      ...(typeof settings.portrait_pairs_only === 'boolean' ? { portrait_pairs_only: settings.portrait_pairs_only } : {}),
      ...(pairingRanges.includes(settings.portrait_pairing_range) ? { portrait_pairing_range: settings.portrait_pairing_range } : {}),
      ...(displayModes.includes(settings.display_mode) ? { display_mode: settings.display_mode } : {}),
      ...(transitions.includes(settings.transition) ? { transition: settings.transition } : {}),
      ...(transitionSpeeds.includes(settings.transition_speed) ? { transition_speed: settings.transition_speed } : {}),
      ...(pairFramings.includes(settings.portrait_pair_framing) ? { portrait_pair_framing: settings.portrait_pair_framing }
        : hasFirmwareUpdates ? { portrait_pair_framing: 'Follow photo framing' } : {}),
      ...(pairDividers.includes(settings.portrait_pair_divider) ? { portrait_pair_divider: settings.portrait_pair_divider } : {}),
      ...(photoOrientations.includes(settings.photo_orientation) ? { orientation: settings.photo_orientation } : {}),
    },
    frequency: {
      interval: formatInterval(settings.slide_seconds),
      ...(connectionTimeouts.includes(settings.conn_timeout) ? { conn_timeout: settings.conn_timeout } : {}),
    },
    ...(name !== undefined ? { identity: { name } } : {}),
    ...(Object.keys(clock).length ? { clock } : {}),
    ...(Object.keys(screen).length ? { screen } : {}),
    ...(Object.keys(metadata).length ? { metadata } : {}),
    ...(hasFirmwareUpdates ? { firmware_updates: firmwareUpdates } : {}),
  }
}
