import { bindPhotoSource, validateAlbum } from './photo_source.mjs'
import { createBackup } from './backup.mjs'

const form = document.querySelector('#settings')
const status = document.querySelector('#status')
const submit = form.querySelector('button[type="submit"]')
const download = form.querySelector('#download-backup')
const updateSource = bindPhotoSource(form)
const firmware = document.querySelector('#firmware')
const updateForm = document.querySelector('#firmware-update')
const updateButton = updateForm.querySelector('button')
const updateStatus = document.querySelector('#firmware-status')

async function request(options) {
  const response = await fetch('/api/playback', options)
  if (!response.ok) throw new Error(await response.text())
  return response.json()
}

try {
  const settings = await request()
  if (settings.firmware_update && /^v[123]$/.test(settings.board_revision)) {
    document.querySelector('#firmware-help').textContent =
      `Choose ${settings.board_revision}-ota.bin from the native installer package.`
    firmware.hidden = false
  }
  form.elements.immich_url.value = settings.immich_url
  form.elements.slide_seconds.value = settings.slide_seconds
  form.elements.portrait_pairing.checked = settings.portrait_pairing === true
  form.elements.display_mode.value = settings.display_mode ?? 'Fit'
  form.elements.photo_source.value = settings.photo_source ?? 'all'
  form.elements.album_id.value = settings.album_id ?? ''
  updateSource()
  status.textContent = ''
  form.hidden = false
  download.addEventListener('click', () => {
    const backup = createBackup(settings)
    const json = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' })
    const link = document.createElement('a')
    link.href = URL.createObjectURL(json)
    link.download = `espframe-config-${backup.exported_at.slice(0, 10)}.json`
    document.body.append(link)
    link.click()
    link.remove()
    setTimeout(() => URL.revokeObjectURL(link.href), 0)
  })
} catch {
  status.textContent = 'Could not load frame settings. Reload the page to try again.'
}

form.addEventListener('submit', async event => {
  event.preventDefault()
  const data = Object.fromEntries(new FormData(form))
  if (!validateAlbum(form, data)) return
  submit.disabled = true
  data.slide_seconds = Number(data.slide_seconds)
  data.portrait_pairing = form.elements.portrait_pairing.checked
  download.disabled = true
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
    download.disabled = false
    status.textContent = error.message
  }
})

updateForm.addEventListener('submit', async event => {
  event.preventDefault()
  if (firmware.hidden || updateButton.disabled || submit.disabled) return
  const file = updateForm.elements.image.files[0]
  if (!file) return
  updateButton.disabled = true
  submit.disabled = true
  updateStatus.textContent = 'Checking and installing firmware…'
  try {
    const response = await fetch('/api/firmware', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: file,
    })
    if (!response.ok) throw new Error(await response.text())
    const result = await response.json()
    if (!result.staged) throw new Error('The frame did not confirm the update.')
    updateStatus.textContent = 'Update ready. The frame is restarting.'
  } catch (error) {
    updateButton.disabled = false
    submit.disabled = false
    updateStatus.textContent = error.message
  }
})
