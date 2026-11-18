const immichId = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i

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
  const refresh = () => {
    const selected = source.value === 'album'
    albumChoice.hidden = !selected
    memoriesChoice.hidden = source.value !== 'memories'
    album.disabled = !selected
    album.required = selected
    album.setCustomValidity('')
  }
  source.addEventListener('change', refresh)
  album.addEventListener('input', () => album.setCustomValidity(''))
  return refresh
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
