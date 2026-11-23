import 'esp-web-tools/dist/install-button.js'
import { ImprovSerial } from '@improv-wifi/serial-sdk/dist/serial.js'
import { connectionTimeouts, deviceLanguageLabel, deviceLanguages, normalizeFrameName, ntpServerFields, readBackup } from './backup.mjs'
import { addCaptureDates, addPhotoFilters, bindPhotoSource, bindPortraitPairing, photoFilterDefaults, validateAlbum } from './photo_source.mjs'
import { configure, findFrame, waitForFrame } from './serial.mjs'

const revision = document.querySelector('#revision')
const install = document.querySelector('esp-web-install-button')
const installButton = install.querySelector('button')
const form = document.querySelector('#settings')
const submit = form.querySelector('button[type="submit"]')
const status = document.querySelector('#status')
const find = document.querySelector('#find-frame')
const networkStatus = document.querySelector('#network-status')
const webAdminAccess = document.querySelector('#web-admin-access')
const clearWebPassword = document.querySelector('#clear-web-password')
const repairWifi = document.querySelector('#repair-wifi')
const wifiRepairForm = document.querySelector('#wifi-repair')
const connectWifi = wifiRepairForm.querySelector('button')
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
const homeAssistantKey = form.elements.home_assistant_key
const keepStockPairing = document.querySelector('#keep-stock-pairing')
const removeHomeAssistantKey = document.querySelector('#remove-home-assistant-key')
const frameName = form.elements.frame_name
for (const timeout of connectionTimeouts)
  form.elements.conn_timeout.add(new Option(timeout, timeout))
for (const language of deviceLanguages)
  form.elements.device_language.add(new Option(deviceLanguageLabel(language), language))
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
const showPercentages = () => {
  for (const name of percentageFields) {
    form.querySelector(`output[for="${name}"]`).value = `${form.elements[name].value}%`
  }
}
for (const name of percentageFields) form.elements[name].addEventListener('input', showPercentages)
let importSequence = 0
let importing = false
let frameNameSpecified = false
let ntpServersSpecified = false
let importedPreset = null
let importedFirmwareUpdates
for (const name of ntpServerFields)
  form.elements[name].addEventListener('input', () => { ntpServersSpecified = true })
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
    const { firmware_updates, ...fields } = settings
    importedPreset = legacyPreset ?? null
    importedFirmwareUpdates = firmware_updates
    if (Object.hasOwn(settings, 'frame_name')) {
      frameNameSpecified = true
      frameName.setCustomValidity('')
    }
    ntpServersSpecified ||= ntpServerFields.some(name => Object.hasOwn(settings, name))
    if (chooseSource) form.elements.photo_source.value = ''
    if (settings.photo_source && settings.photo_source !== 'album') form.elements.album_id.value = ''
    form.elements.photo_orientation.value = 'Any'
    form.elements.display_mode.value = ''
    form.elements.portrait_pair_framing.value = ''
    form.elements.conn_timeout.value = ''
    form.elements.device_language.value = ''
    form.elements.warm_tone_override.value = ''
    for (const [name, value] of Object.entries({ ...photoFilterDefaults, ...fields })) {
      if (name === 'warm_tone_override') form.elements[name].value = String(value)
      else if (typeof value === 'boolean') form.elements[name].checked = value
      else form.elements[name].value = value
    }
    if (!form.elements.timezone.value) notices.push('Choose a timezone supported by the frame.')
    if (importedFirmwareUpdates)
      notices.push('Update preferences will be kept for an ESPHome restore.')
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

function showFrameAddress(frame, passwordSet = false) {
  if (!frame) {
    networkStatus.textContent = 'The frame is waiting for Wi-Fi. Use Change Wi-Fi over USB to update the connection details.'
    repairWifi.open = true
  } else if (frame.web === 'disabled') {
    const link = document.createElement('a')
    link.href = '#web-admin-password'
    link.textContent = 'Set a web admin password'
    link.addEventListener('click', () => { webAdminAccess.open = true })
    networkStatus.replaceChildren(`Connected at ${frame.address}. `, link, ' and save settings to enable the admin.')
  } else if (frame.web === 'starting') {
    networkStatus.textContent = `Connected at ${frame.address}. The settings page is starting. Try again in a moment.`
  } else {
    const link = document.createElement('a')
    link.href = `http://${frame.address}/`
    link.textContent = 'Open frame settings'
    networkStatus.replaceChildren(`Connected at ${frame.address}. `, link, passwordSet ? ' and sign in as espframe.' : '.')
  }
}

async function provisionWifi(port, ssid, password) {
  await port.open({ baudRate: 115200 })
  let client
  try {
    // Improv debug logging can contain the raw credentials packet.
    const quiet = () => {}
    client = new ImprovSerial(port, { log: quiet, debug: quiet, error: quiet })
    const info = await client.initialize(5000)
    if (!/^Native Espframe V[123]$/.test(info?.firmware) || info.chipFamily !== 'ESP32-P4')
      throw new Error('Select the USB port of a frame running Native Espframe.')
    await client.provision(ssid, password, 30_000)
  } finally {
    try {
      await client?.close()
    } finally {
      await port.close().catch(() => {})
    }
  }
}

if ('serial' in navigator) {
  submit.disabled = false
  find.disabled = false
  repairWifi.hidden = false
  connectWifi.disabled = false
  clearWebPassword.disabled = false
  const setSerialBusy = busy => {
    submit.disabled = busy
    find.disabled = busy
    connectWifi.disabled = busy
    clearWebPassword.disabled = busy
    backup.disabled = busy
    revision.disabled = busy
    installButton.disabled = busy
  }
  const networkFields = ['ssid', 'password']
  for (const name of networkFields) {
    const field = wifiRepairForm.elements[name]
    field.addEventListener('input', () => field.setCustomValidity(''))
  }
  wifiRepairForm.addEventListener('submit', async event => {
    event.preventDefault()
    if (connectWifi.disabled) return
    const encoder = new TextEncoder()
    for (const name of networkFields) {
      const field = wifiRepairForm.elements[name]
      if (encoder.encode(field.value).length > field.maxLength) {
        field.setCustomValidity(`Use up to ${field.maxLength} UTF-8 bytes.`)
        field.reportValidity()
        return
      }
    }
    const { ssid, password } = Object.fromEntries(new FormData(wifiRepairForm))
    setSerialBusy(true)
    networkStatus.textContent = 'Connecting the frame to Wi-Fi…'
    try {
      const port = await navigator.serial.requestPort()
      await provisionWifi(port, ssid, password)
      wifiRepairForm.reset()
      repairWifi.open = false
      keepStockWifi.checked = false
      reuseWifi.checked = true
      refreshWifi()
      networkStatus.textContent = 'Wi-Fi saved. Checking the frame’s address…'
      const frame = await waitForFrame(port)
      if (frame) showFrameAddress(frame)
      else networkStatus.textContent = 'Wi-Fi saved. Choose Find frame on Wi-Fi to check its address again.'
    } catch (error) {
      networkStatus.textContent = error.name === 'NotFoundError'
        ? 'Choose the frame’s USB port to continue.'
        : error.message || String(error)
    } finally {
      setSerialBusy(false)
    }
  })
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
  clearWebPassword.addEventListener('click', async () => {
    setSerialBusy(true)
    networkStatus.textContent = 'Removing the web admin password…'
    try {
      const port = await navigator.serial.requestPort()
      await configure(port, { web_password: '' })
      form.elements.web_password.value = ''
      const frame = await waitForFrame(port)
      if (frame) showFrameAddress(frame)
      else networkStatus.textContent = 'Password removed. Choose Find frame on Wi-Fi to check its address.'
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
    if (!data.web_password) delete data.web_password
    if (importedFirmwareUpdates) data.firmware_updates = importedFirmwareUpdates
    const keepPairing = keepStockPairing.checked
    const keepWifi = keepStockWifi.checked
    const keepImmich = keepStockImmich.checked
    if (frameNameSpecified) data.frame_name = normalizeFrameName(frameName.value)
    else delete data.frame_name
    if (!ntpServersSpecified) {
      for (const name of ntpServerFields) delete data[name]
    }
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
    if (!validateAlbum(form, data) || !addCaptureDates(form, data) || !addPhotoFilters(form, data)) return
    if (data.photo_source === 'custom' && importedPreset) data.legacy_photo_preset = importedPreset
    data.slide_seconds = Number(data.slide_seconds)
    if (!data.conn_timeout) delete data.conn_timeout
    if (!data.device_language) delete data.device_language
    for (const name of percentageFields) data[name] = form.elements[name].valueAsNumber
    data.base_tone_enabled = form.elements.base_tone_enabled.checked
    data.warm_tones_enabled = form.elements.warm_tones_enabled.checked
    if (data.warm_tone_override) data.warm_tone_override = data.warm_tone_override === 'true'
    else delete data.warm_tone_override
    data.portrait_pairing = form.elements.portrait_pairing.checked
    data.portrait_pairs_only = form.elements.portrait_pairs_only.checked
    data.portrait_pairing_range = form.elements.portrait_pairing_range.value
    if (!data.display_mode) delete data.display_mode
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
      ntpServersSpecified = false
      importedPreset = null
      importedFirmwareUpdates = undefined
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
      if (frame) showFrameAddress(frame, Boolean(data.web_password))
      else showFrameAddress(null)
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
