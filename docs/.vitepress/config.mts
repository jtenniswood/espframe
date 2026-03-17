import { defineConfig } from 'vitepress'

const hostname = 'https://jtenniswood.github.io/espframe/'

export default defineConfig({
  title: 'Immich Frame',
  description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
  base: '/espframe/',

  sitemap: {
    hostname,
  },

  head: [
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:site_name', content: 'Immich Frame' }],
    ['meta', { property: 'og:image', content: `${hostname}immich-frame.png` }],
    ['meta', { property: 'og:url', content: hostname }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:image', content: `${hostname}immich-frame.png` }],
    ['script', {
      'data-name': 'BMC-Widget',
      'data-cfasync': 'false',
      src: 'https://cdnjs.buymeacoffee.com/1.0.0/widget.prod.min.js',
      'data-id': 'jtenniswood',
      'data-description': 'Support me on Buy me a coffee!',
      'data-message': '',
      'data-color': '#FFDD00',
      'data-position': 'Right',
      'data-x_margin': '18',
      'data-y_margin': '18',
    }],
    ['script', { type: 'application/ld+json' }, JSON.stringify({
      '@context': 'https://schema.org',
      '@type': 'SoftwareApplication',
      name: 'Immich Frame',
      applicationCategory: 'MultimediaApplication',
      operatingSystem: 'ESP32',
      description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
      url: hostname,
      offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
    })],
  ],

  transformPageData(pageData) {
    const canonical = `${hostname}${pageData.relativePath}`
      .replace(/index\.md$/, '')
      .replace(/\.md$/, '.html')
    pageData.frontmatter.head ??= []
    pageData.frontmatter.head.push(['link', { rel: 'canonical', href: canonical }])
  },

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
          { text: 'Manual Setup', link: '/manual-setup' },
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
