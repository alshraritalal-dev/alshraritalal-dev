<script setup>
import { computed } from "vue";

defineOptions({ name: "JsonTreeNode" });

const props = defineProps({
  label: { type: String, default: "" },
  value: { type: null, default: null },
  depth: { type: Number, default: 0 },
});

const isObject = computed(() => props.value && typeof props.value === "object");
const isArray = computed(() => Array.isArray(props.value));
const entries = computed(() => {
  if (Array.isArray(props.value)) return props.value.map((item, index) => [String(index), item]);
  if (props.value && typeof props.value === "object") return Object.entries(props.value);
  return [];
});
</script>

<template>
  <div class="json-node">
    <template v-if="isObject">
      <details class="json-node__details" :open="depth < 1">
        <summary class="json-node__summary">
          <span class="json-node__label">{{ label || (isArray ? "[]" : "{}") }}</span>
          <span class="json-node__badge">{{ isArray ? `${entries.length} items` : `${entries.length} keys` }}</span>
        </summary>
        <div class="json-node__children">
          <JsonTreeNode v-for="[childKey, childValue] in entries" :key="childKey" :label="childKey" :value="childValue" :depth="depth + 1" />
        </div>
      </details>
    </template>
    <template v-else>
      <div class="json-node__leaf">
        <span class="json-node__label">{{ label }}</span>
        <span class="json-node__value">{{ String(value) }}</span>
      </div>
    </template>
  </div>
</template>

<style scoped>
.json-node {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-mute);
}

.json-node__details {
  padding-inline-start: 8px;
}

.json-node__summary,
.json-node__leaf {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 5px 0;
}

.json-node__summary {
  cursor: pointer;
}

.json-node__children {
  border-inline-start: 1px solid var(--border-dim);
  margin-inline-start: 6px;
  padding-inline-start: 8px;
}

.json-node__label {
  color: var(--text-main);
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
}

.json-node__badge,
.json-node__value {
  color: var(--text-dim);
  text-align: end;
  word-break: break-word;
}
</style>
