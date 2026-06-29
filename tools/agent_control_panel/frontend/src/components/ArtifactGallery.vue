<script setup>
const props = defineProps({
  artifacts: { type: Array, default: () => [] },
  title: { type: String, default: "Artifacts" },
  emptyText: { type: String, default: "No generated files yet." },
  apiBase: { type: String, required: true },
  labels: { type: Object, required: true },
});

function toUrl(value) {
  if (!value) return "#";
  if (value.startsWith("http://") || value.startsWith("https://")) {
    return value;
  }
  return `${props.apiBase}${value}`;
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function artifactIcon(artifact) {
  if (artifact.preview_kind === "image") return "🖼️";
  if (artifact.file_extension === "pdf") return "📄";
  if (artifact.file_extension === "docx") return "📝";
  if (artifact.file_extension === "zip") return "🗜️";
  if (artifact.preview_kind === "text") return "💻";
  return "📦";
}
</script>

<template>
  <section class="glass-card overflow-hidden rounded-3xl border border-slate-800">
    <header class="border-b border-slate-800 bg-slate-950/70 px-4 py-3">
      <div class="text-sm font-semibold text-white">{{ title }}</div>
    </header>

    <div class="bg-slate-950/85 px-4 py-4">
      <div v-if="!artifacts.length" class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-4 text-sm text-mute">
        {{ emptyText }}
      </div>

      <div v-else class="grid gap-4">
        <article
          v-for="artifact in artifacts"
          :key="artifact.id"
          class="rounded-3xl border border-slate-800 bg-slate-950/90 p-4"
        >
          <div class="flex flex-wrap items-start justify-between gap-3">
            <div class="min-w-0 flex-1">
              <div class="flex items-center gap-3">
                <div class="flex h-11 w-11 items-center justify-center rounded-2xl border border-slate-800 bg-slate-900 text-lg">
                  {{ artifactIcon(artifact) }}
                </div>
                <div class="min-w-0">
                  <div class="truncate text-sm font-semibold text-white">{{ artifact.file_name }}</div>
                  <div class="mt-1 flex flex-wrap items-center gap-2 text-xs text-mute">
                    <span>{{ formatBytes(artifact.file_size) }}</span>
                    <span>•</span>
                    <span>{{ labels.producedBy }} {{ artifact.generated_by_agent }}</span>
                  </div>
                </div>
              </div>

              <div class="mt-3 rounded-2xl border border-slate-800 bg-black/20 px-3 py-3 text-xs text-slate-300">
                <div class="font-semibold text-slate-100">{{ labels.absolutePath }}</div>
                <div class="mt-1 break-all">{{ artifact.absolute_path }}</div>
              </div>
            </div>

            <div class="flex items-center gap-2">
              <a
                class="action-btn action-btn-secondary"
                :href="toUrl(artifact.open_url)"
                target="_blank"
                rel="noopener noreferrer"
              >
                {{ labels.open }}
              </a>
              <a
                class="action-btn action-btn-secondary"
                :href="toUrl(artifact.download_url)"
                target="_blank"
                rel="noopener noreferrer"
              >
                {{ labels.download }}
              </a>
            </div>
          </div>

          <div v-if="artifact.preview_kind === 'image'" class="mt-4 space-y-3">
            <img
              :src="toUrl(artifact.open_url)"
              :alt="artifact.file_name"
              class="max-h-[420px] w-full rounded-2xl border border-slate-800 object-contain bg-slate-950"
            />
            <div v-if="artifact.description" class="rounded-2xl border border-slate-800 bg-slate-950 px-3 py-3 text-sm text-slate-200">
              {{ artifact.description }}
            </div>
          </div>

          <div
            v-else-if="artifact.preview_text"
            class="mt-4 overflow-auto rounded-2xl border border-slate-800 bg-slate-950 px-3 py-3"
          >
            <pre class="whitespace-pre-wrap break-words font-mono text-xs leading-6 text-slate-200">{{ artifact.preview_text }}</pre>
          </div>

          <div
            v-else-if="artifact.description"
            class="mt-4 rounded-2xl border border-slate-800 bg-slate-950 px-3 py-3 text-sm text-slate-200"
          >
            {{ artifact.description }}
          </div>
        </article>
      </div>
    </div>
  </section>
</template>
