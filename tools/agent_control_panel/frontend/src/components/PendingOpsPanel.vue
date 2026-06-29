<script setup>
const props = defineProps({
  title: { type: String, required: true },
  items: { type: Array, default: () => [] },
  emptyText: { type: String, required: true },
  kind: { type: String, default: "file" },
  statusLabel: { type: String, default: "Status" },
  approvalLabel: { type: String, default: "Requires approval" },
});

function previewContent(item) {
  if (props.kind === "command") return item.command;
  if (!item.content) return item.path;
  return `${item.path}\n\n${item.content.slice(0, 240)}${item.content.length > 240 ? "\n..." : ""}`;
}
</script>

<template>
  <section class="rounded-lg border border-edge bg-panel shadow-panel">
    <header class="border-b border-edge px-4 py-3">
      <h3 class="text-sm font-semibold text-ink">{{ title }}</h3>
    </header>

    <div class="max-h-[320px] overflow-auto px-4 py-3">
      <div v-if="items.length" class="space-y-3">
        <article
          v-for="item in items"
          :key="item.id"
          class="rounded-md border border-slate-800 bg-slate-950/70 px-3 py-3"
        >
          <div class="mb-2 flex flex-wrap items-center gap-2 text-xs">
            <span class="rounded-full border border-cyan-500/30 bg-cyan-500/10 px-2 py-1 text-cyan-200">
              {{ props.kind === "command" ? "command" : item.kind }}
            </span>
            <span class="rounded-full border border-slate-700 px-2 py-1 text-slate-300">
              {{ statusLabel }}: {{ item.status }}
            </span>
            <span
              v-if="item.requires_approval"
              class="rounded-full border border-amber-500/30 bg-amber-500/10 px-2 py-1 text-amber-200"
            >
              {{ approvalLabel }}
            </span>
          </div>
          <div class="mb-2 text-xs text-mute">{{ item.reason || "No reason provided." }}</div>
          <pre class="overflow-auto whitespace-pre-wrap break-words text-xs text-slate-100">{{ previewContent(item) }}</pre>
          <div v-if="item.error" class="mt-2 text-xs text-rose-300">{{ item.error }}</div>
        </article>
      </div>
      <div v-else class="flex min-h-[120px] items-center justify-center text-sm text-mute">
        {{ emptyText }}
      </div>
    </div>
  </section>
</template>
