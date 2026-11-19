const form = document.querySelector('#settings')
const status = document.querySelector('#status')
const submit = form.querySelector('button')

async function request(options) {
  const response = await fetch('/api/playback', options)
  if (!response.ok) throw new Error(await response.text())
  return response.json()
}

try {
  const settings = await request()
  form.elements.immich_url.value = settings.immich_url
  form.elements.slide_seconds.value = settings.slide_seconds
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
