<template>
  <div class="esp-install-wrapper">
    <div v-if="!supported" class="unsupported">
      Your browser does not support WebSerial. Use Chrome or Edge on desktop.
    </div>
    <div v-else-if="loadError" class="unsupported">
      Failed to load installer. {{ loadError }}
    </div>
    <div v-else class="install-button">
      <section class="device-group">
        <fieldset class="device-picker" aria-label="Choose JC8012P4A1 hardware version">
          <div class="device-options">
            <label
              v-for="device in availableDevices"
              :key="device.id"
              class="device-option"
              :class="{ 'device-option-selected': device.id === selectedDeviceId }"
            >
              <input v-model="selectedDeviceId" type="radio" name="espframe-device" :value="device.id">
              <span class="device-option-content">
                <span class="device-option-heading">
                  <strong>{{ device.label }}</strong>
                  <small v-if="device.id === selectedDeviceId && manifestVersion">Latest firmware {{ manifestVersion }}</small>
                </span>
                <span class="device-option-detail">{{ device.identification }}</span>
              </span>
            </label>
          </div>
        </fieldset>
      </section>
      <esp-web-install-button v-if="selectedDevice" :manifest="manifestUrl">
        <button slot="activate" class="brand-button">Install {{ selectedDevice.label }}</button>
      </esp-web-install-button>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, onMounted, watch } from 'vue'
import { loadUsbInstaller } from '../usb-installer'

const devices = [
  {
    id: 'immich-frame',
    label: 'V1 — Original panel',
    identification: 'Choose this if the ESP32-P4 chip is not v3.x and the four-digit number on the rear case is 2627 or lower. The case may not say “V1.”',
    manifest: './firmware/manifest.json',
  },
  {
    id: 'immich-frame-v2',
    label: 'V2 — New panel',
    identification: 'Choose this if the ESP32-P4 chip is not v3.x and the four-digit number on the rear case is 2628 or higher. The case may not say “V2.”',
    manifest: './firmware/jc8012p4a1-v2/manifest.json',
    requirePublishedManifest: true,
  },
  {
    id: 'immich-frame-v3',
    label: 'V3 — Production silicon',
    identification: 'Choose this when the chip revision is ESP32-P4 v3.x. This chip check takes priority over the rear-case number.',
    manifest: './firmware/jc8012p4a1-v3/manifest.json',
    requirePublishedManifest: true,
  },
]

const selectedDeviceId = ref('')
const availableDeviceIds = ref(new Set(devices.filter((device) => !device.requirePublishedManifest).map((device) => device.id)))
const supported = ref(false)
const loadError = ref(null)
const manifestVersion = ref('')
const availableDevices = computed(() => devices.filter((device) => availableDeviceIds.value.has(device.id)))
const selectedDevice = computed(() => availableDevices.value.find((device) => device.id === selectedDeviceId.value) || null)
const manifestUrl = computed(() => selectedDevice.value?.manifest || '')

async function loadManifestVersion() {
  manifestVersion.value = ''
  try {
    const response = await fetch(manifestUrl.value, { cache: 'no-store' })
    if (!response.ok) return
    const manifest = await response.json()
    manifestVersion.value = typeof manifest.version === 'string' ? manifest.version : ''
  } catch (_) {
    manifestVersion.value = ''
  }
}

async function discoverPublishedDevices() {
  const available = new Set(availableDeviceIds.value)
  await Promise.all(devices.filter((device) => device.requirePublishedManifest).map(async (device) => {
    try {
      const response = await fetch(device.manifest, { cache: 'no-store' })
      if (response.ok) available.add(device.id)
    } catch (_) {
      // Keep unreleased device profiles hidden until their manifest is published.
    }
  }))
  availableDeviceIds.value = available
}

onMounted(async () => {
  supported.value = 'serial' in navigator
  await discoverPublishedDevices()
  if (!supported.value) return
  try {
    await loadUsbInstaller()
  } catch (err) {
    loadError.value = err?.message || 'Network or script load error.'
  }
})

watch(manifestUrl, loadManifestVersion)
</script>

<style scoped>
.esp-install-wrapper {
  margin: 1.5rem 0;
}

.install-button {
  display: flex;
  flex-direction: column;
  gap: 16px;
  align-items: flex-start;
}

.device-group {
  width: 100%;
}

.device-picker {
  display: block;
  min-width: 0;
  max-width: 980px;
  margin: 0;
  padding: 0;
  border: 0;
}

.device-options {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(min(100%, 280px), 1fr));
  gap: 12px;
}

.device-option {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 12px;
  align-items: start;
  min-height: 132px;
  padding: 16px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 10px;
  cursor: pointer;
  transition: border-color 0.2s, background-color 0.2s;
}

.device-option:hover {
  border-color: var(--vp-c-brand-1);
}

.device-option-selected {
  border-color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

.device-option:has(input:focus-visible) {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}

.device-option input {
  margin: 4px 0 0;
}

.device-option-content,
.device-option-heading {
  display: grid;
  gap: 8px;
}

.device-option-heading {
  grid-template-columns: 1fr auto;
  align-items: start;
  gap: 10px;
}

.device-option-heading small {
  color: var(--vp-c-text-2);
  text-align: right;
}

.device-option-detail {
  display: block;
  color: var(--vp-c-text-2);
  font-size: 14px;
  line-height: 1.5;
}

.device-option-detail code {
  display: inline-block;
  margin: 4px 0;
  padding: 2px 5px;
  border-radius: 4px;
  background: var(--vp-c-bg-soft);
  overflow-wrap: anywhere;
}

@media (max-width: 520px) {
  .device-option-heading {
    grid-template-columns: 1fr;
    gap: 4px;
  }

  .device-option-heading small {
    text-align: left;
  }
}

.brand-button {
  display: inline-block;
  border: 1px solid transparent;
  text-align: center;
  font-weight: 600;
  white-space: nowrap;
  transition: color 0.25s, border-color 0.25s, background-color 0.25s;
  border-radius: 20px;
  padding: 0 20px;
  line-height: 38px;
  font-size: 14px;
  color: var(--vp-button-brand-text);
  background-color: var(--vp-button-brand-bg);
  cursor: pointer;
}

.brand-button:hover {
  background-color: var(--vp-button-brand-hover-bg);
}

.unsupported {
  padding: 12px 16px;
  border-radius: 8px;
  background-color: var(--vp-c-warning-soft);
  color: var(--vp-c-warning-1);
  font-size: 14px;
}
</style>
