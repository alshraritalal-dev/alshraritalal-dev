<script setup>
import { computed, ref, watch } from "vue";

const props = defineProps({
  groups: { type: Array, default: () => [] },
  activeId: { type: String, default: null },
  brandTitle: { type: String, required: true },
  brandSubtitle: { type: String, required: true },
  newChatLabel: { type: String, required: true },
  emptyLabel: { type: String, required: true },
  renameLabel: { type: String, required: true },
  deleteLabel: { type: String, required: true },
});

const emit = defineEmits(["select", "new-chat", "rename", "delete"]);

const editingId = ref(null);
const editingTitle = ref("");

watch(
  () => props.activeId,
  () => {
    editingId.value = null;
    editingTitle.value = "";
  }
);

const hasSessions = computed(() => props.groups.some((group) => group.items.length > 0));

function startRename(session) {
  editingId.value = session.id;
  editingTitle.value = session.title;
}

function submitRename(session) {
  const title = editingTitle.value.trim();
  if (title) {
    emit("rename", { id: session.id, title });
  }
  editingId.value = null;
  editingTitle.value = "";
}
</script>

<template>
  <aside class="flex h-full min-h-0 w-full max-w-[300px] flex-col border-e border-edge bg-slate-950/95">
    <div class="border-b border-edge px-5 py-5">
      <div class="mb-4 flex items-center gap-3">
        <div class="flex h-11 w-11 items-center justify-center rounded-2xl bg-cyan-400/10 text-xl text-cyan-300 shadow-[0_0_24px_rgba(34,211,238,0.18)]">
          ⚡
        </div>
        <div class="min-w-0">
          <div class="truncate text-sm font-semibold tracking-wide text-white">{{ brandTitle }}</div>
          <div class="truncate text-xs text-mute">{{ brandSubtitle }}</div>
        </div>
      </div>

      <button class="sidebar-primary-btn" @click="$emit('new-chat')">
        <span class="text-base">＋</span>
        <span>{{ newChatLabel }}</span>
      </button>
    </div>

    <div class="sidebar-scroll flex-1 overflow-auto px-3 py-4">
      <template v-if="hasSessions">
        <section v-for="group in groups" :key="group.label" class="mb-6">
          <div class="px-3 pb-2 text-[11px] font-semibold uppercase tracking-[0.24em] text-mute">
            {{ group.label }}
          </div>

          <div class="space-y-1.5">
            <article
              v-for="session in group.items"
              :key="session.id"
              class="group rounded-2xl border px-3 py-3 transition-all duration-200"
              :class="
                session.id === activeId
                  ? 'border-cyan-500/40 bg-cyan-500/10 shadow-[0_0_0_1px_rgba(34,211,238,0.12),0_20px_40px_rgba(15,23,42,0.35)]'
                  : 'border-transparent bg-slate-900/70 hover:border-slate-700 hover:bg-slate-900'
              "
            >
              <div class="flex items-start gap-2">
                <button class="min-w-0 flex-1 text-left" @click="$emit('select', session.id)">
                  <template v-if="editingId === session.id">
                    <input
                      v-model="editingTitle"
                      class="w-full rounded-lg border border-slate-700 bg-slate-950 px-2.5 py-1.5 text-sm text-white outline-none focus:border-cyan-400"
                      @keyup.enter="submitRename(session)"
                      @blur="submitRename(session)"
                    />
                  </template>
                  <template v-else>
                    <div class="truncate text-sm font-medium text-slate-100" @dblclick.stop="startRename(session)">
                      {{ session.title }}
                    </div>
                  </template>
                  <div class="mt-1 flex items-center gap-2 text-xs text-mute">
                    <span>{{ session.relativeTime }}</span>
                    <span class="rounded-full border border-slate-700 px-2 py-0.5 uppercase tracking-wide">
                      {{ session.statusLabel }}
                    </span>
                  </div>
                </button>

                <div class="flex shrink-0 items-center gap-1 opacity-0 transition-opacity duration-200 group-hover:opacity-100">
                  <button
                    type="button"
                    class="sidebar-icon-btn"
                    :title="renameLabel"
                    @click.stop="startRename(session)"
                  >
                    ✏️
                  </button>
                  <button
                    type="button"
                    class="sidebar-icon-btn sidebar-icon-btn-danger"
                    :title="deleteLabel"
                    @click.stop="$emit('delete', session.id)"
                  >
                    🗑️
                  </button>
                </div>
              </div>
            </article>
          </div>
        </section>
      </template>

      <div v-else class="flex h-full items-center justify-center px-6 text-center text-sm text-mute">
        {{ emptyLabel }}
      </div>
    </div>
  </aside>
</template>
