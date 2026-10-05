import { h } from 'vue'
import DefaultTheme from 'vitepress/theme'
import EspInstallButton from './components/EspInstallButton.vue'
import GitHubStars from './components/GitHubStars.vue'
import SupportButton from './components/SupportButton.vue'
import USBSerialLogs from './components/USBSerialLogs.vue'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      'nav-bar-content-after': () => h(GitHubStars),
      'layout-bottom': () => h(SupportButton),
    })
  },
  enhanceApp({ app }) {
    app.component('EspInstallButton', EspInstallButton)
    app.component('USBSerialLogs', USBSerialLogs)
  },
}
