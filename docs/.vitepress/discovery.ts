import { readFileSync } from 'node:fs'

const internalDocumentation = new Set([
  'repository-governance',
  'source-ownership',
  'testing',
])

function pageSlug(url: string): string {
  const pathname = new URL(url, 'https://jtenniswood.github.io').pathname.replace(/\/$/, '')
  return pathname.split('/').pop() ?? ''
}

export function includeInSitemap(url: string): boolean {
  return !internalDocumentation.has(pageSlug(url)) && pageSlug(url) !== '404'
}

export function addDiscoveryMetadata(pageData: {
  relativePath: string
  frontmatter: Record<string, any> & { head?: any[] }
}): void {
  const slug = pageData.relativePath.replace(/\.md$/, '').replace(/\/index$/, '')
  pageData.frontmatter.head ??= []
  if (internalDocumentation.has(slug)) {
    pageData.frontmatter.head.push(['meta', { name: 'robots', content: 'noindex,follow' }])
  }

  if (pageData.relativePath === 'faq.md') {
    const faqSource = readFileSync(new URL('../faq.md', import.meta.url), 'utf8')
    const faqMarkdown = faqSource.replace(/^---[\s\S]*?---\s*/, '')
    const faqItems = faqMarkdown.split(/^## /m).slice(1).map((section) => {
      const [question, ...lines] = section.split('\n')
      const answer = lines
        .join(' ')
        .replace(/\[([^\]]+)\]\([^)]+\)/g, '$1')
        .replace(/`([^`]+)`/g, '$1')
        .replace(/[*_]/g, '')
        .replace(/\s+/g, ' ')
        .trim()
      return { question: question.trim(), answer }
    }).filter((item) => item.question && item.answer)

    pageData.frontmatter.head.push([
      'script',
      { type: 'application/ld+json' },
      JSON.stringify({
        '@context': 'https://schema.org',
        '@type': 'FAQPage',
        mainEntity: faqItems.map((item: { question: string; answer: string }) => ({
          '@type': 'Question',
          name: item.question,
          acceptedAnswer: { '@type': 'Answer', text: item.answer },
        })),
      }),
    ])
  }
}
