<script setup>
import { computed, nextTick, onMounted, ref } from 'vue'

const supported = ref(false)
const connected = ref(false)
const message = ref('')
const output = ref('')
const outputElement = ref(null)
const maxCharacters = 200_000

let port = null
let reader = null

onMounted(() => {
  supported.value = 'serial' in navigator
})

async function connect() {
  message.value = ''
  let opened = false
  let failed = false
  try {
    port = await navigator.serial.requestPort()
    await port.open({ baudRate: 115200 })
    opened = true
    connected.value = true
    message.value = 'Listening for serial logs at 115200 baud.'
    reader = port.readable.getReader()
    const decoder = new TextDecoder()

    while (true) {
      const { value, done } = await reader.read()
      if (done) break
      output.value = (output.value + decoder.decode(value, { stream: true })).slice(-maxCharacters)
      await nextTick()
      if (outputElement.value) outputElement.value.scrollTop = outputElement.value.scrollHeight
    }
  } catch (error) {
    if (error?.name !== 'NotFoundError') {
      failed = true
      message.value = error?.message || 'Could not read the serial log.'
    }
  } finally {
    if (reader) {
      try { reader.releaseLock() } catch {}
      reader = null
    }
    if (port) {
      try { await port.close() } catch {}
      port = null
    }
    connected.value = false
    if (opened && !failed) message.value = 'USB serial connection closed.'
  }
}

async function disconnect() {
  if (reader) {
    try { await reader.cancel() } catch {}
  }
}

async function copyLogs() {
  try {
    await navigator.clipboard.writeText(output.value)
    message.value = 'Logs copied to the clipboard.'
  } catch {
    message.value = 'Copy failed. Select the log text and copy it manually.'
  }
}
</script>

<template>
  <section class="serial-logs" aria-label="USB serial log viewer">
    <p v-if="!supported" class="status warning">
      USB serial logs require Chrome or Edge on a desktop computer.
    </p>
    <div class="actions">
      <button v-if="!connected && supported" type="button" class="button brand" @click="connect">
        Connect to USB
      </button>
      <button v-if="connected" type="button" class="button" @click="disconnect">
        Stop listening
      </button>
      <button type="button" class="button" :disabled="!output" @click="copyLogs">
        Copy logs
      </button>
      <button type="button" class="button" :disabled="!output" @click="output = ''">
        Clear
      </button>
    </div>
    <p v-if="message" class="status" role="status">{{ message }}</p>
    <pre ref="outputElement" class="output" aria-live="polite">{{ output || 'Connect the display, then restart it to capture startup logs.' }}</pre>
  </section>
</template>

<style scoped>
.serial-logs {
  margin: 1.5rem 0;
}

.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.button {
  padding: 8px 14px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 7px;
  color: var(--vp-c-text-1);
  background: var(--vp-c-bg-soft);
  font: inherit;
  font-size: 14px;
  cursor: pointer;
}

.button.brand {
  border-color: var(--vp-button-brand-bg);
  color: var(--vp-button-brand-text);
  background: var(--vp-button-brand-bg);
}

.button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.status {
  color: var(--vp-c-text-2);
  font-size: 14px;
}

.warning {
  color: var(--vp-c-warning-1);
}

.output {
  min-height: 220px;
  max-height: 480px;
  overflow: auto;
  padding: 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 7px;
  color: var(--vp-c-text-1);
  background: var(--vp-c-bg-soft);
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  font-size: 12px;
  line-height: 1.5;
}
</style>
