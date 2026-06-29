<script setup>
const props = defineProps({
  tasks: { type: Array, default: () => [] },
  labels: { type: Object, required: true },
});

function statusClass(status) {
  const map = {
    pending: "border-amber-500/30 bg-amber-500/10 text-amber-200",
    working: "border-cyan-500/30 bg-cyan-500/10 text-cyan-200",
    done: "border-emerald-500/30 bg-emerald-500/10 text-emerald-200",
    error: "border-rose-500/30 bg-rose-500/10 text-rose-200",
    blocked: "border-orange-500/30 bg-orange-500/10 text-orange-200",
    cancelled: "border-slate-500/30 bg-slate-500/10 text-slate-300",
  };
  return map[status] || map.pending;
}
</script>

<template>
  <section class="rounded-3xl border border-edge bg-panel px-4 py-4 shadow-panel">
    <div class="mb-4 flex items-center justify-between">
      <h2 class="text-sm font-semibold text-white">{{ labels.title }}</h2>
      <span class="status-chip">{{ tasks.length }}</span>
    </div>

    <div class="grid gap-3">
      <div
        v-for="task in tasks"
        :key="task.id"
        class="rounded-2xl border border-slate-800 bg-slate-950/70 px-4 py-4"
      >
        <div class="flex flex-wrap items-start justify-between gap-3">
          <div class="min-w-0">
            <div class="text-sm font-semibold text-white">{{ task.title }}</div>
            <div class="mt-1 text-xs uppercase tracking-wide text-mute">
              {{ task.task_type }} <span v-if="task.assigned_worker_name">• {{ task.assigned_worker_name }}</span>
            </div>
          </div>
          <span class="rounded-full border px-3 py-1 text-xs font-medium" :class="statusClass(task.status)">
            {{ task.status }}
          </span>
        </div>

        <div class="mt-3 text-sm leading-7 text-slate-300">
          {{ task.output_summary || task.prompt }}
        </div>

        <div class="mt-3 flex flex-wrap gap-2 text-xs text-slate-300">
          <span class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1">
            {{ labels.attempts }}: {{ task.attempts }}/{{ task.max_attempts }}
          </span>
          <span class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1">
            {{ labels.dependencies }}: {{ task.dependencies?.length || 0 }}
          </span>
          <span v-if="task.generated_paths?.length" class="rounded-full border border-slate-700 bg-slate-900 px-3 py-1">
            {{ labels.files }}: {{ task.generated_paths.length }}
          </span>
        </div>

        <div v-if="task.error" class="mt-3 rounded-xl border border-rose-500/30 bg-rose-500/10 px-3 py-3 text-sm text-rose-200">
          {{ task.error }}
        </div>
      </div>

      <div v-if="!tasks.length" class="rounded-2xl border border-slate-800 bg-slate-950/70 px-4 py-6 text-center text-sm text-mute">
        {{ labels.empty }}
      </div>
    </div>
  </section>
</template>
