import 'esp-web-tools/dist/install-button.js'
import { normalizeFrameName, readBackup } from './backup.mjs'
import { addCaptureDates, addPhotoFilters, bindPhotoSource, bindPortraitPairing, photoFilterDefaults, validateAlbum } from './photo_source.mjs'
import { configure, findFrame, waitForFrame } from './serial.mjs'

const revision = document.querySelector('#revision')
const install = document.querySelector('esp-web-install-button')
const installButton = install.querySelector('button')
const form = document.querySelector('#settings')
const submit = form.querySelector('button')
const status = document.querySelector('#status')
const find = document.querySelector('#find-frame')
const networkStatus = document.querySelector('#network-status')
const reuseWifi = document.querySelector('#reuse-wifi')
const keepStockWifi = document.querySelector('#keep-stock-wifi')
const keepStockImmich = document.querySelector('#keep-stock-immich')
const wifiFields = document.querySelector('#wifi-fields')
const immichFields = document.querySelector('#immich-fields')
const backup = document.querySelector('#backup')
const backupStatus = document.querySelector('#backup-status')
const playback = document.querySelector('#playback')
const schedule = document.querySelector('#screen-schedule')
const homeAssistant = document.querySelector('#home-assistant')
const preparedSlides = document.querySelector('#prepared-slides')
const preparedUrl = form.elements.prepared_slide_url
const preparedToken = form.elements.prepared_slide_token
const homeAssistantKey = form.elements.home_assistant_key
const keepStockPairing = document.querySelector('#keep-stock-pairing')
const removeHomeAssistantKey = document.querySelector('#remove-home-assistant-key')
const frameName = form.elements.frame_name
const refreshSource = bindPhotoSource(form)
const refreshPairing = bindPortraitPairing(form)
const percentageFields = ['brightness_day', 'brightness_night', 'base_tone', 'warm_tone_intensity']
const refreshWifi = () => {
  const useSavedWifi = reuseWifi.checked || keepStockWifi.checked
  wifiFields.hidden = useSavedWifi
  form.elements.ssid.disabled = useSavedWifi
  form.elements.password.disabled = useSavedWifi
}
reuseWifi.addEventListener('change', () => {
  if (reuseWifi.checked) keepStockWifi.checked = false
  refreshWifi()
})
keepStockWifi.addEventListener('change', () => {
  if (keepStockWifi.checked) reuseWifi.checked = false
  refreshWifi()
})
refreshWifi()
const refreshImmich = () => {
  immichFields.hidden = keepStockImmich.checked
  form.elements.immich_url.disabled = keepStockImmich.checked
  form.elements.immich_key.disabled = keepStockImmich.checked
}
keepStockImmich.addEventListener('change', refreshImmich)
refreshImmich()
const refreshHomeAssistant = () => {
  homeAssistantKey.disabled = removeHomeAssistantKey.checked || keepStockPairing.checked
  homeAssistantKey.setCustomValidity('')
}
keepStockPairing.addEventListener('change', () => {
  if (keepStockPairing.checked) removeHomeAssistantKey.checked = false
  refreshHomeAssistant()
})
removeHomeAssistantKey.addEventListener('change', () => {
  if (removeHomeAssistantKey.checked) keepStockPairing.checked = false
  refreshHomeAssistant()
})
homeAssistantKey.addEventListener('input', () => homeAssistantKey.setCustomValidity(''))
const clearPreparedErrors = () => {
  preparedUrl.setCustomValidity('')
  preparedToken.setCustomValidity('')
}
preparedUrl.addEventListener('input', clearPreparedErrors)
preparedToken.addEventListener('input', clearPreparedErrors)
form.elements.immich_key.addEventListener('input', clearPreparedErrors)
keepStockImmich.addEventListener('change', clearPreparedErrors)
const showPercentages = () => {
  for (const name of percentageFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of percentageFields) form.elements[name].addEventListener('input', showPercentages)
let importSequence = 0
let importing = false
let frameNameSpecified = false
let importedPreset = null
form.addEventListener('change', event => {
  if (event.target.name === 'photo_source' || Object.hasOwn(photoFilterDefaults, event.target.name))
    importedPreset = null
})
frameName.addEventListener('input', () => {
  frameNameSpecified = true
  frameName.setCustomValidity(normalizeFrameName(frameName.value) === undefined ?
    'Use up to 120 UTF-8 bytes without control characters.' : '')
})

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
    const { settings, notices, chooseSource, legacyPreset } = readBackup(JSON.parse(await file.text()))
    if (sequence !== importSequence) return
    importedPreset = legacyPreset ?? null
    if (Object.hasOwn(settings, 'frame_name')) {
      frameNameSpecified = true
      frameName.setCustomValidity('')
    }
    if (chooseSource) form.elements.photo_source.value = ''
    if (settings.photo_source && settings.photo_source !== 'album') form.elements.album_id.value = ''
    form.elements.photo_orientation.value = 'Any'
    for (const [name, value] of Object.entries({ ...photoFilterDefaults, ...settings })) {
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
  if (preparedSlides.contains(event.target)) preparedSlides.open = true
}, true)

const embeddedFirmware = document.querySelector('#embedded-firmware')
const embeddedManifests = new Map()
let embeddedImages

function manifestFor(board) {
  if (!embeddedFirmware) return `./${board}.json`
  if (!embeddedManifests.has(board)) {
    embeddedImages ??= JSON.parse(embeddedFirmware.textContent)
    const { image, manifest } = embeddedImages[board]
    const decoded = atob(image)
    const bytes = new Uint8Array(decoded.length)
    for (let i = 0; i < decoded.length; ++i) bytes[i] = decoded.charCodeAt(i)
    // ESP Web Tools accepts a blob manifest with an absolute firmware URL.
    manifest.builds[0].parts[0].path = URL.createObjectURL(new Blob([bytes]))
    const url = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }))
    embeddedManifests.set(board, url)
  }
  return embeddedManifests.get(board)
}

revision.addEventListener('change', () => {
  install.hidden = !revision.value
  if (revision.value) install.manifest = manifestFor(revision.value)
})

function showFrameAddress(frame) {
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
}

function addPreparedSlides(data) {
  if (!data.prepared_slide_url && !data.prepared_slide_token) {
    delete data.prepared_slide_url
    delete data.prepared_slide_token
    return true
  }
  preparedUrl.setCustomValidity(!preparedUrl.value || /[?#@\\]/.test(preparedUrl.value) ?
    'Enter the HTTP or HTTPS service URL without credentials, a query, or a fragment.' : '')
  preparedToken.setCustomValidity(!/^[!-~]{32,256}$/.test(preparedToken.value) || preparedToken.value === data.immich_key ?
    'Enter a separate service token of 32 to 256 characters without spaces.' : '')
  return preparedUrl.reportValidity() && preparedToken.reportValidity()
}

if ('serial' in navigator) {
  submit.disabled = false
  find.disabled = false
  const setSerialBusy = busy => {
    submit.disabled = busy
    find.disabled = busy
    backup.disabled = busy
    revision.disabled = busy
    installButton.disabled = busy
  }
  find.addEventListener('click', async () => {
    setSerialBusy(true)
    networkStatus.textContent = 'Checking the frame’s Wi-Fi connection…'
    try {
      const port = await navigator.serial.requestPort()
      showFrameAddress(await findFrame(port))
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
    const keepPairing = keepStockPairing.checked
    const keepWifi = keepStockWifi.checked
    const keepImmich = keepStockImmich.checked
    if (frameNameSpecified) data.frame_name = normalizeFrameName(frameName.value)
    else delete data.frame_name
    if (keepPairing) delete data.home_assistant_key
    else if (removeHomeAssistantKey.checked) data.home_assistant_key = ''
    else if (!homeAssistantKey.value) delete data.home_assistant_key
    else if (!/^[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=$/.test(homeAssistantKey.value) ||
             homeAssistantKey.value === 'A'.repeat(43) + '=') {
      homeAssistant.open = true
      homeAssistantKey.setCustomValidity('Enter a valid 32-byte Base64 ESPHome encryption key.')
      homeAssistantKey.reportValidity()
      return
    }
    if (!addPreparedSlides(data) || !validateAlbum(form, data) || !addCaptureDates(form, data) || !addPhotoFilters(form, data)) return
    if (data.photo_source === 'custom' && importedPreset) data.legacy_photo_preset = importedPreset
    data.slide_seconds = Number(data.slide_seconds)
    for (const name of percentageFields) data[name] = form.elements[name].valueAsNumber
    data.base_tone_enabled = form.elements.base_tone_enabled.checked
    data.warm_tones_enabled = form.elements.warm_tones_enabled.checked
    data.portrait_pairing = form.elements.portrait_pairing.checked
    data.portrait_pairs_only = form.elements.portrait_pairs_only.checked
    data.portrait_pairing_range = form.elements.portrait_pairing_range.value
    const pairFraming = form.elements.portrait_pair_framing.value
    if (pairFraming) data.portrait_pair_framing = pairFraming
    else delete data.portrait_pair_framing
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
      await configure(port, data, { keepPairing, keepWifi, keepImmich })
      form.reset()
      frameNameSpecified = false
      importedPreset = null
      refreshSource()
      refreshPairing()
      showPercentages()
      refreshWifi()
      refreshImmich()
      refreshHomeAssistant()
      backupStatus.textContent = ''
      status.textContent = keepPairing ? 'Settings saved. Home Assistant pairing kept.' : 'Settings saved.'
      networkStatus.textContent = 'Waiting for the frame to restart and reconnect to Wi-Fi…'
      const frame = await waitForFrame(port)
      if (frame) showFrameAddress(frame)
      else networkStatus.textContent = 'The frame hasn’t reconnected yet. Keep it plugged in and choose Find frame on Wi-Fi to check again.'
    } catch (error) {
      status.textContent = error.name === 'NotFoundError' ? 'Choose the frame’s USB port to continue.' : error.message
    } finally {
      setSerialBusy(false)
    }
  })
} else {
  revision.disabled = true
  submit.disabled = true
  status.textContent = 'Open the installer in desktop Chrome or Edge. A hosted copy needs HTTPS or localhost.'
}
