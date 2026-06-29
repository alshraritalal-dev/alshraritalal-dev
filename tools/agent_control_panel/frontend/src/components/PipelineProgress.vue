<script setup>
const props = defineProps({
  phases: { type: Array, default: () => [] },
  activePhase: { type: Number, default: 0 },
  completedPhases: { type: Array, default: () => [] },
});

function isCompleted(phase) {
  return props.completedPhases.includes(phase);
}

function isActive(phase) {
  return props.activePhase === phase;
}
</script>

<template>
  <div class="flex flex-wrap items-center gap-2">
    <template v-for="(phase, index) in phases" :key="phase.number">
      <div class="flex items-center gap-2">
        <div
          class="pipeline-pill"
          :class="[
            isCompleted(phase.number)
              ? 'pipeline-pill-complete'
              : isActive(phase.number)
                ? phase.activeClass
                : 'pipeline-pill-idle',
          ]"
        >
          <span class="text-sm">
            {{ isCompleted(phase.number) ? "✓" : phase.icon }}
          </span>
          <span class="truncate">{{ phase.label }}</span>
        </div>

        <div v-if="index < phases.length - 1" class="pipeline-link" />
      </div>
    </template>
  </div>
</template>
