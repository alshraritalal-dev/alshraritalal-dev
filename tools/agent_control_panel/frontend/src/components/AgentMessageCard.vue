<script setup>
import { computed, ref } from "vue";
import MarkdownBlock from "./MarkdownBlock.vue";

const props = defineProps({
  message: { type: Object, default: null },
  title: { type: String, required: true },
  icon: { type: String, required: true },
  phase: { type: Number, default: null },
  accentClass: { type: String, required: true },
  isStreaming: { type: Boolean, default: false },
  liveBuffer: { type: String, default: "" },
  copyLabel: { type: String, default: "Copy" },
  phaseLabel: { type: String, default: "Phase" },
});

const open = ref(true);

const durationLabel = computed(() => {
  const duration = props.message?.metadata?.durationMs;
  if (!duration) return null;
  return duration < 1000 ? `${duration} ms` : `${(duration / 1000).toFixed(1)} s`;
});

function copyCard() {
  const content = props.isStreaming ? props.liveBuffer : props.message?.content || "";
  navigator.clipboard.writeText(content);
}
</script>

<template>
  <article class="glass-card overflow-hidden rounded-3xl border border-edge">
    <header
      class="flex cursor-pointer flex-wrap items-center justify-between gap-3 border-b px-4 py-3"
      :class="accentClass"
      @click="open = !open"
    >
      <div class="flex items-center gap-3">
        <div class="flex h-10 w-10 items-center justify-center rounded-2xl bg-black/20 text-lg text-white shadow-inner">
          {{ icon }}
        </div>
        <div>
          <div class="text-sm font-semibold text-white">{{ title }}</div>
          <div v-if="phase !== null" class="text-[11px] uppercase tracking-[0.22em] text-white/70">{{ phaseLabel }} {{ phase }}</div>
        </div>
      </div>

      <div class="flex items-center gap-2">
        <span v-if="durationLabel" class="rounded-full border border-white/15 bg-black/20 px-2 py-1 text-[11px] text-white/80">
          {{ durationLabel }}
        </span>
        <button
          type="button"
          class="rounded-xl border border-white/15 bg-black/20 px-3 py-1.5 text-xs font-semibold text-white/90 transition hover:bg-black/30"
          @click.stop="copyCard"
        >
          {{ copyLabel }}
        </button>
      </div>
    </header>

    <div v-show="open" class="bg-slate-950/85 px-4 py-4">
      <div v-if="isStreaming" class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-4 font-mono text-sm text-slate-100">
        <pre class="whitespace-pre-wrap break-words">{{ liveBuffer }}<span class="typing-cursor">|</span></pre>
      </div>
      <MarkdownBlock v-else :source="message?.content || ''" />
    </div>
  </article>
</template>
