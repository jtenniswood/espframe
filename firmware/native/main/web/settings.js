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
const brightnessFields = ['brightness_day', 'brightness_night']
const schedule = document.querySelector('#screen-schedule')
const showBrightness = () => {
  for (const name of brightnessFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of brightnessFields) form.elements[name].addEventListener('input', showBrightness)
form.addEventListener('invalid', event => {
  if (schedule.contains(event.target)) schedule.open = true
}, true)

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
  form.elements.brightness_day.value = settings.brightness_day ?? 64
  form.elements.brightness_night.value = settings.brightness_night ?? form.elements.brightness_day.value
  showBrightness()
  form.elements.portrait_pairing.checked = settings.portrait_pairing === true
  form.elements.display_mode.value = settings.display_mode ?? 'Fit'
  form.elements.screen_rotation.value = settings.screen_rotation ?? '0'
  form.elements.photo_source.value = settings.photo_source ?? 'all'
  form.elements.album_id.value = settings.album_id ?? ''
  form.elements.show_clock.checked = settings.show_clock === true
  form.elements.photo_metadata_date_enabled.checked = settings.photo_metadata_date_enabled !== false
  form.elements.photo_metadata_location_enabled.checked = settings.photo_metadata_location_enabled !== false
  form.elements.photo_metadata_date_format.value = settings.photo_metadata_date_format ?? 'Date Taken'
  form.elements.photo_metadata_date_taken_format.value = settings.photo_metadata_date_taken_format ?? '1 January, 2026'
  form.elements.clock_format.value = settings.clock_format ?? '24 Hour'
  form.elements.schedule_enabled.checked = settings.schedule_enabled === true
  form.elements.schedule_on_hour.value = settings.schedule_on_hour ?? 6
  form.elements.schedule_off_hour.value = settings.schedule_off_hour ?? 23
  form.elements.schedule_wake_timeout.value = settings.schedule_wake_timeout ?? 60
  schedule.open = form.elements.schedule_enabled.checked
  const timezone = settings.timezone ?? 'Europe/London (GMT+0)'
  for (const [value, label] of Object.entries(settings.clock_timezones ?? { [timezone]: timezone })) {
    form.elements.timezone.add(new Option(label.replaceAll('_', ' '), value))
  }
  form.elements.timezone.value = timezone
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
  for (const name of brightnessFields) data[name] = form.elements[name].valueAsNumber
  data.portrait_pairing = form.elements.portrait_pairing.checked
  data.show_clock = form.elements.show_clock.checked
  data.photo_metadata_date_enabled = form.elements.photo_metadata_date_enabled.checked
  data.photo_metadata_location_enabled = form.elements.photo_metadata_location_enabled.checked
  data.schedule_enabled = form.elements.schedule_enabled.checked
  data.schedule_on_hour = Number(data.schedule_on_hour)
  data.schedule_off_hour = Number(data.schedule_off_hour)
  data.schedule_wake_timeout = Number(data.schedule_wake_timeout)
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
