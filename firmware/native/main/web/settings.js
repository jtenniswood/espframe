const form = document.querySelector('#settings')
const status = document.querySelector('#status')
const submit = form.querySelector('button')
const albumChoice = document.querySelector('#album-choice')
const source = form.elements.photo_source
const album = form.elements.album_id

function updateSource() {
  const isAlbum = source.value === 'album'
  albumChoice.hidden = !isAlbum
  album.disabled = !isAlbum
  album.required = isAlbum
  album.setCustomValidity('')
}

function normalizeAlbum(value) {
  const input = value.trim()
  const uuid = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i
  if (uuid.test(input)) return input.toLowerCase()
  try {
    const url = new URL(input)
    const parts = url.pathname.split('/').filter(Boolean)
    const id = parts.at(-1)
    if (['http:', 'https:'].includes(url.protocol) && parts.at(-2) === 'albums' && uuid.test(id))
      return id.toLowerCase()
  } catch {
    // Report invalid input through the form below.
  }
  return ''
}

source.addEventListener('change', updateSource)
album.addEventListener('input', () => album.setCustomValidity(''))

async function request(options) {
  const response = await fetch('/api/playback', options)
  if (!response.ok) throw new Error(await response.text())
  return response.json()
}

try {
  const settings = await request()
  form.elements.immich_url.value = settings.immich_url
  form.elements.slide_seconds.value = settings.slide_seconds
  form.elements.portrait_pairing.checked = settings.portrait_pairing === true
  source.value = settings.photo_source ?? 'all'
  album.value = settings.album_id ?? ''
  updateSource()
  status.textContent = ''
  form.hidden = false
} catch {
  status.textContent = 'Could not load frame settings. Reload the page to try again.'
}

form.addEventListener('submit', async event => {
  event.preventDefault()
  submit.disabled = true
  const data = Object.fromEntries(new FormData(form))
  data.slide_seconds = Number(data.slide_seconds)
  data.portrait_pairing = form.elements.portrait_pairing.checked
  if (source.value === 'album') {
    data.album_id = normalizeAlbum(data.album_id)
    if (!data.album_id) {
      album.setCustomValidity('Enter a link to an Immich album or its ID.')
      album.reportValidity()
      submit.disabled = false
      return
    }
  }
  try {
    await request({
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data),
    })
    form.elements.immich_key.value = ''
    status.textContent = 'Settings saved. The frame is restarting.'
  } catch (error) {
    submit.disabled = false
    status.textContent = error.message
  }
})
