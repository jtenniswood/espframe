import { normalizeAlbum } from './photo_source.mjs'

const legacySources = ['All Photos', 'Album', 'Favorites', 'Custom']
const displayModes = ['Fill', 'Fit']
const clockFormats = ['24 Hour', '12 Hour']
const groups = ['connection', 'photos', 'frequency', 'screen', 'clock', 'firmware_updates', 'identity']
const own = (object, key) => Object.hasOwn(object, key)
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value)
const integerIn = (min, max) => value => Number.isInteger(value) && value >= min && value <= max
const screenFields = {
  rotation: [value => ['0', '180'].includes(value), 'Choose Normal or Upside down for mounting orientation.', 'screen_rotation'],
  brightness_day: [integerIn(10, 100), 'Choose a daytime brightness from 10% to 100%.'],
  brightness_night: [integerIn(10, 100), 'Choose a nighttime brightness from 10% to 100%.'],
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
  const flag = (key, fallback = false) => own(photos, key) ? photos[key] !== false : fallback
  const legacy = photos.source
  const flags = ['albums_enabled', 'people_enabled', 'tags_enabled', 'favorites_enabled', 'rating_enabled',
    'location_enabled', 'date_filter_enabled', 'portrait_pairs_only']
  const exclusions = ['album', 'person', 'tag'].filter(group => {
    const ids = photos[`excluded_${group}_ids`]
    return ids != null && String(ids).trim() !== ''
  })
  const olderFilterHints = version === 2 &&
    ['favorite_mode', 'minimum_rating', 'country', 'state', 'city'].some(key => own(photos, key))
  const hasFilter = legacy !== undefined || flags.some(key => own(photos, key))
    || own(photos, 'orientation') || olderFilterHints || (version === 2 && exclusions.length > 0)
  if (!hasFilter) return {}

  const supported = legacy === undefined || legacySources.includes(legacy)
  const albums = flag('albums_enabled', legacy === 'Album')
  const favorites = flag('favorites_enabled',
    legacy === 'Favorites' || (version === 2 && !!photos.favorite_mode && photos.favorite_mode !== 'Any'))
  const excludesPhotos = exclusions.some(group =>
    version === 2 || flag({ album: 'albums_enabled', person: 'people_enabled', tag: 'tags_enabled' }[group]))
  const hasOtherFilter = flags.some(key => own(photos, key) && typeof photos[key] !== 'boolean')
    || !supported || flag('people_enabled') || flag('tags_enabled') || flag('date_filter_enabled')
    || flag('portrait_pairs_only') || (photos.orientation !== undefined && photos.orientation !== 'Any')
    || flag('rating_enabled', version === 2 && !!photos.minimum_rating && photos.minimum_rating !== 'Any')
    || flag('location_enabled', version === 2 && !!(photos.country || photos.state || photos.city))
    || excludesPhotos
  if (hasOtherFilter || (albums && favorites)) return { chooseSource: true }

  if (albums) {
    const ids = typeof photos.album_ids === 'string' ? photos.album_ids.split(',').filter(id => id.trim()) : []
    const album = ids.length === 1 ? normalizeAlbum(ids[0]) : ''
    return album ? { photo_source: 'album', album_id: album } : { chooseSource: true }
  }
  if (favorites) {
    const mode = photos.favorite_mode ?? (legacy === 'Favorites' ? 'Favorites only' : undefined)
    if (mode === 'Favorites only') return { photo_source: 'favorites' }
    if (mode !== 'Any') return { chooseSource: true }
  }
  if (legacy === 'Custom' && !flags.slice(0, 6).some(key => own(photos, key)))
    return { chooseSource: true }
  return { photo_source: 'all' }
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
  if (typeof photos.portrait_pairing === 'boolean') settings.portrait_pairing = photos.portrait_pairing
  if (own(photos, 'display_mode')) {
    if (displayModes.includes(photos.display_mode)) settings.display_mode = photos.display_mode
    else notices.push('Choose a display mode.')
  }
  if (typeof clock.show === 'boolean') settings.show_clock = clock.show
  if (clockFormats.includes(clock.format)) settings.clock_format = clock.format
  if (typeof clock.timezone === 'string' && clock.timezone.length <= 96) {
    settings.timezone = clock.timezone === 'Asia/Almaty (GMT+6)' ? 'Asia/Almaty (GMT+5)' : clock.timezone
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
  const { chooseSource = false, ...sourceSettings } = importSource(photos, data.version)
  Object.assign(settings, sourceSettings)
  if (chooseSource)
    notices.push('This backup uses photo filters the native player cannot apply. Choose a photo source before saving.')
  const skipped = []
  if (Object.keys(screen).some(key => !own(screenFields, key))) skipped.push('screen settings')
  if (own(clock, 'ntp_servers')) skipped.push('custom time servers')
  if (own(data, 'firmware_updates')) skipped.push('firmware update settings')
  if (own(data, 'identity')) skipped.push('the frame name')
  if (['transition', 'transition_speed', 'portrait_pairing_range'].some(key => own(photos, key)) ||
      own(frequency, 'conn_timeout'))
    skipped.push('advanced playback settings')
  if (skipped.length) notices.push(`Keep the original backup for ${skipped.join(', ')}.`)
  return { settings, notices, chooseSource }
}

function formatInterval(seconds) {
  const [factor, unit] = seconds % 3600 === 0 ? [3600, 'hour']
    : seconds % 60 === 0 ? [60, 'minute'] : [1, 'second']
  const value = seconds / factor
  return `${value} ${unit}${value === 1 ? '' : 's'}`
}

// Export the supported settings using the same groups as the Espframe web app.
export function createBackup(settings, date = new Date()) {
  const favorites = settings.photo_source === 'favorites'
  const album = settings.photo_source === 'album'
  const clock = {
    ...(typeof settings.show_clock === 'boolean' ? { show: settings.show_clock } : {}),
    ...(clockFormats.includes(settings.clock_format) ? { format: settings.clock_format } : {}),
    ...(typeof settings.timezone === 'string' ? { timezone: settings.timezone } : {}),
  }
  const screen = {}
  for (const [key, [valid, , field = key]] of Object.entries(screenFields)) {
    if (valid(settings[field])) screen[key] = settings[field]
  }
  return {
    version: 3,
    exported_at: date.toISOString(),
    connection: { immich_url: settings.immich_url },
    photos: {
      source: 'All Photos',
      favorites_enabled: favorites,
      favorite_mode: favorites ? 'Favorites only' : 'Any',
      albums_enabled: album,
      ...(album ? { album_ids: settings.album_id } : {}),
      portrait_pairing: settings.portrait_pairing,
      ...(displayModes.includes(settings.display_mode) ? { display_mode: settings.display_mode } : {}),
    },
    frequency: { interval: formatInterval(settings.slide_seconds) },
    ...(Object.keys(clock).length ? { clock } : {}),
    ...(Object.keys(screen).length ? { screen } : {}),
  }
}
