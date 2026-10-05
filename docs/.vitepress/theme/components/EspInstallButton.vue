<template>
  <div class="esp-install-wrapper">
    <div v-if="!supported" class="unsupported">
      Your browser does not support WebSerial. Use Chrome or Edge on desktop.
    </div>
    <div v-else-if="loadError" class="unsupported">
      Failed to load installer. {{ loadError }}
    </div>
    <div v-else class="install-button">
      <section class="device-group" aria-labelledby="jc8012-heading">
        <h3 id="jc8012-heading">10-inch JC8012P4A1</h3>
        <label class="device-version-label" for="espframe-device-version">Hardware version</label>
        <select id="espframe-device-version" v-model="selectedDeviceId" class="device-version-select" required>
          <option value="" disabled>Choose your panel version</option>
          <option v-for="device in availableDevices" :key="device.id" :value="device.id">
            {{ device.label }}
          </option>
        </select>
        <p v-if="selectedDevice" class="device-version-detail">
          {{ selectedDevice.model }}
          <span v-if="manifestVersion"> Latest firmware: {{ manifestVersion }}</span>
        </p>
      </section>
      <esp-web-install-button v-if="selectedDevice" :manifest="manifestUrl">
        <button slot="activate" class="brand-button">Install {{ selectedDevice.label }}</button>
      </esp-web-install-button>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, onMounted, watch } from 'vue'

const devices = [
  {
    id: 'immich-frame',
    label: 'V1 — Original panel',
    model: 'Rear-case marking 2627 or lower, if the chip is not V3',
    manifest: './firmware/manifest.json',
  },
  {
    id: 'immich-frame-v2',
    label: 'V2 — New panel',
    model: 'Rear-case marking 2628 or higher, if the chip is not V3',
    manifest: './firmware/jc8012p4a1-v2/manifest.json',
    requirePublishedManifest: true,
  },
  {
    id: 'immich-frame-v3',
    label: 'V3 — Production silicon',
    model: 'ESP32-P4 v3.x chip revision; this takes precedence over the case marking',
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
    await import('https://unpkg.com/esp-web-tools@10.2.1/dist/web/install-button.js')
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

.device-group h3 {
  margin: 0 0 10px;
}

.device-version-label {
  display: block;
  margin-bottom: 6px;
  font-weight: 600;
}

.device-version-select {
  display: block;
  width: 100%;
  max-width: 520px;
  min-height: 42px;
  padding: 8px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  color: var(--vp-c-text-1);
  background: var(--vp-c-bg);
  font: inherit;
}

.device-version-detail {
  max-width: 620px;
  margin: 8px 0 0;
  color: var(--vp-c-text-2);
  font-size: 14px;
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
