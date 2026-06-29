<script setup>
import { computed, nextTick, onMounted, ref, watch } from "vue";
import { marked } from "marked";
import hljs from "highlight.js";

const props = defineProps({
  source: { type: String, default: "" },
  copyLabel: { type: String, default: "Copy" },
});

const root = ref(null);

marked.setOptions({
  gfm: true,
  breaks: true,
});

const html = computed(() => marked.parse(props.source || ""));

function enhanceCodeBlocks() {
  if (!root.value) return;

  root.value.querySelectorAll("pre code").forEach((block) => {
    hljs.highlightElement(block);
  });

  root.value.querySelectorAll("pre").forEach((pre) => {
    if (pre.querySelector(".code-copy-btn")) return;

    const code = pre.querySelector("code");
    const className = code?.className || "";
    const language = className.replace("language-", "").trim() || "text";

    const chrome = document.createElement("div");
    chrome.className = "code-chrome";

    const label = document.createElement("span");
    label.className = "code-language-pill";
    label.textContent = language;

    const button = document.createElement("button");
    button.type = "button";
    button.className = "code-copy-btn";
    button.textContent = props.copyLabel;
    button.addEventListener("click", async () => {
      const text = code?.innerText || pre.innerText || "";
      await navigator.clipboard.writeText(text);
      const previous = button.textContent;
      button.textContent = "OK";
      setTimeout(() => {
        button.textContent = previous;
      }, 900);
    });

    chrome.appendChild(label);
    chrome.appendChild(button);
    pre.prepend(chrome);
  });
}

watch(html, async () => {
  await nextTick();
  enhanceCodeBlocks();
});

onMounted(async () => {
  await nextTick();
  enhanceCodeBlocks();
});
</script>

<template>
  <div ref="root" class="markdown-block" v-html="html" />
</template>

<style scoped>
.markdown-block {
  color: var(--text-main);
  line-height: 1.75;
  font-size: 13px;
}

.markdown-block :deep(> :first-child) {
  margin-top: 0;
}

.markdown-block :deep(> :last-child) {
  margin-bottom: 0;
}

.markdown-block :deep(h1),
.markdown-block :deep(h2),
.markdown-block :deep(h3),
.markdown-block :deep(h4) {
  margin: 1rem 0 0.55rem;
  color: var(--text-main);
  font-weight: 600;
}

.markdown-block :deep(h1) { font-size: 1.15rem; }
.markdown-block :deep(h2) { font-size: 1rem; }
.markdown-block :deep(h3) { font-size: 0.92rem; }

.markdown-block :deep(p),
.markdown-block :deep(ul),
.markdown-block :deep(ol),
.markdown-block :deep(blockquote) {
  margin: 0.7rem 0;
}

.markdown-block :deep(ul),
.markdown-block :deep(ol) {
  padding-inline-start: 1.2rem;
}

.markdown-block :deep(li + li) {
  margin-top: 0.35rem;
}

.markdown-block :deep(a) {
  color: #9ca3ff;
  text-decoration: none;
}

.markdown-block :deep(a:hover) {
  text-decoration: underline;
}

.markdown-block :deep(blockquote) {
  border-inline-start: 2px solid var(--accent-main);
  padding-inline-start: 0.85rem;
  color: var(--text-mute);
}

.markdown-block :deep(code:not(pre code)) {
  padding: 0.15rem 0.4rem;
  border-radius: 6px;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid var(--border-dim);
  font-family: var(--font-mono);
  font-size: 0.88em;
}

.markdown-block :deep(pre) {
  margin: 0.9rem 0;
  padding: 0.85rem;
  border-radius: var(--radius-md);
  border: 1px solid var(--border-dim);
  background: #050507;
  overflow-x: auto;
}

.markdown-block :deep(pre code) {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-main);
}

.markdown-block :deep(.code-chrome) {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.5rem;
  margin-bottom: 0.75rem;
}

.markdown-block :deep(.code-language-pill) {
  padding: 0.18rem 0.45rem;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  color: var(--text-mute);
  font-size: 10px;
  font-family: var(--font-mono);
}

.markdown-block :deep(.code-copy-btn) {
  border: 1px solid var(--border-dim);
  background: transparent;
  color: var(--text-mute);
  border-radius: 999px;
  padding: 0.18rem 0.55rem;
  font-size: 10px;
  cursor: pointer;
}

.markdown-block :deep(.code-copy-btn:hover) {
  color: var(--text-main);
  border-color: var(--border-focus);
}
</style>
