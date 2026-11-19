import 'esp-web-tools/dist/install-button.js'
import { readBackup } from './backup.mjs'
import { addCaptureDates, bindPhotoSource, bindPortraitPairing, validateAlbum } from './photo_source.mjs'
import { configure, findFrame } from './serial.mjs'

const revision = document.querySelector('#revision')
const install = document.querySelector('esp-web-install-button')
const form = document.querySelector('#settings')
const submit = form.querySelector('button')
const status = document.querySelector('#status')
const find = document.querySelector('#find-frame')
const networkStatus = document.querySelector('#network-status')
const reuseWifi = document.querySelector('#reuse-wifi')
const wifiFields = document.querySelector('#wifi-fields')
const backup = document.querySelector('#backup')
const backupStatus = document.querySelector('#backup-status')
const playback = document.querySelector('#playback')
const schedule = document.querySelector('#screen-schedule')
const refreshSource = bindPhotoSource(form)
const refreshPairing = bindPortraitPairing(form)
const percentageFields = ['brightness_day', 'brightness_night', 'base_tone', 'warm_tone_intensity']
const refreshWifi = () => {
  wifiFields.hidden = reuseWifi.checked
  form.elements.ssid.disabled = reuseWifi.checked
  form.elements.password.disabled = reuseWifi.checked
}
reuseWifi.addEventListener('change', refreshWifi)
refreshWifi()
const showPercentages = () => {
  for (const name of percentageFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of percentageFields) form.elements[name].addEventListener('input', showPercentages)
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
    refreshPairing()
    showPercentages()
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
  find.disabled = false
  const setSerialBusy = busy => {
    submit.disabled = busy
    find.disabled = busy
    backup.disabled = busy
  }
  find.addEventListener('click', async () => {
    setSerialBusy(true)
    networkStatus.textContent = 'Checking the frame’s Wi-Fi connection…'
    try {
      const port = await navigator.serial.requestPort()
      const frame = await findFrame(port)
      if (!frame) {
        networkStatus.textContent = 'The frame is waiting for Wi-Fi. Check the Wi-Fi name and password if it stays offline.'
      } else if (frame.web === 'disabled') {
        networkStatus.textContent = `Connected at ${frame.address}. Set a web settings password to open the frame’s settings.`
      } else if (frame.web === 'starting') {
        networkStatus.textContent = `Connected at ${frame.address}. The settings page is starting. Try again in a moment.`
      } else {
        const link = document.createElement('a')
        link.href = `http://${frame.address}/`
        link.textContent = 'Open frame settings'
        networkStatus.replaceChildren(`Connected at ${frame.address}. `, link)
      }
    } catch (error) {
      networkStatus.textContent = error.name === 'NotFoundError' ? 'Choose the frame’s USB port to continue.' : error.message
    } finally {
      setSerialBusy(false)
    }
  })
  form.addEventListener('submit', async event => {
    event.preventDefault()
    if (submit.disabled) return
    if (importing) {
      backupStatus.textContent = 'Wait for the backup to finish loading.'
      return
    }
    const data = Object.fromEntries(new FormData(form))
    if (!validateAlbum(form, data) || !addCaptureDates(form, data)) return
    data.slide_seconds = Number(data.slide_seconds)
    for (const name of percentageFields) data[name] = form.elements[name].valueAsNumber
    data.base_tone_enabled = form.elements.base_tone_enabled.checked
    data.warm_tones_enabled = form.elements.warm_tones_enabled.checked
    data.portrait_pairing = form.elements.portrait_pairing.checked
    data.portrait_pairs_only = form.elements.portrait_pairs_only.checked
    data.portrait_pairing_range = form.elements.portrait_pairing_range.value
    data.show_clock = form.elements.show_clock.checked
    data.memories_fallback = form.elements.memories_fallback.checked
    data.photo_metadata_date_enabled = form.elements.photo_metadata_date_enabled.checked
    data.photo_metadata_location_enabled = form.elements.photo_metadata_location_enabled.checked
    data.schedule_enabled = form.elements.schedule_enabled.checked
    data.schedule_on_hour = Number(data.schedule_on_hour)
    data.schedule_off_hour = Number(data.schedule_off_hour)
    data.schedule_wake_timeout = Number(data.schedule_wake_timeout)
    setSerialBusy(true)
    networkStatus.textContent = ''
    try {
      const port = await navigator.serial.requestPort()
      status.textContent = 'Saving settings on the frame…'
      await configure(port, data)
      form.reset()
      refreshSource()
      refreshPairing()
      showPercentages()
      refreshWifi()
      backupStatus.textContent = ''
      status.textContent = 'Settings saved. After the frame restarts, choose Find frame on Wi-Fi to open its settings.'
    } catch (error) {
      status.textContent = error.name === 'NotFoundError' ? 'Choose the frame’s USB port to continue.' : error.message
    } finally {
      setSerialBusy(false)
    }
  })
} else {
  revision.disabled = true
  submit.disabled = true
  status.textContent = 'Open this page in desktop Chrome or Edge over localhost or HTTPS.'
}
