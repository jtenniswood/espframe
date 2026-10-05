<script setup>
import { computed, onMounted, ref } from 'vue'

const repoUrl = 'https://github.com/jtenniswood/espframe'
const stars = ref(null)
const starLabel = computed(() => {
  if (stars.value == null) return '…'
  return new Intl.NumberFormat('en', { notation: 'compact' }).format(stars.value)
})

onMounted(async () => {
  try {
    const response = await fetch('https://api.github.com/repos/jtenniswood/espframe', {
      headers: { Accept: 'application/vnd.github+json' },
    })
    if (!response.ok) return
    const repo = await response.json()
    if (typeof repo.stargazers_count === 'number') stars.value = repo.stargazers_count
  } catch {
    stars.value = null
  }
})
</script>

<template>
  <a
    class="github-stars"
    :href="repoUrl"
    target="_blank"
    rel="noopener"
    aria-label="Star Espframe on GitHub"
    title="Star Espframe on GitHub"
  >
    <svg class="github-stars__icon" aria-hidden="true" viewBox="0 0 24 24">
      <path d="m12 2.5 2.9 5.9 6.5.9-4.7 4.6 1.1 6.5L12 17.3l-5.8 3.1 1.1-6.5-4.7-4.6 6.5-.9L12 2.5Z" />
    </svg>
    <span>Star</span>
    <span class="github-stars__count">{{ starLabel }}</span>
  </a>
</template>

<style scoped>
.github-stars {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  min-height: 32px;
  margin-left: 12px;
  padding: 0 10px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 7px;
  color: var(--vp-c-text-1);
  font-size: 13px;
  font-weight: 600;
  line-height: 1;
  text-decoration: none;
  transition: border-color 0.2s ease, color 0.2s ease, background-color 0.2s ease;
}

.github-stars:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  background: var(--vp-c-bg-soft);
}

.github-stars__icon {
  width: 14px;
  height: 14px;
  color: #d19a00;
  fill: currentColor;
}

.github-stars__count {
  min-width: 28px;
  padding-left: 7px;
  border-left: 1px solid var(--vp-c-divider);
  color: var(--vp-c-text-2);
  text-align: center;
}

.github-stars:hover .github-stars__count {
  color: var(--vp-c-brand-1);
}

@media (max-width: 767px) {
  .github-stars {
    display: none;
  }
}
</style>
