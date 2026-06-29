<script setup>
import { computed } from "vue";
import ChatBubble from "./ChatBubble.vue";
import WorkerTaskCard from "./WorkerTaskCard.vue";

const props = defineProps({
  route: { type: String, required: true },
  messages: { type: Array, default: () => [] },
  taskCards: { type: Array, default: () => [] },
  isArabic: { type: Boolean, default: false },
  labels: { type: Object, required: true },
  streamingManagerText: { type: String, default: "" },
  streamingManager: { type: Boolean, default: false },
});

const emit = defineEmits(["suggestion", "retry-task"]);

const userMessages = computed(() => props.messages.filter((message) => message.role === "user"));
const directAssistantMessages = computed(() =>
  props.messages.filter((message) => message.role === "assistant" && message.metadata?.kind !== "swarm_worker"),
);
</script>

<template>
  <section class="work-area">
    <div class="work-area__scroll">
      <div v-if="messages.length || streamingManager || taskCards.length" class="work-area__stack">
        <template v-if="route === 'chat'">
          <template v-for="message in messages" :key="message.id">
            <ChatBubble v-if="message.role === 'user'" :message="message" :label="labels.you" :is-user="true" :rtl="isArabic" :copy-label="labels.copy" />
            <ChatBubble v-else :message="message" :label="labels.assistant" :copy-label="labels.copy" />
          </template>
          <ChatBubble
            v-if="streamingManager"
            :message="{ content: streamingManagerText }"
            :label="labels.assistant"
            :is-streaming="true"
            :live-buffer="streamingManagerText"
            :copy-label="labels.copy"
          />
        </template>

        <template v-else>
          <template v-for="message in userMessages" :key="message.id">
            <ChatBubble :message="message" :label="labels.you" :is-user="true" :rtl="isArabic" :copy-label="labels.copy" />
          </template>

          <template v-for="message in directAssistantMessages" :key="message.id">
            <ChatBubble :message="message" :label="labels.assistant" :copy-label="labels.copy" />
          </template>

          <WorkerTaskCard v-for="card in taskCards" :key="card.id" :card="card" :labels="labels" @retry="$emit('retry-task', $event)" />
        </template>
      </div>

      <div v-else class="work-area__empty">
        <div class="work-area__empty-title">{{ labels.emptyTitle }}</div>
        <div class="work-area__empty-body">{{ labels.emptySubtitle }}</div>
        <div class="work-area__suggestions">
          <button v-for="suggestion in labels.suggestions" :key="suggestion" type="button" class="work-area__suggestion" @click="$emit('suggestion', suggestion)">
            {{ suggestion }}
          </button>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.work-area {
  min-height: 0;
  background: radial-gradient(circle at top, rgba(99, 102, 241, 0.08), transparent 26%), var(--bg-root);
}

.work-area__scroll {
  height: 100%;
  overflow: auto;
  padding: 24px;
}

.work-area__stack {
  max-width: 980px;
  margin: 0 auto;
  display: grid;
  gap: 16px;
}

.work-area__empty {
  min-height: 100%;
  display: grid;
  align-content: center;
  justify-items: center;
  gap: 14px;
  text-align: center;
  padding: 40px 18px;
}

.work-area__empty-title {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-main);
}

.work-area__empty-body {
  max-width: 720px;
  font-size: 14px;
  line-height: 1.7;
  color: var(--text-mute);
}

.work-area__suggestions {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 10px;
  margin-top: 8px;
}

.work-area__suggestion {
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  background: var(--bg-surface);
  color: var(--text-main);
  padding: 10px 14px;
  cursor: pointer;
  font-size: 13px;
}

.work-area__suggestion:hover {
  background: var(--bg-hover);
}
</style>
