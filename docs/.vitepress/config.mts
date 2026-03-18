import { defineConfig } from 'vitepress'

const hostname = 'https://jtenniswood.github.io/espframe/'

export default defineConfig({
  title: 'Espframe for Immich',
  description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
  base: '/espframe/',
  lang: 'en-US',
  cleanUrls: true,
  lastUpdated: true,

  sitemap: {
    hostname,
  },

  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/espframe/favicon.svg' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:site_name', content: 'Espframe for Immich' }],
    ['meta', { property: 'og:image', content: `${hostname}immich-frame.png` }],
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
      name: 'Espframe for Immich',
      applicationCategory: 'MultimediaApplication',
      operatingSystem: 'ESP32',
      description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
      url: hostname,
      image: `${hostname}immich-frame.png`,
      author: {
        '@type': 'Person',
        name: 'jtenniswood',
        url: 'https://github.com/jtenniswood',
      },
      offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
    })],
  ],

  transformPageData(pageData) {
    const canonicalUrl = `${hostname}${pageData.relativePath}`
      .replace(/index\.md$/, '')
      .replace(/\.md$/, '')

    const title = pageData.frontmatter.title || pageData.title
    const description = pageData.frontmatter.description || ''

    pageData.frontmatter.head ??= []
    pageData.frontmatter.head.push(
      ['link', { rel: 'canonical', href: canonicalUrl }],
      ['meta', { property: 'og:title', content: title }],
      ['meta', { property: 'og:description', content: description }],
      ['meta', { property: 'og:url', content: canonicalUrl }],
      ['meta', { name: 'twitter:title', content: title }],
      ['meta', { name: 'twitter:description', content: description }],
    )
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
          { text: 'Immich', link: '/api-key' },
        ],
      },
      {
        text: 'Features',
        items: [
          { text: 'Photo Sources', link: '/photo-sources' },
          { text: 'Firmware Update', link: '/firmware-update' },
          { text: 'Screen Settings', link: '/screen-settings' },
          { text: 'Screen Tone', link: '/screen-tone' },
          { text: 'Touch Controls', link: '/touch-controls' },
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

    editLink: {
      pattern: 'https://github.com/jtenniswood/espframe/edit/main/docs/:path',
      text: 'Edit this page on GitHub',
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/jtenniswood/espframe' },
    ],

    search: {
      provider: 'local',
    },
  },
})
