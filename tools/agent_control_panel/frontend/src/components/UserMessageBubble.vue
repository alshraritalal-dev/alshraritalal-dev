<script setup>
defineProps({
  message: { type: Object, required: true },
  label: { type: String, default: "You" },
});
</script>

<template>
  <div class="flex justify-end">
    <div class="max-w-[85%] rounded-3xl rounded-br-md bg-cyan-400 px-4 py-3 text-sm text-slate-950 shadow-[0_20px_50px_rgba(34,211,238,0.18)]">
      <div class="mb-1 text-[11px] font-semibold uppercase tracking-[0.22em] text-slate-800/70">{{ label }}</div>
      <div class="whitespace-pre-wrap leading-7">{{ message.content }}</div>
    </div>
  </div>
</template>
