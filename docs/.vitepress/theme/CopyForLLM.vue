<template>
  <div class="llm-copy">
    <button @click="copyReference" :class="{ copied }">
      <svg v-if="!copied" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
      <svg v-else xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>
      <span v-if="!copied">Copy Nyx reference for AI</span>
      <span v-else>Copied!</span>
    </button>
  </div>
</template>

<script setup>
import { ref } from 'vue'

const copied = ref(false)

async function copyReference() {
  try {
    const res = await fetch('/nyx-llm-reference.txt')
    const text = await res.text()
    await navigator.clipboard.writeText(text)
    copied.value = true
    setTimeout(() => { copied.value = false }, 2500)
  } catch (e) {
    console.error('Failed to copy:', e)
  }
}
</script>

<style scoped>
.llm-copy {
  margin: 0 0 1.5rem 0;
}

.llm-copy button {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 14px;
  font-size: 13px;
  font-weight: 500;
  color: var(--vp-c-text-2);
  background: transparent;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.15s ease;
}

.llm-copy button:hover {
  color: var(--vp-c-text-1);
  border-color: var(--vp-c-text-3);
  background: var(--vp-c-bg-soft);
}

.llm-copy button.copied {
  color: #22c55e;
  border-color: #22c55e40;
  background: #22c55e08;
}
</style>
