<script setup>
import JsonTreeNode from "./JsonTreeNode.vue";

defineProps({
  labels: { type: Object, required: true },
  globalState: { type: Object, default: () => ({}) },
  dependencyItems: { type: Array, default: () => [] },
  metrics: { type: Array, default: () => [] },
  form: { type: Object, required: true },
  statusLabel: { type: String, required: true },
  routeLabel: { type: String, required: true },
  notice: { type: String, default: "" },
  canHalt: { type: Boolean, default: false },
  canClear: { type: Boolean, default: false },
});

defineEmits(["halt", "clear", "restart"]);
</script>

<template>
  <aside class="inspector">
    <section class="inspector__panel">
      <div class="inspector__panel-title">{{ labels.runContext }}</div>
      <div class="inspector__field">
        <label>{{ labels.workspace }}</label>
        <input v-model="form.workspace_root" type="text" />
      </div>
      <div class="inspector__field">
        <label>{{ labels.attachedFile }}</label>
        <input v-model="form.attached_file_path" type="text" />
      </div>
      <div class="inspector__field">
        <label>{{ labels.model }}</label>
        <input v-model="form.model" type="text" />
      </div>
      <div class="inspector__route">
        <span>{{ routeLabel }}</span>
        <span>{{ statusLabel }}</span>
      </div>
      <div v-if="notice" class="inspector__notice">{{ notice }}</div>
    </section>

    <section class="inspector__panel">
      <div class="inspector__panel-title">{{ labels.globalRegistry }}</div>
      <div class="inspector__json">
        <JsonTreeNode label="swarm_state.json" :value="globalState" :depth="0" />
      </div>
    </section>

    <section class="inspector__panel">
      <div class="inspector__panel-title">{{ labels.activeTaskGraph }}</div>
      <div v-if="dependencyItems.length" class="inspector__list">
        <div v-for="item in dependencyItems" :key="item.key" class="inspector__list-item">
          <div class="inspector__list-title">{{ item.title }}</div>
          <div class="inspector__list-body">{{ item.body }}</div>
        </div>
      </div>
      <div v-else class="inspector__empty">{{ labels.noTasks }}</div>
    </section>

    <section class="inspector__panel">
      <div class="inspector__panel-title">{{ labels.resourceMonitor }}</div>
      <div class="inspector__metrics">
        <div v-for="metric in metrics" :key="metric.label" class="inspector__metric">
          <span>{{ metric.label }}</span>
          <strong>{{ metric.value }}</strong>
        </div>
      </div>
    </section>

    <section class="inspector__panel">
      <div class="inspector__panel-title">{{ labels.systemOverrides }}</div>
      <div class="inspector__actions">
        <button type="button" :disabled="!canHalt" @click="$emit('halt')">{{ labels.haltSwarm }}</button>
        <button type="button" :disabled="!canClear" @click="$emit('clear')">{{ labels.clearState }}</button>
        <button type="button" @click="$emit('restart')">{{ labels.restartUvicorn }}</button>
      </div>
    </section>
  </aside>
</template>

<style scoped>
.inspector {
  min-height: 0;
  overflow: auto;
  border-inline-start: 1px solid var(--border-dim);
  background: var(--bg-panel);
  padding: 18px;
  display: grid;
  align-content: start;
  gap: 16px;
}

.inspector__panel {
  background: var(--bg-surface);
  border: 1px solid var(--border-dim);
  border-radius: var(--radius-md);
  padding: 14px;
}

.inspector__panel-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-main);
  margin-bottom: 12px;
}

.inspector__field + .inspector__field {
  margin-top: 10px;
}

.inspector__field label {
  display: block;
  margin-bottom: 6px;
  font-size: 11px;
  color: var(--text-dim);
}

.inspector__field input {
  width: 100%;
  height: 36px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-dim);
  background: #050507;
  color: var(--text-main);
  padding: 0 10px;
  outline: none;
}

.inspector__field input:focus {
  border-color: var(--border-focus);
}

.inspector__route {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-top: 12px;
}

.inspector__route span,
.inspector__notice {
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  padding: 6px 10px;
  font-size: 11px;
  color: var(--text-mute);
}

.inspector__notice {
  margin-top: 12px;
  border-radius: var(--radius-sm);
}

.inspector__json {
  max-height: 280px;
  overflow: auto;
}

.inspector__list {
  display: grid;
  gap: 10px;
}

.inspector__list-item {
  padding: 10px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-dim);
  background: rgba(255, 255, 255, 0.02);
}

.inspector__list-title {
  font-size: 12px;
  color: var(--text-main);
  margin-bottom: 6px;
}

.inspector__list-body,
.inspector__empty {
  font-size: 12px;
  color: var(--text-mute);
}

.inspector__metrics {
  display: grid;
  gap: 10px;
}

.inspector__metric {
  display: flex;
  align-items: center;
  justify-content: space-between;
  border: 1px solid var(--border-dim);
  border-radius: var(--radius-sm);
  padding: 10px;
  font-size: 12px;
  color: var(--text-mute);
}

.inspector__metric strong {
  color: var(--text-main);
  font-family: var(--font-mono);
}

.inspector__actions {
  display: grid;
  gap: 8px;
}

.inspector__actions button {
  height: 36px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  background: transparent;
  color: var(--text-main);
  cursor: pointer;
}

.inspector__actions button:hover:not(:disabled) {
  border-color: var(--border-focus);
  background: var(--bg-hover);
}

.inspector__actions button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
</style>
