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
    ['meta', { name: 'keywords', content: 'Espframe, Immich, digital photo frame, ESP32, ESP32-P4, ESPHome, photo frame, self-hosted' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:locale', content: 'en_US' }],
    ['meta', { property: 'og:site_name', content: 'Espframe for Immich' }],
    ['meta', { property: 'og:image', content: `${hostname}espframe.png` }],
    ['meta', { property: 'og:image:alt', content: 'Espframe displaying Immich photos on a Guition ESP32-P4 touchscreen' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:image', content: `${hostname}espframe.png` }],
    ['meta', { name: 'twitter:image:alt', content: 'Espframe displaying Immich photos on a Guition ESP32-P4 touchscreen' }],
    ['script', {
      'data-name': 'BMC-Widget',
      'data-cfasync': 'false',
      src: 'https://cdnjs.buymeacoffee.com/1.0.0/widget.prod.min.js',
      'data-id': 'jtenniswood',
      'data-description': 'Support me on Buy me a coffee!',
      'data-message': '',
      'data-color': '#FFDD00',
      'data-position': 'Right',
      'data-x_margin': '28',
      'data-y_margin': '28',
    }],
    ['script', { type: 'application/ld+json' }, JSON.stringify({
      '@context': 'https://schema.org',
      '@graph': [
        {
          '@type': 'WebSite',
          '@id': `${hostname}#website`,
          url: hostname,
          name: 'Espframe for Immich',
          description: 'Standalone Immich-powered digital photo frame on ESP32-P4. No hub, cloud, or extra software required.',
          inLanguage: 'en-US',
        },
        {
          '@type': 'SoftwareApplication',
          '@id': `${hostname}#software`,
          name: 'Espframe for Immich',
          applicationCategory: 'MultimediaApplication',
          operatingSystem: 'ESP32',
          description: 'Standalone Immich-powered digital photo frame on ESP32-P4. Displays your Immich photo library on supported Guition touchscreens over HTTP.',
          url: hostname,
          image: `${hostname}espframe.png`,
          author: {
            '@type': 'Person',
            name: 'jtenniswood',
            url: 'https://github.com/jtenniswood',
          },
          offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
        },
      ],
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

    // Per-page Article schema for docs (helps search and AI understanding)
    if (pageData.relativePath !== 'index.md' && title && description) {
      const isHowTo = pageData.relativePath === 'install.md'
      const articleSchema: Record<string, unknown> = {
        '@context': 'https://schema.org',
        '@type': isHowTo ? 'HowTo' : 'TechArticle',
        name: title,
        description,
        url: canonicalUrl,
        isPartOf: { '@id': `${hostname}#website` },
        author: { '@type': 'Person', name: 'jtenniswood', url: 'https://github.com/jtenniswood' },
      }
      if (isHowTo) {
        articleSchema.step = [
          { '@type': 'HowToStep', name: 'Connect device via USB-C' },
          { '@type': 'HowToStep', name: 'Flash firmware with Web Installer' },
          { '@type': 'HowToStep', name: 'Configure WiFi and Immich' },
        ]
      }
      pageData.frontmatter.head.push(
        ['script', { type: 'application/ld+json' }, JSON.stringify(articleSchema)],
      )
    }
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
          { text: 'Immich API Key', link: '/api-key' },
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
          { text: 'Backup & Restore', link: '/backup' },
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
