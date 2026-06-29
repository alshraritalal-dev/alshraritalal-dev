<script setup>
import { computed } from "vue";

const props = defineProps({
  title: { type: String, required: true },
  logs: { type: Array, default: () => [] },
  buffer: { type: String, default: "" },
  emptyText: { type: String, default: "No logs yet." },
  accent: { type: String, default: "cyan" },
});

const accentClasses = computed(() => {
  const map = {
    emerald: "border-emerald-500/40 bg-emerald-500/10 text-emerald-300",
    cyan: "border-cyan-500/40 bg-cyan-500/10 text-cyan-300",
    amber: "border-amber-500/40 bg-amber-500/10 text-amber-300",
  };
  return map[props.accent] || map.cyan;
});
</script>

<template>
  <section class="flex min-h-[360px] flex-col rounded-lg border border-edge bg-panel shadow-panel">
    <header class="flex items-center justify-between border-b border-edge px-4 py-3">
      <div class="text-sm font-semibold tracking-wide text-ink">{{ title }}</div>
      <span class="rounded-full border px-2 py-1 text-[11px] font-medium" :class="accentClasses">
        {{ logs.length }}
      </span>
    </header>

    <div class="flex-1 overflow-auto px-4 py-3 font-mono text-xs leading-6 text-slate-200">
      <template v-if="logs.length || buffer">
        <div
          v-for="entry in logs"
          :key="entry.id"
          class="mb-3 rounded-md border border-slate-800 bg-slate-950/70 px-3 py-2"
        >
          <div class="mb-1 flex items-center justify-between text-[10px] uppercase tracking-wide text-mute">
            <span>{{ entry.kind }}</span>
            <span>{{ new Date(entry.timestamp).toLocaleTimeString() }}</span>
          </div>
          <pre class="whitespace-pre-wrap break-words">{{ entry.message }}</pre>
        </div>

        <div v-if="buffer" class="rounded-md border border-cyan-900 bg-slate-950/80 px-3 py-2">
          <div class="mb-1 text-[10px] uppercase tracking-wide text-cyan-400">stream</div>
          <pre class="whitespace-pre-wrap break-words text-cyan-100">{{ buffer }}</pre>
        </div>
      </template>

      <div v-else class="flex h-full items-center justify-center text-center text-sm text-mute">
        {{ emptyText }}
      </div>
    </div>
  </section>
</template>
