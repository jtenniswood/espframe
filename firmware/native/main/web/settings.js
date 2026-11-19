import { addCaptureDates, addPhotoFilters, bindPhotoSource, bindPortraitPairing, photoFilterDefaults, validateAlbum } from './photo_source.mjs'
import { createBackup } from './backup.mjs'

const form = document.querySelector('#settings')
const status = document.querySelector('#status')
const submit = form.querySelector('button[type="submit"]')
const download = form.querySelector('#download-backup')
const updateSource = bindPhotoSource(form)
const updatePairing = bindPortraitPairing(form)
const firmware = document.querySelector('#firmware')
const updateForm = document.querySelector('#firmware-update')
const updateButton = updateForm.querySelector('button')
const updateStatus = document.querySelector('#firmware-status')
const resetSection = document.querySelector('#customization-reset')
const resetButton = document.querySelector('#open-reset')
const resetDialog = document.querySelector('#confirm-reset')
const resetStatus = document.querySelector('#reset-status')
const factorySection = document.querySelector('#factory-reset')
const factoryButton = document.querySelector('#open-factory-reset')
const factoryDialog = document.querySelector('#confirm-factory-reset')
let resetPath = ''
let resetEpoch
const percentageFields = ['brightness_day', 'brightness_night', 'base_tone', 'warm_tone_intensity']
const schedule = document.querySelector('#screen-schedule')
const showPercentages = () => {
  for (const name of percentageFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of percentageFields) form.elements[name].addEventListener('input', showPercentages)
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
  resetEpoch = settings.reset_epoch
  if (settings.firmware_update && /^v[123]$/.test(settings.board_revision)) {
    document.querySelector('#firmware-help').textContent =
      `Choose ${settings.board_revision}-ota.bin from the native installer package.`
    firmware.hidden = false
  }
  form.elements.immich_url.value = settings.immich_url
  if (settings.api_key_configured === false) {
    document.querySelector('#immich-key-label').textContent = 'Immich API key'
    document.querySelector('#keep-key').textContent = 'Enter your API key to connect Immich.'
    form.elements.immich_key.required = true
  }
  if (settings.reset_path === '/espframe/api/v1/reset') {
    resetPath = settings.reset_path
    resetSection.hidden = false
    factorySection.hidden = settings.factory_reset !== true
  }
  form.elements.slide_seconds.value = settings.slide_seconds
  form.elements.brightness_day.value = settings.brightness_day ?? 64
  form.elements.brightness_night.value = settings.brightness_night ?? form.elements.brightness_day.value
  form.elements.base_tone_enabled.checked = settings.base_tone_enabled === true
  form.elements.base_tone.value = settings.base_tone ?? 0
  form.elements.warm_tones_enabled.checked = settings.warm_tones_enabled === true
  form.elements.warm_tone_intensity.value = settings.warm_tone_intensity ?? 50
  showPercentages()
  form.elements.portrait_pairing.checked = settings.portrait_pairing === true
  form.elements.portrait_pairs_only.checked = settings.portrait_pairs_only === true
  form.elements.portrait_pairing_range.value = settings.portrait_pairing_range ?? 'Same Day'
  form.elements.portrait_pair_framing.value = settings.portrait_pair_framing ?? 'Follow photo framing'
  updatePairing()
  form.elements.display_mode.value = settings.display_mode ?? 'Fit'
  form.elements.photo_orientation.value = settings.photo_orientation ?? 'Any'
  form.elements.screen_rotation.value = settings.screen_rotation ?? '0'
  form.elements.photo_source.value = settings.photo_source ?? 'all'
  for (const [key, fallback] of Object.entries(photoFilterDefaults)) {
    const value = settings[key] ?? fallback
    if (typeof fallback === 'boolean') form.elements[key].checked = value
    else form.elements[key].value = value
  }
  form.elements.album_id.value = settings.album_id ?? ''
  form.elements.memories_window.value = settings.memories_window ?? 'Within 2 Days'
  form.elements.memories_fallback.checked = settings.memories_fallback !== false
  form.elements.date_filter_enabled.checked = settings.date_filter_enabled === true
  form.elements.date_filter_mode.value = settings.date_filter_mode ?? 'Fixed Range'
  form.elements.date_from.value = settings.date_from ?? ''
  form.elements.date_to.value = settings.date_to ?? ''
  form.elements.relative_amount.value = settings.relative_amount ?? 1
  form.elements.relative_unit.value = settings.relative_unit ?? 'Years'
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
  if (submit.disabled) return
  const data = Object.fromEntries(new FormData(form))
  if (!validateAlbum(form, data) || !addCaptureDates(form, data) || !addPhotoFilters(form, data)) return
  if (Number.isInteger(resetEpoch)) data.reset_epoch = resetEpoch
  submit.disabled = true
  data.slide_seconds = Number(data.slide_seconds)
  for (const name of percentageFields) data[name] = form.elements[name].valueAsNumber
  data.base_tone_enabled = form.elements.base_tone_enabled.checked
  data.warm_tones_enabled = form.elements.warm_tones_enabled.checked
  data.portrait_pairing = form.elements.portrait_pairing.checked
  data.portrait_pairs_only = form.elements.portrait_pairs_only.checked
  data.portrait_pairing_range = form.elements.portrait_pairing_range.value
  data.portrait_pair_framing = form.elements.portrait_pair_framing.value
  data.show_clock = form.elements.show_clock.checked
  data.memories_fallback = form.elements.memories_fallback.checked
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

async function applyReset(mode) {
  if (!resetPath || submit.disabled) return
  const actions = [resetButton, factoryButton, submit, download, updateButton]
  for (const button of actions) button.disabled = true
  resetStatus.textContent = 'Resetting settings…'
  try {
    const response = await fetch(resetPath, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ mode }),
    })
    const payload = await response.json()
    if (!response.ok || payload.status !== 'accepted') throw new Error('reset_failed')
  } catch {
    for (const button of actions) button.disabled = false
    resetStatus.textContent = 'Could not confirm the reset. Reload the settings to check the frame.'
    return
  }
  if (mode === 'factory') {
    resetStatus.textContent = 'Factory reset accepted. After the frame restarts, connect it by USB and open “Connect Wi-Fi and Immich” in the native browser installer.'
    return
  }
  resetStatus.textContent = 'Settings reset. The frame is restarting.'
  for (let attempt = 0; attempt < 5; ++attempt) {
    await new Promise(resolve => setTimeout(resolve, attempt === 0 ? 2500 : 1500))
    try {
      const response = await fetch(resetPath, { cache: 'no-store', signal: AbortSignal.timeout(2000) })
      if (response.ok && (await response.json()).status === 'ready') {
        window.location.reload()
        return
      }
    } catch {
      // The frame may be joining Wi-Fi again.
    }
  }
  resetStatus.textContent = 'Reload this page when the frame reconnects to set up Immich.'
}

for (const [mode, button, dialog, suffix] of [
  ['customization', resetButton, resetDialog, 'reset'],
  ['factory', factoryButton, factoryDialog, 'factory-reset'],
]) {
  button.addEventListener('click', () => {
    if (resetPath && !submit.disabled) dialog.showModal()
  })
  document.querySelector(`#cancel-${suffix}`).addEventListener('click', () => dialog.close())
  document.querySelector(`#apply-${suffix}`).addEventListener('click', () => {
    dialog.close()
    applyReset(mode)
  })
}
