<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import Sidebar from "./components/Sidebar.vue";
import TopNavbar from "./components/TopNavbar.vue";
import SwarmStatusBar from "./components/SwarmStatusBar.vue";
import ChatAndTaskArea from "./components/ChatAndTaskArea.vue";
import InputConsole from "./components/InputConsole.vue";
import SwarmInspectorPanel from "./components/SwarmInspectorPanel.vue";
import { translations } from "./lib/i18n";

const apiBase = import.meta.env.VITE_API_BASE_URL || "http://127.0.0.1:8008";
const storageKeys = {
  locale: "local-agent-panel-locale",
  activeRun: "local-agent-panel-active-run",
};

const locale = ref(localStorage.getItem(storageKeys.locale) || "en");
const health = ref(null);
const config = ref(null);
const sessions = ref([]);
const currentRun = ref(null);
const globalSwarmState = ref({ version: 1, updated_at: "", runs: {} });
const socket = ref(null);
const socketRunId = ref(null);
const submitting = ref(false);
const loadingRun = ref(false);
const systemNotice = ref("");

const liveBuffers = reactive({});
const streamingAgents = reactive({});

const form = reactive({
  goal: "",
  workspace_root: "",
  attached_file_path: "",
  model: "",
  stepMode: false,
});

const t = computed(() => translations[locale.value]);
const isArabic = computed(() => locale.value === "ar");
const direction = computed(() => (isArabic.value ? "rtl" : "ltr"));
const currentMessages = computed(() => currentRun.value?.messages || []);
const currentArtifacts = computed(() => currentRun.value?.artifacts || []);
const currentRoute = computed(() => currentRun.value?.execution_route || (currentRun.value?.swarm_run ? "swarm" : "chat"));
const swarmRun = computed(() => currentRun.value?.swarm_run || null);
const currentStatus = computed(() => currentRun.value?.status || "idle");
const hasActiveStream = computed(() => Object.values(streamingAgents).some(Boolean));
const isRunInProgress = computed(() => ["queued", "planning", "applying"].includes(currentStatus.value) || hasActiveStream.value);
const isSwarmRunning = computed(() => currentRoute.value === "swarm" && ["queued", "planning", "applying"].includes(currentStatus.value));
const canSend = computed(() => Boolean(form.goal.trim()) && !submitting.value && !isRunInProgress.value);
const managerStreamingText = computed(() => liveBuffers.manager || "");
const managerIsStreaming = computed(() => Boolean(streamingAgents.manager || managerStreamingText.value));

const sidebarLabels = computed(() => ({
  brandTitle: t.value.brandTitle,
  brandSubtitle: t.value.brandSubtitle,
  newChat: t.value.newChat,
  emptySidebar: t.value.emptySidebar,
  rename: t.value.rename,
  delete: t.value.delete,
}));

const routeLabel = computed(() => (currentRoute.value === "swarm" ? t.value.topRouteSwarm : t.value.topRouteChat));
const connectionLabel = computed(() =>
  `${t.value.backendHealth}: ${String(health.value?.ollama || "").startsWith("ok") ? t.value.connected : t.value.unavailable}`,
);
const statusLabel = computed(() => `${t.value.currentStatus}: ${statusText(currentStatus.value)}`);

const managerStatusLabel = computed(() => {
  if (!currentRun.value) return t.value.managerIdle;
  if (currentStatus.value === "failed") return t.value.managerFailed;
  if (currentStatus.value === "cancelled") return t.value.managerCancelled;
  if (currentStatus.value === "completed") return t.value.managerCompleted;
  if (currentRoute.value === "chat" && ["planning", "applying", "queued"].includes(currentStatus.value)) return t.value.managerReplying;
  if (currentRoute.value === "swarm" && ["planning", "applying", "queued"].includes(currentStatus.value)) return t.value.managerOrchestrating;
  return t.value.managerIdle;
});

const sessionGroups = computed(() => {
  const buckets = { today: [], yesterday: [], last7: [], older: [] };
  const now = new Date();
  const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());

  sessions.value.forEach((session) => {
    const updated = new Date(session.updated_at);
    const updatedDay = new Date(updated.getFullYear(), updated.getMonth(), updated.getDate());
    const diffDays = Math.floor((today.getTime() - updatedDay.getTime()) / 86400000);

    const enriched = {
      ...session,
      title: session.title || t.value.sessionUntitled,
      relativeTime: formatRelativeTime(session.updated_at),
      statusLabel: statusText(session.status),
    };

    if (diffDays <= 0) buckets.today.push(enriched);
    else if (diffDays === 1) buckets.yesterday.push(enriched);
    else if (diffDays <= 7) buckets.last7.push(enriched);
    else buckets.older.push(enriched);
  });

  return [
    { label: t.value.today, items: buckets.today },
    { label: t.value.yesterday, items: buckets.yesterday },
    { label: t.value.last7Days, items: buckets.last7 },
    { label: t.value.older, items: buckets.older },
  ];
});

const workerPills = computed(() => swarmRun.value?.workers || []);

const taskCards = computed(() => {
  if (!swarmRun.value) return [];

  const artifactsById = new Map();
  const artifactsByPath = new Map();
  currentArtifacts.value.forEach((artifact) => {
    artifactsById.set(artifact.id, artifact);
    artifactsByPath.set(artifact.absolute_path, artifact);
    if (artifact.source_absolute_path) artifactsByPath.set(artifact.source_absolute_path, artifact);
  });

  const workerByTaskId = new Map();
  (swarmRun.value.workers || []).forEach((worker) => {
    if (worker.task_id) workerByTaskId.set(worker.task_id, worker);
    (worker.completed_task_ids || []).forEach((taskId) => {
      if (!workerByTaskId.has(taskId)) workerByTaskId.set(taskId, worker);
    });
  });

  return (swarmRun.value.tasks || []).map((task) => {
    const worker = workerByTaskId.get(task.id) || null;
    const workerLabel = worker?.name || task.assigned_worker_name || t.value.workerId;
    const workerKey = normalizeAgentKey(workerLabel);
    const logs = currentRun.value?.logs?.[workerKey] || [];
    const artifacts = [];

    (task.artifact_ids || []).forEach((artifactId) => {
      const artifact = artifactsById.get(artifactId);
      if (artifact) artifacts.push(artifact);
    });
    (task.generated_paths || []).forEach((path) => {
      const artifact = artifactsByPath.get(path);
      if (artifact && !artifacts.some((entry) => entry.id === artifact.id)) artifacts.push(artifact);
    });

    const primaryArtifact = artifacts[0] || null;
    return {
      id: task.id,
      title: task.title,
      workerLabel,
      logs,
      reply: task.reply || "",
      error: task.error || "",
      summary: task.output_summary || "",
      status: task.status,
      statusLabel: statusText(task.status),
      statusTone: taskTone(task.status),
      durationLabel: formatDuration(task.started_at, task.completed_at),
      primaryArtifact: primaryArtifact
        ? {
            ...primaryArtifact,
            open_url: resolveApiUrl(primaryArtifact.open_url),
          }
        : null,
      diffPreview: buildDiffPreview(primaryArtifact),
    };
  });
});

const dependencyItems = computed(() => {
  if (!swarmRun.value?.dependency_graph) return [];
  const titleById = Object.fromEntries((swarmRun.value.tasks || []).map((task) => [task.id, task.title]));
  return Object.entries(swarmRun.value.dependency_graph)
    .filter(([, deps]) => Array.isArray(deps) && deps.length)
    .map(([taskId, deps]) => ({
      key: taskId,
      title: titleById[taskId] || taskId,
      body: deps.map((dep) => titleById[dep] || dep).join(" <- "),
    }));
});

const inspectorMetrics = computed(() => {
  const activeWorkers = (swarmRun.value?.workers || []).filter((worker) =>
    ["planning", "working", "verifying"].includes(worker.status),
  ).length;
  const tokenEstimate =
    currentMessages.value.reduce((total, message) => total + String(message.content || "").length, 0) +
    Object.values(liveBuffers).join("").length;
  const taskCounts = swarmRun.value?.metrics?.taskCounts || {};
  const queueDepth = taskCounts.pending || 0;

  return [
    { label: t.value.memoryUsage, value: `${180 + activeWorkers * 62} MB` },
    { label: t.value.tokenUsage, value: String(tokenEstimate) },
    { label: t.value.queueDepth, value: String(queueDepth) },
  ];
});

watch(
  locale,
  (value) => {
    localStorage.setItem(storageKeys.locale, value);
    document.documentElement.dir = value === "ar" ? "rtl" : "ltr";
    document.documentElement.lang = value === "ar" ? "ar" : "en";
  },
  { immediate: true },
);

function statusText(status) {
  return t.value.statuses[status] || status || t.value.statuses.idle;
}

function taskTone(status) {
  if (["working", "planning", "verifying"].includes(status)) return "working";
  if (["done", "completed"].includes(status)) return "success";
  if (["error", "failed", "cancelled", "blocked"].includes(status)) return "error";
  return "idle";
}

function formatDuration(startedAt, completedAt) {
  if (!startedAt) return "0.0s";
  const start = new Date(startedAt).getTime();
  const end = completedAt ? new Date(completedAt).getTime() : Date.now();
  const durationMs = Math.max(0, end - start);
  return durationMs < 1000 ? `${durationMs}ms` : `${(durationMs / 1000).toFixed(1)}s`;
}

function buildDiffPreview(artifact) {
  if (!artifact || artifact.preview_kind !== "text" || !artifact.preview_text) return "";
  return artifact.preview_text
    .split(/\r?\n/)
    .slice(0, 120)
    .map((line) => `+ ${line}`)
    .join("\n");
}

function resolveApiUrl(path) {
  if (!path) return "";
  if (/^https?:\/\//i.test(path)) return path;
  return `${apiBase}${path}`;
}

function sortSessions() {
  sessions.value = [...sessions.value].sort((left, right) => right.updated_at.localeCompare(left.updated_at));
}

function runToSummary(run) {
  return {
    id: run.id,
    title: run.title,
    created_at: run.created_at,
    updated_at: run.updated_at,
    status: run.status,
    execution_route: run.execution_route,
    message_count: run.messages?.length || 0,
    workspace_root: run.workspace_root,
    swarm: run.swarm_run || null,
  };
}

function upsertSession(runLike) {
  const summary = runLike.messages ? runToSummary(runLike) : runLike;
  const index = sessions.value.findIndex((entry) => entry.id === summary.id);
  if (index >= 0) sessions.value[index] = { ...sessions.value[index], ...summary };
  else sessions.value.unshift(summary);
  sortSessions();
}

function ensureAgentBuffer(agentKey) {
  if (!(agentKey in liveBuffers)) liveBuffers[agentKey] = "";
  if (!(agentKey in streamingAgents)) streamingAgents[agentKey] = false;
}

function resetLiveState() {
  Object.keys(liveBuffers).forEach((key) => delete liveBuffers[key]);
  Object.keys(streamingAgents).forEach((key) => delete streamingAgents[key]);
}

function normalizeAgentKey(agent) {
  return String(agent || "")
    .trim()
    .toLowerCase()
    .replace(/\s+/g, "_");
}

function syncBuffersFromRun(run) {
  const completedAgents = new Set(
    (run.messages || [])
      .filter((message) => message.role === "assistant" && message.agent)
      .map((message) => normalizeAgentKey(message.agent)),
  );
  Object.keys(liveBuffers).forEach((key) => {
    if (completedAgents.has(key) && !streamingAgents[key]) liveBuffers[key] = "";
  });
  if (["completed", "failed", "cancelled"].includes(run.status)) {
    Object.keys(streamingAgents).forEach((key) => {
      streamingAgents[key] = false;
    });
  }
}

function formatRelativeTime(isoString) {
  const value = new Date(isoString).getTime() - Date.now();
  const absSeconds = Math.abs(Math.round(value / 1000));
  const rtf = new Intl.RelativeTimeFormat(locale.value, { numeric: "auto" });

  if (absSeconds < 60) return rtf.format(Math.round(value / 1000), "second");
  if (absSeconds < 3600) return rtf.format(Math.round(value / 60000), "minute");
  if (absSeconds < 86400) return rtf.format(Math.round(value / 3600000), "hour");
  return rtf.format(Math.round(value / 86400000), "day");
}

function clearSocket() {
  if (socket.value) {
    socket.value.close();
    socket.value = null;
  }
  socketRunId.value = null;
}

async function fetchJson(url, options = undefined) {
  const response = await fetch(url, options);
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

async function loadHealth() {
  try {
    health.value = await fetchJson(`${apiBase}/api/health`);
  } catch (error) {
    health.value = { ollama: `unavailable: ${error.message}` };
  }
}

async function loadConfig() {
  config.value = await fetchJson(`${apiBase}/api/config`);
  form.workspace_root = config.value.projectRoot;
  form.model = config.value.defaultModel;
}

async function loadSessions() {
  sessions.value = await fetchJson(`${apiBase}/api/sessions`);
  sortSessions();
}

async function refreshGlobalSwarmState() {
  try {
    globalSwarmState.value = await fetchJson(`${apiBase}/api/swarm/state`);
  } catch {
    globalSwarmState.value = { version: 1, updated_at: "", runs: {} };
  }
}

async function loadRun(runId) {
  loadingRun.value = true;
  try {
    currentRun.value = await fetchJson(`${apiBase}/api/runs/${runId}`);
    form.workspace_root = currentRun.value.workspace_root || form.workspace_root;
    form.attached_file_path = currentRun.value.attached_file_path || "";
    syncBuffersFromRun(currentRun.value);
    localStorage.setItem(storageKeys.activeRun, runId);
    connectSocket(runId);
    await refreshGlobalSwarmState();
  } finally {
    loadingRun.value = false;
  }
}

function patchCurrentRunArtifact(artifact) {
  if (!currentRun.value || !artifact) return;
  if (!currentRun.value.artifacts) currentRun.value.artifacts = [];
  const index = currentRun.value.artifacts.findIndex((entry) => entry.id === artifact.id || entry.absolute_path === artifact.absolute_path);
  if (index >= 0) currentRun.value.artifacts[index] = artifact;
  else currentRun.value.artifacts = [...currentRun.value.artifacts, artifact];
}

function connectSocket(runId) {
  if (socketRunId.value === runId && socket.value) return;
  clearSocket();
  resetLiveState();

  const url = `${apiBase.replace("http://", "ws://").replace("https://", "wss://")}/ws/runs/${runId}`;
  socket.value = new WebSocket(url);
  socketRunId.value = runId;

  socket.value.onmessage = async (event) => {
    const payload = JSON.parse(event.data);

    if (payload.type === "state") {
      currentRun.value = payload.run;
      upsertSession(payload.run);
      syncBuffersFromRun(payload.run);
      return;
    }

    if (payload.type === "route_selected" && currentRun.value?.id === payload.runId) {
      currentRun.value.execution_route = payload.route;
      currentRun.value.metadata = {
        ...(currentRun.value.metadata || {}),
        route_reason: payload.reason,
      };
      return;
    }

    if (payload.type === "swarm_state" && currentRun.value?.id === payload.runId) {
      currentRun.value.swarm_run = payload.swarm;
      await refreshGlobalSwarmState();
      return;
    }

    if (payload.type === "agent_start") {
      const key = normalizeAgentKey(payload.agent);
      ensureAgentBuffer(key);
      streamingAgents[key] = true;
      return;
    }

    if (payload.type === "token" || payload.type === "agent_chunk") {
      const key = normalizeAgentKey(payload.agent);
      const chunk = payload.token ?? payload.chunk ?? "";
      ensureAgentBuffer(key);
      liveBuffers[key] += chunk;
      streamingAgents[key] = true;
      return;
    }

    if (payload.type === "agent_done") {
      const key = normalizeAgentKey(payload.agent);
      ensureAgentBuffer(key);
      streamingAgents[key] = false;
      return;
    }

    if (payload.type === "artifact_created") {
      patchCurrentRunArtifact(payload.artifact);
      return;
    }

    if (payload.type === "pipeline_complete" || payload.type === "pipeline_error") {
      Object.keys(streamingAgents).forEach((key) => {
        streamingAgents[key] = false;
      });
      await refreshGlobalSwarmState();
    }
  };
}

function newChat() {
  if (isRunInProgress.value) return;
  currentRun.value = null;
  systemNotice.value = "";
  form.goal = "";
  form.attached_file_path = "";
  localStorage.removeItem(storageKeys.activeRun);
  clearSocket();
  resetLiveState();
}

async function renameRun({ id, title }) {
  const updated = await fetchJson(`${apiBase}/api/runs/${id}/title`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json; charset=utf-8" },
    body: JSON.stringify({ title }),
  });
  upsertSession(updated);
  if (currentRun.value?.id === id) currentRun.value = updated;
}

async function deleteRun(id) {
  const session = sessions.value.find((item) => item.id === id);
  if (!window.confirm(`${t.value.delete}: ${session?.title || id}?`)) return;
  await fetchJson(`${apiBase}/api/runs/${id}`, { method: "DELETE" });
  sessions.value = sessions.value.filter((entry) => entry.id !== id);
  if (currentRun.value?.id === id) newChat();
}

async function cancelRun() {
  if (!currentRun.value) return;
  const response = await fetchJson(`${apiBase}/api/runs/${currentRun.value.id}/cancel`, { method: "POST" });
  systemNotice.value = response.message;
}

async function retryTask(taskId) {
  if (!currentRun.value) return;
  const payload = await fetchJson(`${apiBase}/api/runs/${currentRun.value.id}/swarm/tasks/${taskId}/retry`, { method: "POST" });
  currentRun.value.execution_route = payload.route;
  currentRun.value.swarm_run = payload.swarm;
  systemNotice.value = `${t.value.retry}: ${taskId}`;
  await refreshGlobalSwarmState();
}

async function clearState() {
  if (!currentRun.value) return;
  const result = await fetchJson(`${apiBase}/api/runs/${currentRun.value.id}/swarm/state`, { method: "DELETE" });
  systemNotice.value = result.message;
  if (currentRun.value) currentRun.value.swarm_run = null;
  await refreshGlobalSwarmState();
}

async function restartUvicorn() {
  const result = await fetchJson(`${apiBase}/api/admin/restart-uvicorn`, { method: "POST" });
  systemNotice.value = result.message;
}

async function runPipeline(prefilledGoal = null) {
  if (isRunInProgress.value || submitting.value) return;
  const rawGoal = typeof prefilledGoal === "string" ? prefilledGoal : form.goal;
  const goal = rawGoal.trim();
  if (!goal) return;

  submitting.value = true;
  try {
    const run = await fetchJson(`${apiBase}/api/runs`, {
      method: "POST",
      headers: { "Content-Type": "application/json; charset=utf-8" },
      body: JSON.stringify({
        goal,
        workspace_root: form.workspace_root,
        attached_file_path: form.attached_file_path || null,
        model: form.model || null,
        mode: form.stepMode ? "step" : "auto",
        locale: locale.value,
      }),
    });
    currentRun.value = run;
    systemNotice.value = "";
    upsertSession(run);
    localStorage.setItem(storageKeys.activeRun, run.id);
    form.goal = "";
    resetLiveState();
    connectSocket(run.id);
  } finally {
    submitting.value = false;
  }
}

async function handleSuggestion(goal) {
  form.goal = goal;
  await runPipeline(goal);
}

function setLocale(nextLocale) {
  locale.value = nextLocale;
}

onMounted(async () => {
  await Promise.all([loadHealth(), loadConfig(), loadSessions(), refreshGlobalSwarmState()]);
  const rememberedRun = localStorage.getItem(storageKeys.activeRun);
  const fallbackRun = rememberedRun || sessions.value[0]?.id;
  if (fallbackRun) {
    try {
      await loadRun(fallbackRun);
    } catch {
      newChat();
    }
  }
});

onBeforeUnmount(() => {
  clearSocket();
});
</script>

<template>
  <div class="os-root" :dir="direction" :class="{ 'os-root--arabic': isArabic }">
    <div class="os-shell">
      <Sidebar
        class="os-sidebar"
        :groups="sessionGroups"
        :active-id="currentRun?.id || null"
        :labels="sidebarLabels"
        @select="loadRun"
        @new-chat="newChat"
        @rename="renameRun"
        @delete="deleteRun"
      />

      <main class="os-main">
        <TopNavbar
          :title="currentRun?.title || t.sessionDraft"
          :route-label="routeLabel"
          :status-label="statusLabel"
          :connection-label="connectionLabel"
          :is-connected="String(health?.ollama || '').startsWith('ok')"
          :locale="locale"
          :notice="systemNotice"
          :loading="loadingRun"
          @set-locale="setLocale"
        />

        <SwarmStatusBar
          :manager-label="managerStatusLabel"
          :workers="workerPills"
          :labels="{ managerStatus: t.managerStatus, noWorkers: t.noWorkers }"
        />

        <ChatAndTaskArea
          :route="currentRoute"
          :messages="currentMessages"
          :task-cards="taskCards"
          :is-arabic="isArabic"
          :labels="{ ...t, assistant: t.assistant }"
          :streaming-manager-text="managerStreamingText"
          :streaming-manager="currentRoute === 'chat' && managerIsStreaming"
          @suggestion="handleSuggestion"
          @retry-task="retryTask"
        />

        <InputConsole
          v-model="form.goal"
          :placeholder="t.composerPlaceholder"
          :submit-label="t.send"
          :stop-label="t.stopGeneration"
          :hint="t.sendHint"
          :char-count-label="t.charCount"
          :can-send="canSend"
          :swarm-running="isSwarmRunning"
          @submit="runPipeline"
          @cancel="cancelRun"
        />
      </main>

      <SwarmInspectorPanel
        class="os-inspector"
        :labels="t"
        :global-state="globalSwarmState"
        :dependency-items="dependencyItems"
        :metrics="inspectorMetrics"
        :form="form"
        :status-label="statusLabel"
        :route-label="routeLabel"
        :notice="currentRun?.metadata?.route_reason || ''"
        :can-halt="isSwarmRunning"
        :can-clear="Boolean(currentRun && currentRoute === 'swarm' && !isSwarmRunning)"
        @halt="cancelRun"
        @clear="clearState"
        @restart="restartUvicorn"
      />
    </div>
  </div>
</template>

<style scoped>
.os-root {
  height: 100%;
  background: var(--bg-root);
}

.os-root--arabic {
  font-family: var(--font-ar);
}

.os-shell {
  height: 100%;
  display: grid;
  grid-template-columns: 280px minmax(0, 1fr) 360px;
  grid-template-areas: "sidebar main inspector";
}

.os-sidebar { grid-area: sidebar; min-width: 0; }
.os-main { grid-area: main; min-width: 0; display: grid; grid-template-rows: auto auto minmax(0, 1fr) auto; }
.os-inspector { grid-area: inspector; min-width: 0; }

.os-root[dir="rtl"] .os-shell {
  grid-template-columns: 360px minmax(0, 1fr) 280px;
  grid-template-areas: "inspector main sidebar";
}

@media (max-width: 1360px) {
  .os-shell {
    grid-template-columns: 240px minmax(0, 1fr) 320px;
  }
}

@media (max-width: 1080px) {
  .os-shell {
    grid-template-columns: 1fr;
    grid-template-areas:
      "main"
      "inspector"
      "sidebar";
  }

  .os-root[dir="rtl"] .os-shell {
    grid-template-columns: 1fr;
    grid-template-areas:
      "main"
      "inspector"
      "sidebar";
  }

  .os-main {
    min-height: 70vh;
  }
}
</style>
