<script setup>
import { computed } from "vue";

const props = defineProps({
  swarm: { type: Object, default: null },
  labels: { type: Object, required: true },
});

const taskCounts = computed(() => props.swarm?.metrics?.taskCounts || {});
const workerCounts = computed(() => props.swarm?.metrics?.workerCounts || {});

const taskChips = computed(() => [
  { key: "pending", label: props.labels.pending, value: taskCounts.value.pending || 0, accent: "text-amber-300" },
  { key: "working", label: props.labels.working, value: taskCounts.value.working || 0, accent: "text-cyan-300" },
  { key: "done", label: props.labels.done, value: taskCounts.value.done || 0, accent: "text-emerald-300" },
  { key: "error", label: props.labels.error, value: taskCounts.value.error || 0, accent: "text-rose-300" },
]);

const workerChips = computed(() =>
  Object.entries(workerCounts.value).map(([key, value]) => ({
    key,
    label: key,
    value,
  }))
);
</script>

<template>
  <section class="rounded-3xl border border-edge bg-panel px-4 py-4 shadow-panel">
    <div class="flex flex-wrap items-start justify-between gap-3">
      <div>
        <div class="text-xs font-semibold uppercase tracking-[0.22em] text-mute">{{ labels.title }}</div>
        <h2 class="mt-2 text-lg font-semibold text-white">{{ swarm?.manager_summary || labels.idle }}</h2>
        <p class="mt-2 text-sm leading-7 text-slate-300">{{ swarm?.manager_reply || labels.subtitle }}</p>
      </div>
      <div class="status-chip">
        {{ labels.workers }}: <strong>{{ swarm?.metrics?.totalWorkers || 0 }}</strong>
      </div>
    </div>

    <div class="mt-4 grid gap-3 md:grid-cols-4">
      <div
        v-for="chip in taskChips"
        :key="chip.key"
        class="rounded-2xl border border-slate-800 bg-slate-950/70 px-3 py-3"
      >
        <div class="text-xs uppercase tracking-wide text-mute">{{ chip.label }}</div>
        <div class="mt-2 text-2xl font-semibold" :class="chip.accent">{{ chip.value }}</div>
      </div>
    </div>

    <div v-if="workerChips.length" class="mt-4 rounded-2xl border border-slate-800 bg-slate-950/70 px-3 py-3">
      <div class="text-xs uppercase tracking-wide text-mute">{{ labels.workerStates }}</div>
      <div class="mt-3 flex flex-wrap gap-2">
        <span
          v-for="chip in workerChips"
          :key="chip.key"
          class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1 text-xs text-slate-200"
        >
          {{ chip.label }}: {{ chip.value }}
        </span>
      </div>
    </div>
  </section>
</template>
