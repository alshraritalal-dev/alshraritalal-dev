<script setup>
import { computed, ref } from "vue";
import MarkdownBlock from "./MarkdownBlock.vue";

const props = defineProps({
  card: { type: Object, required: true },
  labels: { type: Object, required: true },
});

const emit = defineEmits(["retry"]);
const showDiff = ref(false);

const canOpen = computed(() => Boolean(props.card.primaryArtifact?.open_url));
const canDiff = computed(() => Boolean(props.card.diffPreview));
const canRetry = computed(() => ["error", "cancelled"].includes(String(props.card.status)));

const terminalText = computed(() => {
  const logs = props.card.logs || [];
  const lines = logs.map((entry) => {
    const prefix = entry.kind === "stderr" ? "[stderr]" : entry.kind === "stdout" ? "[stdout]" : `[${entry.kind}]`;
    return `${prefix} ${entry.message}`;
  });
  if (props.card.reply && !lines.length) lines.push(props.card.reply);
  if (props.card.error) lines.push(`[error] ${props.card.error}`);
  return lines.join("\n").trim();
});

function openFile() {
  if (!canOpen.value) return;
  window.open(props.card.primaryArtifact.open_url, "_blank", "noopener");
}
</script>

<template>
  <article class="task-card">
    <header class="task-card__header">
      <div class="task-card__heading">
        <div class="task-card__worker">[{{ card.workerLabel }}]</div>
        <div class="task-card__title">{{ card.title }}</div>
      </div>
      <div class="task-card__meta">
        <span class="task-card__duration">{{ card.durationLabel }}</span>
        <span class="task-card__status" :class="`task-card__status--${card.statusTone}`">{{ card.statusLabel }}</span>
      </div>
    </header>

    <div class="task-card__body">
      <div class="task-card__terminal">
        <div v-if="terminalText" class="task-card__terminal-lines">
          <pre>{{ terminalText }}</pre>
        </div>
        <div v-else class="task-card__terminal-empty">{{ labels.noTaskOutput }}</div>
      </div>

      <div v-if="card.summary" class="task-card__summary">
        <MarkdownBlock :source="card.summary" :copy-label="labels.copy" />
      </div>

      <div v-if="card.primaryArtifact" class="task-card__artifact">
        <div class="task-card__artifact-label">{{ labels.taskArtifacts }}</div>
        <div class="task-card__artifact-name">{{ card.primaryArtifact.file_name }}</div>
      </div>

      <div v-if="showDiff && canDiff" class="task-card__diff">
        <pre>{{ card.diffPreview }}</pre>
      </div>
    </div>

    <footer class="task-card__actions">
      <button type="button" class="task-card__action" :disabled="!canOpen" @click="openFile">{{ labels.openFile }}</button>
      <button type="button" class="task-card__action" :disabled="!canDiff" @click="showDiff = !showDiff">{{ labels.viewDiff }}</button>
      <button type="button" class="task-card__action task-card__action--primary" :disabled="!canRetry" @click="$emit('retry', card.id)">{{ labels.retry }}</button>
    </footer>
  </article>
</template>

<style scoped>
.task-card {
  background: var(--bg-panel);
  border: 1px solid var(--border-dim);
  border-radius: var(--radius-md);
  overflow: hidden;
}

.task-card__header,
.task-card__actions {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 12px 14px;
}

.task-card__header {
  border-bottom: 1px solid var(--border-dim);
}

.task-card__heading {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.task-card__worker {
  color: var(--text-mute);
  font-size: 12px;
  font-family: var(--font-mono);
}

.task-card__title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-main);
}

.task-card__meta {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 11px;
}

.task-card__duration {
  color: var(--text-mute);
  font-family: var(--font-mono);
}

.task-card__status {
  padding: 4px 8px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
}

.task-card__status--working {
  color: var(--warning);
  border-color: rgba(245, 158, 11, 0.3);
}

.task-card__status--success {
  color: var(--success);
  border-color: rgba(16, 185, 129, 0.28);
}

.task-card__status--error {
  color: var(--danger);
  border-color: rgba(239, 68, 68, 0.28);
}

.task-card__status--idle {
  color: var(--text-mute);
}

.task-card__body {
  padding: 14px;
  display: grid;
  gap: 12px;
}

.task-card__terminal {
  background: #000;
  border-radius: var(--radius-sm);
  border: 1px solid rgba(255, 255, 255, 0.04);
  padding: 12px;
  min-height: 96px;
  color: #d5d7e2;
  font-family: var(--font-mono);
  font-size: 11px;
}

.task-card__terminal-lines pre,
.task-card__diff pre {
  white-space: pre-wrap;
  word-break: break-word;
}

.task-card__terminal-empty {
  color: #72758c;
}

.task-card__summary,
.task-card__artifact,
.task-card__diff {
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--border-dim);
  border-radius: var(--radius-sm);
  padding: 12px;
}

.task-card__artifact-label {
  font-size: 11px;
  color: var(--text-dim);
  margin-bottom: 6px;
}

.task-card__artifact-name {
  font-size: 13px;
  color: var(--text-main);
}

.task-card__diff {
  background: rgba(16, 185, 129, 0.06);
}

.task-card__actions {
  border-top: 1px solid var(--border-dim);
  justify-content: flex-end;
}

.task-card__action {
  min-width: 90px;
  height: 32px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  background: transparent;
  color: var(--text-mute);
  cursor: pointer;
  padding: 0 12px;
}

.task-card__action:hover:not(:disabled) {
  border-color: var(--border-focus);
  color: var(--text-main);
}

.task-card__action--primary {
  color: var(--accent-main);
  border-color: rgba(99, 102, 241, 0.28);
}

.task-card__action:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
</style>
