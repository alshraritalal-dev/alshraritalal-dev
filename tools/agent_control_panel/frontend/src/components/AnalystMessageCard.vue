<script setup>
import { computed, ref, watch } from "vue";
import MarkdownBlock from "./MarkdownBlock.vue";

const props = defineProps({
  message: { type: Object, default: null },
  report: { type: Object, default: null },
  isStreaming: { type: Boolean, default: false },
  liveBuffer: { type: String, default: "" },
  sessionTitle: { type: String, default: "session" },
  reportPath: { type: String, default: "" },
  outputPath: { type: String, default: "" },
  labels: { type: Object, required: true },
});

const sectionState = ref({
  executive: true,
  architecture: true,
  code: true,
  qa: true,
  risks: true,
  performance: true,
  roadmap: true,
  overall: true,
});

watch(
  () => props.report,
  (value) => {
    if (value) {
      Object.keys(sectionState.value).forEach((key) => {
        sectionState.value[key] = true;
      });
    }
  },
  { immediate: true }
);

const durationLabel = computed(() => {
  const duration = props.message?.metadata?.durationMs;
  if (!duration) return null;
  return duration < 1000 ? `${duration} ms` : `${(duration / 1000).toFixed(1)} s`;
});

function toggleSection(key) {
  sectionState.value[key] = !sectionState.value[key];
}

function exportReport() {
  const content = props.report?.full_report_markdown || props.message?.content || "";
  const safeTitle = (props.sessionTitle || "session")
    .toLowerCase()
    .replace(/[^\w\u0600-\u06ff-]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 40) || "session";
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const fileName = `report_${safeTitle}_${timestamp}.md`;
  const blob = new Blob([content], { type: "text/markdown;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = fileName;
  link.click();
  URL.revokeObjectURL(url);
}

function scoreWidth(value) {
  return `${Math.max(0, Math.min(10, value || 0)) * 10}%`;
}

function complexityClass(value) {
  if (value === "EASY") return "border-emerald-500/30 bg-emerald-500/10 text-emerald-200";
  if (value === "MEDIUM") return "border-amber-500/30 bg-amber-500/10 text-amber-200";
  return "border-rose-500/30 bg-rose-500/10 text-rose-200";
}

function complexityLabel(value) {
  if (value === "EASY") return props.labels.easy;
  if (value === "MEDIUM") return props.labels.medium;
  return props.labels.hard;
}

function prettifyOverallKey(value) {
  return props.labels.overallKeyMap?.[value] || value.replaceAll("_", " ");
}
</script>

<template>
  <article class="glass-card overflow-hidden rounded-3xl border border-amber-500/30">
    <header class="flex flex-wrap items-center justify-between gap-3 border-b border-amber-500/25 bg-gradient-to-r from-amber-500/20 to-amber-400/5 px-4 py-3">
      <div class="flex items-center gap-3">
        <div class="flex h-10 w-10 items-center justify-center rounded-2xl bg-amber-500/20 text-lg text-amber-100">
          📊
        </div>
        <div>
          <div class="text-sm font-semibold text-white">{{ labels.title }}</div>
          <div class="text-[11px] uppercase tracking-[0.22em] text-amber-100/80">{{ labels.phaseLabel }} 4</div>
        </div>
      </div>

      <div class="flex items-center gap-2">
        <span v-if="durationLabel" class="rounded-full border border-amber-400/20 bg-black/20 px-2 py-1 text-[11px] text-amber-100/80">
          {{ durationLabel }}
        </span>
        <button
          type="button"
          class="rounded-xl border border-amber-500/30 bg-amber-500/10 px-3 py-1.5 text-xs font-semibold text-amber-100 transition hover:bg-amber-500/20"
          @click="exportReport"
        >
          {{ labels.exportReport }}
        </button>
      </div>
    </header>

    <div class="bg-slate-950/85 px-4 py-4">
      <div v-if="isStreaming && !report" class="rounded-2xl border border-amber-500/20 bg-slate-950 px-4 py-4 font-mono text-sm text-amber-50">
        <pre class="whitespace-pre-wrap break-words">{{ liveBuffer }}<span class="typing-cursor">|</span></pre>
      </div>

      <template v-else-if="report">
        <div class="mb-4 grid gap-3 lg:grid-cols-3">
          <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
            <div class="text-[11px] uppercase tracking-[0.22em] text-mute">{{ labels.reportLanguage }}</div>
            <div class="mt-2 text-sm font-semibold text-amber-100">{{ report.language }}</div>
          </div>
          <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
            <div class="text-[11px] uppercase tracking-[0.22em] text-mute">{{ labels.savedReport }}</div>
            <div class="mt-2 truncate text-sm text-slate-200">{{ reportPath || labels.notAvailable }}</div>
          </div>
          <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
            <div class="text-[11px] uppercase tracking-[0.22em] text-mute">{{ labels.outputLog }}</div>
            <div class="mt-2 truncate text-sm text-slate-200">{{ outputPath || labels.notAvailable }}</div>
          </div>
        </div>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('executive')">
            <span>{{ labels.sectionExecutive }}</span>
            <span>{{ sectionState.executive ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.executive" class="analyst-section-body">
            <MarkdownBlock :source="report.executive_summary" />
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('architecture')">
            <span>{{ labels.sectionArchitecture }}</span>
            <span>{{ sectionState.architecture ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.architecture" class="analyst-section-body space-y-4">
            <div v-for="item in report.architecture_scores" :key="item.label" class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
              <div class="mb-2 flex items-center justify-between text-sm">
                <span class="font-semibold text-slate-100">{{ item.label }}</span>
                <span class="text-amber-200">{{ item.score }}/10</span>
              </div>
              <div class="score-track">
                <div class="score-fill bg-amber-400" :style="{ width: scoreWidth(item.score) }" />
              </div>
              <div class="mt-2 text-sm text-slate-300">{{ item.reason }}</div>
            </div>
            <div class="grid gap-3 lg:grid-cols-2">
              <div class="rounded-2xl border border-emerald-500/20 bg-emerald-500/5 px-4 py-3">
                <div class="text-xs uppercase tracking-[0.22em] text-emerald-200/70">{{ labels.strongestDesignDecision }}</div>
                <div class="mt-2 text-sm text-slate-100">{{ report.strongest_design_decision }}</div>
              </div>
              <div class="rounded-2xl border border-rose-500/20 bg-rose-500/5 px-4 py-3">
                <div class="text-xs uppercase tracking-[0.22em] text-rose-200/70">{{ labels.biggestArchitecturalRisk }}</div>
                <div class="mt-2 text-sm text-slate-100">{{ report.biggest_architectural_risk }}</div>
              </div>
            </div>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('code')">
            <span>{{ labels.sectionCodeQuality }}</span>
            <span>{{ sectionState.code ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.code" class="analyst-section-body space-y-4">
            <div v-for="item in report.code_quality_scores" :key="item.label" class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
              <div class="mb-2 flex items-center justify-between text-sm">
                <span class="font-semibold text-slate-100">{{ item.label }}</span>
                <span class="text-cyan-200">{{ item.score }}/10</span>
              </div>
              <div class="score-track">
                <div class="score-fill bg-cyan-400" :style="{ width: scoreWidth(item.score) }" />
              </div>
              <div class="mt-2 text-sm text-slate-300">{{ item.reason }}</div>
            </div>
            <div class="grid gap-3 lg:grid-cols-2">
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
                <div class="mb-2 text-xs uppercase tracking-[0.22em] text-mute">{{ labels.threeThingsDoneWell }}</div>
                <ul class="list-disc space-y-2 ps-5 text-sm text-slate-200">
                  <li v-for="item in report.coder_strengths" :key="item">{{ item }}</li>
                </ul>
              </div>
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
                <div class="mb-2 text-xs uppercase tracking-[0.22em] text-mute">{{ labels.threeThingsToFixNext }}</div>
                <ul class="list-disc space-y-2 ps-5 text-sm text-slate-200">
                  <li v-for="item in report.coder_fixes" :key="item">{{ item }}</li>
                </ul>
              </div>
            </div>
            <div class="rounded-2xl border border-amber-500/20 bg-amber-500/5 px-4 py-3 text-sm text-amber-100">
              {{ labels.technicalDebt }}: <strong>{{ report.technical_debt }}</strong>
            </div>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('qa')">
            <span>{{ labels.sectionQa }}</span>
            <span>{{ sectionState.qa ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.qa" class="analyst-section-body space-y-3">
            <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-slate-100">
              {{ report.qa_effectiveness_verdict }}
            </div>
            <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
              <div class="text-xs uppercase tracking-[0.22em] text-mute">{{ labels.missedBugs }}</div>
              <ul class="mt-2 list-disc space-y-2 ps-5 text-sm text-slate-200">
                <li v-for="item in report.missed_bugs" :key="item">{{ item }}</li>
                <li v-if="!report.missed_bugs.length" class="list-none text-mute">{{ labels.noCriticalQaMisses }}</li>
              </ul>
            </div>
            <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-amber-100">
              {{ labels.thoroughness }}: <strong>{{ report.qa_thoroughness }}</strong>
            </div>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('risks')">
            <span>{{ labels.sectionRisks }}</span>
            <span>{{ sectionState.risks ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.risks" class="analyst-section-body space-y-3">
            <article
              v-for="risk in report.hidden_risks"
              :key="risk.title"
              class="rounded-2xl border border-amber-500/25 bg-amber-500/5 px-4 py-4"
            >
              <div class="mb-3 flex items-center gap-2">
                <span class="rounded-full border border-amber-500/30 bg-amber-500/15 px-2 py-1 text-xs font-semibold text-amber-100">
                  ⚠️ {{ risk.title }}
                </span>
              </div>
              <div class="space-y-2 text-sm text-slate-200">
                <div><strong>{{ labels.whatItIs }}:</strong> {{ risk.what_it_is }}</div>
                <div><strong>{{ labels.whyItMatters }}:</strong> {{ risk.why_it_matters }}</div>
                <div><strong>{{ labels.howToFixIt }}:</strong> {{ risk.how_to_fix_it }}</div>
              </div>
            </article>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('performance')">
            <span>{{ labels.sectionPerformance }}</span>
            <span>{{ sectionState.performance ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.performance" class="analyst-section-body">
            <div class="grid gap-3 lg:grid-cols-2">
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-slate-100">
                <div class="text-xs uppercase tracking-[0.22em] text-mute">{{ labels.expectedFpsImpact }}</div>
                <div class="mt-2 text-amber-100">{{ report.performance_forecast.expected_fps_impact }}</div>
              </div>
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-slate-100">
                <div class="text-xs uppercase tracking-[0.22em] text-mute">{{ labels.memoryFootprint }}</div>
                <div class="mt-2">{{ report.performance_forecast.memory_footprint_estimate }}</div>
              </div>
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-slate-100">
                <div class="text-xs uppercase tracking-[0.22em] text-mute">{{ labels.biggestBottleneck }}</div>
                <div class="mt-2">{{ report.performance_forecast.biggest_performance_bottleneck }}</div>
              </div>
              <div class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3 text-sm text-slate-100">
                <div class="text-xs uppercase tracking-[0.22em] text-mute">{{ labels.highestRoiOptimization }}</div>
                <div class="mt-2">{{ report.performance_forecast.highest_roi_optimization }}</div>
              </div>
            </div>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('roadmap')">
            <span>{{ labels.sectionRoadmap }}</span>
            <span>{{ sectionState.roadmap ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.roadmap" class="analyst-section-body space-y-3">
            <article
              v-for="step in report.next_steps"
              :key="step.rank"
              class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-4"
            >
              <div class="mb-2 flex flex-wrap items-center gap-3">
                <span class="text-sm font-semibold text-slate-100">#{{ step.rank }} — {{ step.title }}</span>
                <span class="rounded-full border px-2 py-1 text-[11px] font-semibold" :class="complexityClass(step.complexity)">
                  {{ complexityLabel(step.complexity) }}
                </span>
              </div>
              <div class="text-sm text-slate-300">{{ step.description }}</div>
            </article>
          </div>
        </section>

        <section class="analyst-section">
          <button class="analyst-section-header" @click="toggleSection('overall')">
            <span>{{ labels.sectionOverall }}</span>
            <span>{{ sectionState.overall ? "−" : "+" }}</span>
          </button>
          <div v-show="sectionState.overall" class="analyst-section-body space-y-4">
            <div class="grid gap-3 lg:grid-cols-2">
              <div v-for="(score, key) in report.overall_scores" :key="key" class="rounded-2xl border border-slate-800 bg-slate-950 px-4 py-3">
                <div class="mb-2 flex items-center justify-between text-sm">
                  <span class="font-semibold text-slate-100">{{ prettifyOverallKey(key) }}</span>
                  <span class="text-amber-100">{{ score }}/10</span>
                </div>
                <div class="score-track">
                  <div class="score-fill bg-amber-400" :style="{ width: scoreWidth(score) }" />
                </div>
              </div>
            </div>
            <div class="rounded-3xl border border-amber-500/30 bg-amber-500/8 px-5 py-5">
              <div class="mb-3 text-xs font-semibold uppercase tracking-[0.22em] text-amber-100/80">{{ labels.pipelineVerdict }}</div>
              <div class="space-y-3 text-sm leading-7 text-slate-100">
                <p><strong>{{ labels.finalVerdict }}:</strong> {{ report.final_verdict }}</p>
                <p><strong>{{ labels.proudOf }}:</strong> {{ report.proud_of }}</p>
                <p><strong>{{ labels.mustAddress }}:</strong> {{ report.must_address }}</p>
              </div>
            </div>
          </div>
        </section>
      </template>

      <MarkdownBlock v-else :source="message?.content || ''" />
    </div>
  </article>
</template>
