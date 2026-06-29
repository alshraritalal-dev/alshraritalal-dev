<script setup>
const props = defineProps({
  managerLabel: { type: String, required: true },
  workers: { type: Array, default: () => [] },
  labels: { type: Object, required: true },
});

function workerClass(worker) {
  const status = String(worker.status || "idle");
  if (["working", "planning", "verifying"].includes(status)) return "worker-pill worker-pill--active";
  if (["error", "offline"].includes(status)) return "worker-pill worker-pill--failed";
  return "worker-pill worker-pill--idle";
}
</script>

<template>
  <div class="statusbar">
    <div class="statusbar__manager">{{ labels.managerStatus }}: <strong>{{ managerLabel }}</strong></div>
    <div class="statusbar__workers">
      <template v-if="workers.length">
        <span v-for="worker in workers" :key="worker.id" :class="workerClass(worker)">
          {{ worker.name }}
        </span>
      </template>
      <span v-else class="statusbar__empty">{{ labels.noWorkers }}</span>
    </div>
  </div>
</template>

<style scoped>
.statusbar {
  min-height: 48px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 0 24px;
  border-bottom: 1px solid var(--border-dim);
  background: rgba(6, 6, 8, 0.85);
}

.statusbar__manager {
  font-size: 12px;
  color: var(--text-mute);
}

.statusbar__manager strong {
  color: var(--text-main);
  font-weight: 500;
}

.statusbar__workers {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.statusbar__empty {
  font-size: 11px;
  color: var(--text-dim);
}

.worker-pill {
  height: 26px;
  display: inline-flex;
  align-items: center;
  padding: 0 10px;
  border-radius: 999px;
  font-size: 11px;
}

.worker-pill--idle {
  color: var(--text-mute);
  border: 1px dashed var(--text-dim);
}

.worker-pill--active {
  color: var(--accent-main);
  background: var(--accent-glow);
  border: 1px solid var(--accent-main);
  animation: pulse 1.8s ease-in-out infinite;
}

.worker-pill--failed {
  color: var(--danger);
  border: 1px solid rgba(239, 68, 68, 0.45);
  background: rgba(239, 68, 68, 0.1);
}

@keyframes pulse {
  0%, 100% { box-shadow: 0 0 0 0 rgba(99, 102, 241, 0); }
  50% { box-shadow: 0 0 18px 0 rgba(99, 102, 241, 0.16); }
}
</style>
