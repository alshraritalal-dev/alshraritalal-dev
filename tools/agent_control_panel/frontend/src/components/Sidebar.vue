<script setup>
import { computed, ref, watch } from "vue";

const props = defineProps({
  groups: { type: Array, default: () => [] },
  activeId: { type: String, default: null },
  labels: { type: Object, required: true },
});

const emit = defineEmits(["select", "new-chat", "rename", "delete"]);

const editingId = ref(null);
const editingTitle = ref("");

watch(
  () => props.activeId,
  () => {
    editingId.value = null;
    editingTitle.value = "";
  },
);

const hasSessions = computed(() => props.groups.some((group) => group.items.length > 0));

function startRename(session) {
  editingId.value = session.id;
  editingTitle.value = session.title;
}

function submitRename(session) {
  const title = editingTitle.value.trim();
  if (title) emit("rename", { id: session.id, title });
  editingId.value = null;
  editingTitle.value = "";
}
</script>

<template>
  <aside class="sidebar">
    <div class="sidebar__brand">
      <div class="sidebar__logo">S</div>
      <div class="sidebar__titlewrap">
        <div class="sidebar__title">{{ labels.brandTitle }}</div>
        <div class="sidebar__subtitle">{{ labels.brandSubtitle }}</div>
      </div>
    </div>

    <button type="button" class="sidebar__new" @click="$emit('new-chat')">
      <span class="sidebar__plus">+</span>
      <span>{{ labels.newChat }}</span>
    </button>

    <div class="sidebar__groups">
      <template v-if="hasSessions">
        <section v-for="group in groups" :key="group.label" class="sidebar__group">
          <div class="sidebar__group-label">{{ group.label }}</div>
          <div class="sidebar__group-items">
            <article
              v-for="session in group.items"
              :key="session.id"
              class="sidebar__item"
              :class="{ 'sidebar__item--active': session.id === activeId }"
            >
              <button type="button" class="sidebar__item-main" @click="$emit('select', session.id)">
                <template v-if="editingId === session.id">
                  <input
                    v-model="editingTitle"
                    class="sidebar__rename-input"
                    @keyup.enter="submitRename(session)"
                    @blur="submitRename(session)"
                  />
                </template>
                <template v-else>
                  <div class="sidebar__item-title" @dblclick.stop="startRename(session)">
                    {{ session.title }}
                  </div>
                </template>
                <div class="sidebar__item-meta">
                  <span>{{ session.relativeTime }}</span>
                  <span class="sidebar__item-status">{{ session.statusLabel }}</span>
                </div>
              </button>

              <div class="sidebar__item-actions">
                <button type="button" class="sidebar__icon" :title="labels.rename" @click.stop="startRename(session)">
                  <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M4 20h4l10-10-4-4L4 16v4Z" />
                    <path d="M13 7l4 4" />
                  </svg>
                </button>
                <button type="button" class="sidebar__icon sidebar__icon--danger" :title="labels.delete" @click.stop="$emit('delete', session.id)">
                  <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M5 7h14" />
                    <path d="M9 7V4h6v3" />
                    <path d="M8 7l1 12h6l1-12" />
                  </svg>
                </button>
              </div>
            </article>
          </div>
        </section>
      </template>

      <div v-else class="sidebar__empty">{{ labels.emptySidebar }}</div>
    </div>
  </aside>
</template>

<style scoped>
.sidebar {
  display: flex;
  min-height: 0;
  flex-direction: column;
  background: var(--bg-panel);
  border-inline-end: 1px solid var(--border-dim);
  padding: 20px 16px 16px;
}

.sidebar__brand {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
}

.sidebar__logo {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  display: grid;
  place-items: center;
  background: var(--accent-glow);
  border: 1px solid rgba(99, 102, 241, 0.25);
  color: var(--accent-main);
  font-size: 13px;
  font-weight: 600;
}

.sidebar__titlewrap {
  min-width: 0;
}

.sidebar__title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-main);
}

.sidebar__subtitle {
  font-size: 12px;
  color: var(--text-mute);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.sidebar__new {
  width: 100%;
  height: 40px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border-focus);
  background: var(--bg-surface);
  color: var(--text-main);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  cursor: pointer;
}

.sidebar__new:hover {
  background: var(--bg-hover);
}

.sidebar__plus {
  font-size: 18px;
  line-height: 1;
}

.sidebar__groups {
  margin-top: 18px;
  overflow: auto;
  min-height: 0;
  padding-inline-end: 2px;
}

.sidebar__group + .sidebar__group {
  margin-top: 18px;
}

.sidebar__group-label {
  font-size: 11px;
  color: var(--text-dim);
  text-transform: uppercase;
  margin-bottom: 8px;
  padding-inline: 6px;
}

.sidebar__group-items {
  display: grid;
  gap: 8px;
}

.sidebar__item {
  display: flex;
  gap: 8px;
  padding: 10px;
  border-radius: var(--radius-md);
  border: 1px solid transparent;
  background: transparent;
}

.sidebar__item:hover,
.sidebar__item--active {
  background: var(--bg-surface);
  border-color: var(--border-dim);
}

.sidebar__item--active {
  box-shadow: 0 0 0 1px rgba(99, 102, 241, 0.12);
}

.sidebar__item-main {
  flex: 1;
  min-width: 0;
  border: 0;
  background: transparent;
  color: inherit;
  text-align: start;
  cursor: pointer;
}

.sidebar__item-title,
.sidebar__rename-input {
  width: 100%;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-main);
}

.sidebar__rename-input {
  background: #050507;
  border: 1px solid var(--border-focus);
  border-radius: var(--radius-sm);
  padding: 6px 8px;
  outline: none;
}

.sidebar__item-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 6px;
  font-size: 11px;
  color: var(--text-mute);
}

.sidebar__item-status {
  padding: 2px 6px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
}

.sidebar__item-actions {
  display: flex;
  gap: 6px;
}

.sidebar__icon {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  border: 1px solid var(--border-dim);
  background: transparent;
  color: var(--text-mute);
  cursor: pointer;
  display: inline-grid;
  place-items: center;
}

.sidebar__icon svg {
  width: 13px;
  height: 13px;
  stroke: currentColor;
  stroke-width: 1.7;
  fill: none;
}

.sidebar__icon:hover {
  color: var(--text-main);
  border-color: var(--border-focus);
}

.sidebar__icon--danger:hover {
  color: var(--danger);
  border-color: rgba(239, 68, 68, 0.35);
}

.sidebar__empty {
  min-height: 180px;
  display: grid;
  place-items: center;
  text-align: center;
  color: var(--text-mute);
  font-size: 13px;
  padding: 12px;
}
</style>
