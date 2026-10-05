import { defineConfig } from 'vitepress'
import { addDiscoveryMetadata, includeInSitemap } from './discovery'

const hostname = 'https://jtenniswood.github.io/espframe/'

export default defineConfig({
  title: 'EspFrame for Immich',
  description: 'Standalone Immich-powered digital photo frame on ESP32-P4',
  base: '/espframe/',
  lang: 'en-US',
  cleanUrls: true,
  lastUpdated: true,

  sitemap: {
    hostname,
    transformItems(items) {
      return items.filter((item) => includeInSitemap(item.url))
    },
  },

  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/espframe/favicon.svg' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:locale', content: 'en_US' }],
    ['meta', { property: 'og:site_name', content: 'EspFrame for Immich' }],
    ['meta', { property: 'og:image', content: `${hostname}espframe.png` }],
    ['meta', { property: 'og:image:alt', content: 'EspFrame displaying Immich photos on a Guition ESP32-P4 touchscreen' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:image', content: `${hostname}espframe.png` }],
    ['meta', { name: 'twitter:image:alt', content: 'EspFrame displaying Immich photos on a Guition ESP32-P4 touchscreen' }],
    ['script', { type: 'application/ld+json' }, JSON.stringify({
      '@context': 'https://schema.org',
      '@graph': [
        {
          '@type': 'WebSite',
          '@id': `${hostname}#website`,
          url: hostname,
          name: 'EspFrame for Immich',
          description: 'Standalone Immich-powered digital photo frame on ESP32-P4. No hub, cloud, or extra software required.',
          inLanguage: 'en-US',
        },
        {
          '@type': 'SoftwareApplication',
          '@id': `${hostname}#software`,
          name: 'EspFrame for Immich',
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
    addDiscoveryMetadata(pageData)

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