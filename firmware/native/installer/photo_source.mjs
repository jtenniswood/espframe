const immichId = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i

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
  const { portrait_pairing, portrait_pairs_only, portrait_pairing_range } = form.elements
  const refresh = () => {
    portrait_pairs_only.disabled = !portrait_pairing.checked
    portrait_pairing_range.disabled = !portrait_pairing.checked
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
