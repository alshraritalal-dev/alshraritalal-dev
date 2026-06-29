<script setup>
import MarkdownBlock from "./MarkdownBlock.vue";

defineProps({
  message: { type: Object, required: true },
  label: { type: String, required: true },
  isUser: { type: Boolean, default: false },
  isStreaming: { type: Boolean, default: false },
  liveBuffer: { type: String, default: "" },
  rtl: { type: Boolean, default: false },
  copyLabel: { type: String, default: "Copy" },
});
</script>

<template>
  <div class="bubble-row" :class="{ 'bubble-row--user': isUser }">
    <div class="bubble-meta">{{ label }}</div>
    <div class="bubble" :class="[{ 'bubble--user': isUser, 'bubble--rtl-user': isUser && rtl }]">
      <template v-if="isStreaming">
        <pre class="bubble__stream">{{ liveBuffer }}<span class="bubble__cursor">|</span></pre>
      </template>
      <template v-else-if="isUser">
        <div class="bubble__plain">{{ message.content }}</div>
      </template>
      <template v-else>
        <MarkdownBlock :source="message.content" :copy-label="copyLabel" />
      </template>
    </div>
  </div>
</template>

<style scoped>
.bubble-row {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 6px;
}

.bubble-row--user {
  align-items: flex-end;
}

.bubble-meta {
  font-size: 11px;
  color: var(--text-dim);
}

.bubble {
  max-width: min(820px, 82%);
  padding: 14px 16px;
  border-radius: 12px 12px 12px 2px;
  background: var(--bg-surface);
  border: 1px solid var(--border-dim);
  box-shadow: 0 12px 36px rgba(0, 0, 0, 0.18);
}

.bubble--user {
  background: var(--accent-main);
  color: white;
  border-color: transparent;
  border-radius: 12px 12px 2px 12px;
}

.bubble--rtl-user {
  border-radius: 12px 12px 12px 2px;
}

.bubble__plain,
.bubble__stream {
  white-space: pre-wrap;
  word-break: break-word;
  font-size: 14px;
  line-height: 1.65;
}

.bubble__stream {
  font-family: var(--font-sans);
}

.bubble__cursor {
  display: inline-block;
  animation: blink 0.9s step-end infinite;
}

@keyframes blink {
  0%, 45% { opacity: 1; }
  46%, 100% { opacity: 0; }
}
</style>
