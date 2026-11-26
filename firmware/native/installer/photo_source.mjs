const immichId = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i
const filterGroups = [
  { label: 'Albums', flag: 'albums_enabled', kind: 'album', match: ['Any selected album', 'All selected albums'] },
  { label: 'People', flag: 'people_enabled', kind: 'person', match: ['Any selected person', 'All selected people'] },
  { label: 'Tags', flag: 'tags_enabled', kind: 'tag', match: ['Any selected tag', 'All selected tags'] },
]
const filterControls = [
  ...filterGroups.flatMap(({ label, flag, kind, match }) => [
    [flag, `Filter by ${label.toLowerCase()}`, false],
    [`${kind}_ids`, `Include ${label.toLowerCase()}`, 255],
    [`excluded_${kind}_ids`, `Exclude ${label.toLowerCase()}`, 255],
    [`${kind}_matching`, `${label} to match`, match],
  ]),
  ['album_order', 'Album order', ['Random albums', 'Album list order']],
  ['inclusion_matching', 'Combine albums, people and tags', ['Match all enabled groups', 'Match any enabled group']],
  ['favorites_enabled', 'Filter by favourites', false],
  ['favorite_mode', 'Favourites', ['Any', 'Favorites only', 'Exclude favorites']],
  ['rating_enabled', 'Filter by rating', false],
  ['minimum_rating', 'Minimum rating', ['Any', '1+', '2+', '3+', '4+', '5+']],
  ['location_enabled', 'Filter by location', false],
  ['filter_country', 'Country', 96],
  ['filter_state', 'State or province', 96],
  ['filter_city', 'City', 96],
]

export const photoFilterDefaults = Object.fromEntries(filterControls.map(([key, , spec]) =>
  [key, Array.isArray(spec) ? spec[0] : spec === false ? false : '']))

export function normalizePhotoFilter(key, value) {
  const definition = filterControls.find(([name]) => name === key)
  const spec = definition?.[2]
  if (spec === false) return typeof value === 'boolean' ? value : undefined
  if (Array.isArray(spec)) return spec.includes(value) ? value : undefined
  if (typeof spec !== 'number' || typeof value !== 'string' || new TextEncoder().encode(value).length > spec) return
  if (spec === 255) {
    if (!value.trim()) return ''
    const ids = value.split(',').map(id => id.trim())
    return ids.every(id => immichId.test(id)) ? ids.join(',').toLowerCase() : undefined
  }
  return /[\x00-\x1f\x7f]/.test(value) ? undefined : value
}

export function addPhotoFilters(form, values) {
  const editor = form.querySelector('#filter-controls')
  for (const [key, fallback] of Object.entries(photoFilterDefaults)) {
    const field = form.elements[key]
    const value = normalizePhotoFilter(key, typeof fallback === 'boolean' ? field.checked : field.value)
    if (value === undefined) return showFilterError(editor, field, 'Enter valid filter values. Separate Immich IDs with commas.')
    values[key] = value
  }
  if (values.photo_source === 'custom') {
    for (const { flag, kind, label } of filterGroups) {
      if (values[flag] && !values[`${kind}_ids`] && !values[`excluded_${kind}_ids`])
        return showFilterError(editor, form.elements[`${kind}_ids`], `Choose ${label.toLowerCase()} to include or exclude.`)
    }
  }
  return true
}

function showFilterError(editor, field, message) {
  editor.hidden = false
  editor.open = true
  field.setCustomValidity(message)
  field.reportValidity()
  field.setCustomValidity('')
  return false
}

function buildFilterControls(editor) {
  for (const [key, label, spec] of filterControls) {
    const row = document.createElement('label')
    const input = document.createElement(Array.isArray(spec) ? 'select' : 'input')
    input.name = key
    if (spec === false) {
      input.type = 'checkbox'
      input.className = 'inline-option'
      row.append(input, ` ${label}`)
    } else {
      if (Array.isArray(spec)) {
        for (const option of spec) input.add(new Option(option))
      } else {
        input.maxLength = spec
        input.autocapitalize = 'off'
        input.spellcheck = false
      }
      row.append(`${label} `, input)
    }
    editor.append(row)
  }
}

export function validCaptureDay(value) {
  if (value === '') return true
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value)
    || value < '0002-01-01' || value > '9998-12-31') return false
  const instant = new Date(`${value}T00:00:00Z`)
  return !Number.isNaN(instant.valueOf()) && instant.toISOString().slice(0, 10) === value
}

export function normalizeAlbum(value) {
  if (typeof value !== 'string') return ''
  const input = value.trim()
  if (immichId.test(input)) return input.toLowerCase()
  try {
    const url = new URL(input)
    const parts = url.pathname.split('/').filter(Boolean)
    const id = parts.at(-1)
    if (['http:', 'https:'].includes(url.protocol) && parts.at(-2) === 'albums' && immichId.test(id))
      return id.toLowerCase()
  } catch {
    // The field validation reports invalid input.
  }
  return ''
}

export function bindPhotoSource(form) {
  const source = form.elements.photo_source
  const album = form.elements.album_id
  const albumChoice = form.querySelector('#album-choice')
  const memoriesChoice = form.querySelector('#memories-choice')
  const customChoice = form.querySelector('#filter-controls')
  buildFilterControls(customChoice)
  const dateChoices = form.querySelector('#capture-dates')
  const fixedDates = form.querySelector('#fixed-dates')
  const relativeDates = form.querySelector('#relative-dates')
  const dateMode = form.elements.date_filter_mode
  const refreshDates = () => {
    dateChoices.hidden = source.value === 'memories'
    fixedDates.hidden = dateChoices.hidden || dateMode.value === 'Relative Range'
    relativeDates.hidden = dateChoices.hidden || dateMode.value !== 'Relative Range'
    for (const field of fixedDates.querySelectorAll('input')) field.disabled = fixedDates.hidden
    form.elements.relative_amount.disabled = relativeDates.hidden
  }
  const refresh = () => {
    const selected = source.value === 'album'
    albumChoice.hidden = !selected
    memoriesChoice.hidden = source.value !== 'memories'
    customChoice.hidden = source.value !== 'custom'
    album.disabled = !selected
    album.required = selected
    album.setCustomValidity('')
    refreshDates()
  }
  source.addEventListener('change', refresh)
  dateMode.addEventListener('change', refreshDates)
  dateChoices.addEventListener('invalid', () => { dateChoices.open = true }, true)
  album.addEventListener('input', () => album.setCustomValidity(''))
  return refresh
}

export function bindPortraitPairing(form) {
  const { portrait_pairing, portrait_pairs_only, portrait_pairing_range, portrait_pair_framing,
    portrait_pair_divider } = form.elements
  const refresh = () => {
    portrait_pairs_only.disabled = !portrait_pairing.checked
    portrait_pairing_range.disabled = !portrait_pairing.checked
    if (portrait_pair_framing) portrait_pair_framing.disabled = !portrait_pairing.checked
    if (portrait_pair_divider) portrait_pair_divider.disabled = !portrait_pairing.checked
  }
  portrait_pairing.addEventListener('change', refresh)
  refresh()
  return refresh
}

export function addCaptureDates(form, data) {
  const { date_filter_enabled, date_from, date_to, relative_amount } = form.elements
  data.date_filter_enabled = date_filter_enabled.checked
  // Keep both ranges when switching modes, including a switch to On This Day.
  data.date_from = date_from.value
  data.date_to = date_to.value
  data.relative_amount = relative_amount.valueAsNumber
  const invalidDay = [date_from, date_to].find(field => !validCaptureDay(field.value))
  const invalidRange = data.date_from && data.date_to && data.date_from > data.date_to
  const invalidAmount = !Number.isInteger(data.relative_amount) || data.relative_amount < 1 || data.relative_amount > 120
  if (!invalidDay && !invalidRange && !invalidAmount) return true
  const details = form.querySelector('#capture-dates')
  details.hidden = false
  details.open = true
  const field = invalidDay || (invalidRange ? date_to : relative_amount)
  field.closest('div').hidden = false
  field.disabled = false
  field.setCustomValidity(invalidDay ? 'Enter a date between years 0002 and 9998.'
    : invalidRange ? 'Choose a date on or after the start date.' : 'Enter a whole number from 1 to 120.')
  field.reportValidity()
  field.setCustomValidity('')
  return false
}

export function validateAlbum(form, data) {
  const album = form.elements.album_id
  const id = normalizeAlbum(album.value)
  if (id) data.album_id = id
  if (data.photo_source !== 'album') return true
  if (id) return true
  album.setCustomValidity('Enter a link to an Immich album or its ID.')
  album.reportValidity()
  return false
}
