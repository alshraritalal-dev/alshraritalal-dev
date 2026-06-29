<script setup>
import { computed, ref, watch } from "vue";

const props = defineProps({
  modelValue: { type: String, default: "" },
  placeholder: { type: String, required: true },
  submitLabel: { type: String, required: true },
  stopLabel: { type: String, required: true },
  hint: { type: String, required: true },
  charCountLabel: { type: String, required: true },
  canSend: { type: Boolean, default: false },
  swarmRunning: { type: Boolean, default: false },
});

const emit = defineEmits(["update:modelValue", "submit", "cancel"]);

const textareaRef = ref(null);

const charCount = computed(() => props.modelValue.length);

watch(
  () => props.modelValue,
  () => resize(),
  { immediate: true },
);

function resize() {
  if (!textareaRef.value) return;
  textareaRef.value.style.height = "0px";
  textareaRef.value.style.height = `${Math.min(textareaRef.value.scrollHeight, 152)}px`;
}

function handleKeydown(event) {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    if (props.swarmRunning) emit("cancel");
    else emit("submit");
  }
}
</script>

<template>
  <div class="console-wrap">
    <div class="console">
      <textarea
        ref="textareaRef"
        :value="modelValue"
        :placeholder="placeholder"
        rows="1"
        class="console__textarea"
        @input="$emit('update:modelValue', $event.target.value)"
        @keydown="handleKeydown"
      />

      <div class="console__footer">
        <div class="console__meta">
          <div>{{ hint }}</div>
          <div>{{ charCount }} {{ charCountLabel }}</div>
        </div>

        <div class="console__actions">
          <div class="console__tools" aria-hidden="true">
            <span class="console__tool">&gt;_</span>
            <span class="console__tool">{ }</span>
            <span class="console__tool">[]</span>
          </div>

          <button
            type="button"
            class="console__submit"
            :class="{ 'console__submit--stop': swarmRunning }"
            :disabled="!swarmRunning && !canSend"
            @click="swarmRunning ? $emit('cancel') : $emit('submit')"
          >
            <span v-if="swarmRunning">■</span>
            <span v-else>↗</span>
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.console-wrap {
  padding: 16px 24px 20px;
}

.console {
  background: var(--bg-surface);
  border: 1px solid var(--border-dim);
  border-radius: var(--radius-lg);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.2);
  padding: 16px;
}

.console__textarea {
  width: 100%;
  min-height: 26px;
  max-height: 152px;
  border: 0;
  resize: none;
  outline: none;
  background: transparent;
  color: var(--text-main);
  font-size: 14px;
  line-height: 1.6;
  font-family: inherit;
}

.console__textarea::placeholder {
  color: var(--text-dim);
}

.console__footer {
  margin-top: 12px;
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 12px;
}

.console__meta {
  font-size: 11px;
  color: var(--text-mute);
  display: grid;
  gap: 4px;
}

.console__actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.console__tools {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.console__tool {
  min-width: 28px;
  height: 28px;
  padding: 0 8px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--text-mute);
  font-size: 11px;
  font-family: var(--font-mono);
}

.console__submit {
  width: 32px;
  height: 32px;
  border-radius: 999px;
  border: 0;
  background: var(--accent-main);
  color: white;
  display: grid;
  place-items: center;
  cursor: pointer;
}

.console__submit--stop {
  border-radius: 8px;
  background: var(--danger);
}

.console__submit:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
</style>
