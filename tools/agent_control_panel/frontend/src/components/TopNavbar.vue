<script setup>
const props = defineProps({
  title: { type: String, required: true },
  routeLabel: { type: String, required: true },
  statusLabel: { type: String, required: true },
  connectionLabel: { type: String, required: true },
  isConnected: { type: Boolean, default: false },
  locale: { type: String, required: true },
  notice: { type: String, default: "" },
  loading: { type: Boolean, default: false },
});

defineEmits(["set-locale"]);
</script>

<template>
  <header class="topnav">
    <div class="topnav__main">
      <div class="topnav__title">{{ title }}</div>
      <div class="topnav__meta">
        <span class="topnav__chip">{{ routeLabel }}</span>
        <span class="topnav__chip">{{ statusLabel }}</span>
        <span class="topnav__chip" :class="{ 'topnav__chip--connected': isConnected }">{{ connectionLabel }}</span>
        <span v-if="loading" class="topnav__chip">{{ loading ? "..." : "" }}</span>
      </div>
      <div v-if="notice" class="topnav__notice">{{ notice }}</div>
    </div>

    <div class="topnav__actions">
      <div class="topnav__locale">
        <button type="button" :class="{ 'topnav__locale-btn--active': locale === 'en' }" class="topnav__locale-btn" @click="$emit('set-locale', 'en')">EN</button>
        <button type="button" :class="{ 'topnav__locale-btn--active': locale === 'ar' }" class="topnav__locale-btn" @click="$emit('set-locale', 'ar')">عربي</button>
      </div>
    </div>
  </header>
</template>

<style scoped>
.topnav {
  min-height: 72px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 18px 24px 14px;
  border-bottom: 1px solid var(--border-dim);
  background: rgba(10, 10, 15, 0.92);
  backdrop-filter: blur(12px);
}

.topnav__main {
  min-width: 0;
  flex: 1;
}

.topnav__title {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.topnav__meta {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 8px;
}

.topnav__chip {
  display: inline-flex;
  align-items: center;
  min-height: 24px;
  padding: 0 10px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  color: var(--text-mute);
  font-size: 11px;
}

.topnav__chip--connected {
  color: var(--success);
  border-color: rgba(16, 185, 129, 0.28);
}

.topnav__notice {
  margin-top: 8px;
  font-size: 12px;
  color: var(--text-mute);
}

.topnav__actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.topnav__locale {
  display: inline-flex;
  padding: 3px;
  border-radius: 999px;
  border: 1px solid var(--border-dim);
  background: var(--bg-surface);
}

.topnav__locale-btn {
  min-width: 58px;
  height: 32px;
  border: 0;
  border-radius: 999px;
  background: transparent;
  color: var(--text-mute);
  cursor: pointer;
}

.topnav__locale-btn--active {
  background: var(--accent-glow);
  color: var(--text-main);
}
</style>
