import { h } from 'vue'
import DefaultTheme from 'vitepress/theme'
import EspInstallButton from './components/EspInstallButton.vue'
import SupportButton from './components/SupportButton.vue'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      'layout-bottom': () => h(SupportButton),
    })
  },
  enhanceApp({ app }) {
    app.component('EspInstallButton', EspInstallButton)
  },
}
