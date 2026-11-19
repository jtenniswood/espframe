import 'esp-web-tools/dist/install-button.js'
import { configure } from './serial.mjs'

const revision = document.querySelector('#revision')
const install = document.querySelector('esp-web-install-button')
const form = document.querySelector('#settings')
const submit = form.querySelector('button')
const status = document.querySelector('#status')

revision.addEventListener('change', () => {
  install.hidden = !revision.value
  install.manifest = `./${revision.value}.json`
})

if ('serial' in navigator) {
  submit.disabled = false
  form.addEventListener('submit', async event => {
    event.preventDefault()
    if (submit.disabled) return
    submit.disabled = true
    try {
      const port = await navigator.serial.requestPort()
      const data = Object.fromEntries(new FormData(form))
      status.textContent = 'Saving settings on the frame…'
      await configure(port, data)
      form.reset()
      status.textContent = 'Settings saved. The frame will restart and load photos from Immich.'
    } catch (error) {
      status.textContent = error.name === 'NotFoundError' ? 'Choose the frame’s USB port to continue.' : error.message
    } finally {
      submit.disabled = false
    }
  })
} else {
  revision.disabled = true
  submit.disabled = true
  status.textContent = 'Open this page in desktop Chrome or Edge over localhost or HTTPS.'
}
