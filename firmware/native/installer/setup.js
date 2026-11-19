import 'esp-web-tools/dist/install-button.js'
import { readBackup } from './backup.mjs'
import { bindPhotoSource, validateAlbum } from './photo_source.mjs'
import { configure } from './serial.mjs'

const revision = document.querySelector('#revision')
const install = document.querySelector('esp-web-install-button')
const form = document.querySelector('#settings')
const submit = form.querySelector('button')
const status = document.querySelector('#status')
const backup = document.querySelector('#backup')
const backupStatus = document.querySelector('#backup-status')
const playback = document.querySelector('#playback')
const schedule = document.querySelector('#screen-schedule')
const refreshSource = bindPhotoSource(form)
const brightnessFields = ['brightness_day', 'brightness_night']
const showBrightness = () => {
  for (const name of brightnessFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of brightnessFields) form.elements[name].addEventListener('input', showBrightness)
let importSequence = 0
let importing = false

backup.addEventListener('change', async () => {
  const file = backup.files[0]
  if (!file) return
  const sequence = ++importSequence
  backup.value = ''
  importing = false
  if (file.size > 128 * 1024) {
    backupStatus.textContent = 'This file is too large to be an Espframe settings backup.'
    return
  }
  importing = true
  try {
    const { settings, notices, chooseSource } = readBackup(JSON.parse(await file.text()))
    if (sequence !== importSequence) return
    if (chooseSource) form.elements.photo_source.value = ''
    if (settings.photo_source && settings.photo_source !== 'album') form.elements.album_id.value = ''
    for (const [name, value] of Object.entries(settings)) {
      if (typeof value === 'boolean') form.elements[name].checked = value
      else form.elements[name].value = value
    }
    if (!form.elements.timezone.value) notices.push('Choose a timezone supported by the frame.')
    refreshSource()
    showBrightness()
    playback.open = true
    if (form.elements.schedule_enabled.checked) schedule.open = true
    const message = Object.keys(settings).length ? 'Backup loaded. Review the settings before saving.'
      : 'This backup has no settings the native installer can prefill.'
    backupStatus.textContent = [message, ...notices].join(' ')
  } catch (error) {
    if (sequence !== importSequence) return
    backupStatus.textContent = error instanceof SyntaxError ? 'Could not read the backup JSON.' : error.message
  } finally {
    if (sequence === importSequence) importing = false
  }
})
form.addEventListener('invalid', event => {
  if (playback.contains(event.target)) playback.open = true
  if (schedule.contains(event.target)) schedule.open = true
}, true)

revision.addEventListener('change', () => {
  install.hidden = !revision.value
  install.manifest = `./${revision.value}.json`
})

if ('serial' in navigator) {
  submit.disabled = false
  form.addEventListener('submit', async event => {
    event.preventDefault()
    if (submit.disabled) return
    if (importing) {
      backupStatus.textContent = 'Wait for the backup to finish loading.'
      return
    }
    const data = Object.fromEntries(new FormData(form))
    if (!validateAlbum(form, data)) return
    data.slide_seconds = Number(data.slide_seconds)
    for (const name of brightnessFields) data[name] = form.elements[name].valueAsNumber
    data.portrait_pairing = form.elements.portrait_pairing.checked
    data.show_clock = form.elements.show_clock.checked
    data.schedule_enabled = form.elements.schedule_enabled.checked
    data.schedule_on_hour = Number(data.schedule_on_hour)
    data.schedule_off_hour = Number(data.schedule_off_hour)
    data.schedule_wake_timeout = Number(data.schedule_wake_timeout)
    submit.disabled = true
    backup.disabled = true
    try {
      const port = await navigator.serial.requestPort()
      status.textContent = 'Saving settings on the frame…'
      await configure(port, data)
      form.reset()
      refreshSource()
      showBrightness()
      backupStatus.textContent = ''
      status.textContent = 'Settings saved. The frame will restart and load photos from Immich.'
    } catch (error) {
      status.textContent = error.name === 'NotFoundError' ? 'Choose the frame’s USB port to continue.' : error.message
    } finally {
      submit.disabled = false
      backup.disabled = false
    }
  })
} else {
  revision.disabled = true
  submit.disabled = true
  status.textContent = 'Open this page in desktop Chrome or Edge over localhost or HTTPS.'
}
