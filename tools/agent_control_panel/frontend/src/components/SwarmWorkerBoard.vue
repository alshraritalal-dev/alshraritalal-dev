<script setup>
const props = defineProps({
  workers: { type: Array, default: () => [] },
  labels: { type: Object, required: true },
});

function statusClass(status) {
  const map = {
    idle: "border-slate-500/30 bg-slate-500/10 text-slate-200",
    planning: "border-violet-500/30 bg-violet-500/10 text-violet-200",
    working: "border-cyan-500/30 bg-cyan-500/10 text-cyan-200",
    verifying: "border-amber-500/30 bg-amber-500/10 text-amber-200",
    done: "border-emerald-500/30 bg-emerald-500/10 text-emerald-200",
    error: "border-rose-500/30 bg-rose-500/10 text-rose-200",
    restarting: "border-orange-500/30 bg-orange-500/10 text-orange-200",
  };
  return map[status] || map.idle;
}
</script>

<template>
  <section class="rounded-3xl border border-edge bg-panel px-4 py-4 shadow-panel">
    <div class="mb-4 flex items-center justify-between">
      <h2 class="text-sm font-semibold text-white">{{ labels.title }}</h2>
      <span class="status-chip">{{ workers.length }}</span>
    </div>

    <div class="grid gap-3 md:grid-cols-2">
      <div
        v-for="worker in workers"
        :key="worker.id"
        class="rounded-2xl border border-slate-800 bg-slate-950/70 px-4 py-4"
      >
        <div class="flex items-start justify-between gap-3">
          <div>
            <div class="text-sm font-semibold text-white">{{ worker.name }}</div>
            <div class="mt-1 text-xs uppercase tracking-wide text-mute">{{ worker.current_task || labels.waiting }}</div>
          </div>
          <span class="rounded-full border px-3 py-1 text-xs font-medium" :class="statusClass(worker.status)">
            {{ worker.status }}
          </span>
        </div>

        <div class="mt-3 text-sm text-slate-300">
          {{ worker.last_output || worker.last_error || labels.noOutput }}
        </div>

        <div class="mt-3 flex flex-wrap gap-2 text-xs text-slate-300">
          <span class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1">
            {{ labels.completed }}: {{ worker.completed_task_ids?.length || 0 }}
          </span>
          <span class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1">
            {{ labels.files }}: {{ worker.generated_paths?.length || 0 }}
          </span>
        </div>
      </div>

      <div v-if="!workers.length" class="rounded-2xl border border-slate-800 bg-slate-950/70 px-4 py-6 text-center text-sm text-mute md:col-span-2">
        {{ labels.empty }}
      </div>
    </div>
  </section>
</template>
