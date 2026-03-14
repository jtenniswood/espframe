import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Immich Frame',
  description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
  base: '/espframe/',

  themeConfig: {
    nav: [
      { text: 'Install', link: '/install' },
      { text: 'Docs', link: '/' },
      { text: 'GitHub', link: 'https://github.com/jtenniswood/espframe' },
    ],

    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Overview', link: '/' },
          { text: 'Install', link: '/install' },
          { text: 'Configuration', link: '/configuration' },
          { text: 'Immich API Key', link: '/api-key' },
        ],
      },
      {
        text: 'Advanced',
        items: [
          { text: 'Home Assistant', link: '/home-assistant' },
          { text: 'WireGuard VPN', link: '/wireguard' },
          { text: 'Manual Setup', link: '/manual-setup' },
          { text: 'Troubleshooting', link: '/troubleshooting' },
        ],
      },
      {
        text: 'Project',
        items: [
          { text: 'Roadmap', link: '/roadmap' },
        ],
      },
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/jtenniswood/espframe' },
    ],

    search: {
      provider: 'local',
    },
  },
})
