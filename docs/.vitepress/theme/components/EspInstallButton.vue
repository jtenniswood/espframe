<template>
  <div class="esp-install-wrapper">
    <div v-if="!supported" class="unsupported">
      Your browser does not support WebSerial. Use Chrome or Edge on desktop.
    </div>
    <div v-else-if="loadError" class="unsupported">
      Failed to load installer. {{ loadError }}
    </div>
    <div v-else class="install-button">
      <div class="device-picker" role="radiogroup" aria-label="Choose device model">
        <label v-for="device in devices" :key="device.id" class="device-option">
          <input v-model="selectedDeviceId" type="radio" name="espframe-device" :value="device.id">
          <span>
            <strong>{{ device.label }}</strong>
            <small>{{ device.model }}</small>
          </span>
        </label>
      </div>
      <esp-web-install-button :manifest="manifestUrl">
        <button slot="activate" class="brand-button">Install {{ selectedDevice.buttonLabel }}</button>
      </esp-web-install-button>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, onMounted } from 'vue'

const devices = [
  {
    id: 'jc8012p4a1',
    label: 'Guition ESP32-P4 10-inch',
    model: 'JC8012P4A1',
    buttonLabel: '10-inch Espframe',
    manifest: './firmware/manifest.json',
  },
  {
    id: 'jc1060p470',
    label: 'Guition ESP32-P4 7-inch',
    model: 'JC1060P470',
    buttonLabel: '7-inch Espframe',
    manifest: './firmware/manifest-7inch.json',
  },
]

const selectedDeviceId = ref(devices[0].id)
const supported = ref(false)
const loadError = ref(null)
const selectedDevice = computed(() => devices.find((device) => device.id === selectedDeviceId.value) || devices[0])
const manifestUrl = computed(() => selectedDevice.value.manifest)

onMounted(async () => {
  supported.value = 'serial' in navigator
  if (!supported.value) return
  try {
    await import('https://unpkg.com/esp-web-tools@10.2.1/dist/web/install-button.js')
  } catch (err) {
    loadError.value = err?.message || 'Network or script load error.'
  }
})
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

.device-picker {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(230px, 1fr));
  gap: 10px;
  width: 100%;
  max-width: 620px;
}

.device-option {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 10px;
  align-items: center;
  padding: 12px 14px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  cursor: pointer;
}

.device-option:has(input:checked) {
  border-color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

.device-option input {
  margin: 0;
}

.device-option strong,
.device-option small {
  display: block;
  line-height: 1.3;
}

.device-option small {
  color: var(--vp-c-text-2);
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
