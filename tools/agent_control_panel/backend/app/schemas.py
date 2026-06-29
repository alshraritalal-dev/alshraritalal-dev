from __future__ import annotations

from typing import Any, Literal
from uuid import uuid4

from pydantic import BaseModel, Field


AgentName = str
RunMode = Literal["auto", "step"]
ExecutionRoute = Literal["chat", "swarm"]
RunStatus = Literal[
    "queued",
    "planning",
    "awaiting_approval",
    "awaiting_override",
    "applying",
    "completed",
    "failed",
    "cancelled",
]
OperationKind = Literal["write_file", "append_file", "delete_file", "make_dir"]
MessageRole = Literal["user", "assistant", "system"]
DetectedLanguage = Literal["en", "ar"]
IntentKind = Literal[
    "create",
    "modify",
    "analyze",
    "debug",
    "explain",
    "review",
    "generate_file",
    "transform_file",
    "screen_read",
    "system_action",
]
DomainKind = Literal["code", "documents", "data", "game_dev", "system", "ui", "automation", "general"]
OutputFormat = Literal["py", "pdf", "docx", "md", "json", "html", "txt", "csv", "image", "zip", "none"]
ComplexityKind = Literal["trivial", "low", "medium", "high", "architectural"]
TimeBudget = Literal["fast", "normal", "thorough"]
RouteKind = Literal[
    "reply_only",
    "analyst_only",
    "architect_only",
    "architect_plus_coder",
    "full_pipeline",
    "file_modification",
    "computer_use_only",
]
PreviewKind = Literal["image", "text", "document", "binary"]

SwarmTaskType = Literal[
    "analysis",
    "planning",
    "coding",
    "verification",
    "terminal",
    "frontend",
    "backend",
    "artifact",
    "response",
]
SwarmTaskStatus = Literal["pending", "working", "done", "error", "blocked", "cancelled"]
SwarmWorkerStatus = Literal["idle", "planning", "working", "verifying", "done", "error", "offline", "restarting"]
SwarmActionKind = Literal[
    "write_file",
    "append_file",
    "run_command",
    "verify_file",
    "spawn_task",
    "reply",
    "artifact_note",
    "noop",
]
SwarmEventLevel = Literal["info", "warning", "error", "success"]


def _new_id() -> str:
    return uuid4().hex


class AgentLogEntry(BaseModel):
    id: str = Field(default_factory=_new_id)
    agent: AgentName
    kind: Literal["status", "result", "error", "stdout", "stderr"] = "status"
    message: str
    timestamp: str


class FileOperation(BaseModel):
    id: str = Field(default_factory=_new_id)
    kind: OperationKind
    path: str
    reason: str = ""
    content: str = ""
    source_path: str | None = None
    encoding: str = "utf-8"
    requires_approval: bool = False
    status: Literal["pending", "applied", "failed", "skipped"] = "pending"
    error: str | None = None


class CommandSpec(BaseModel):
    id: str = Field(default_factory=_new_id)
    command: str
    reason: str = ""
    requires_approval: bool = False
    status: Literal["pending", "applied", "failed", "skipped"] = "pending"
    error: str | None = None
    return_code: int | None = None


class ArchitectPlan(BaseModel):
    objective: str = ""
    execution_strategy: list[str] = Field(default_factory=list)
    deliverables: list[str] = Field(default_factory=list)
    file_targets: list[str] = Field(default_factory=list)
    commands: list[str] = Field(default_factory=list)
    qa_focus: list[str] = Field(default_factory=list)


class CoderPlan(BaseModel):
    summary: str = ""
    operations: list[FileOperation] = Field(default_factory=list)
    commands: list[CommandSpec] = Field(default_factory=list)
    notes: list[str] = Field(default_factory=list)


class QAReview(BaseModel):
    verdict: Literal["approved", "approved_with_fixes", "needs_override"] = "approved"
    summary: str = ""
    fixes: list[str] = Field(default_factory=list)
    corrected_operations: list[FileOperation] = Field(default_factory=list)
    corrected_commands: list[CommandSpec] = Field(default_factory=list)
    rationale: str = ""


class ScoreVerdict(BaseModel):
    label: str
    score: int
    reason: str


class HiddenRisk(BaseModel):
    title: str
    what_it_is: str
    why_it_matters: str
    how_to_fix_it: str


class PerformanceForecast(BaseModel):
    expected_fps_impact: Literal["HIGH", "MEDIUM", "LOW"]
    memory_footprint_estimate: str
    biggest_performance_bottleneck: str
    highest_roi_optimization: str


class RoadmapStep(BaseModel):
    rank: int
    complexity: Literal["EASY", "MEDIUM", "HARD"]
    title: str
    description: str


class OverallScores(BaseModel):
    architecture: int
    code_quality: int
    qa_coverage: int
    overall: int


class AnalystReport(BaseModel):
    language: DetectedLanguage
    executive_summary: str
    architecture_scores: list[ScoreVerdict] = Field(default_factory=list)
    strongest_design_decision: str
    biggest_architectural_risk: str
    code_quality_scores: list[ScoreVerdict] = Field(default_factory=list)
    coder_strengths: list[str] = Field(default_factory=list)
    coder_fixes: list[str] = Field(default_factory=list)
    technical_debt: Literal["LOW", "MEDIUM", "HIGH"]
    qa_effectiveness_verdict: str
    missed_bugs: list[str] = Field(default_factory=list)
    qa_thoroughness: Literal["SHALLOW", "ADEQUATE", "THOROUGH"]
    hidden_risks: list[HiddenRisk] = Field(default_factory=list)
    performance_forecast: PerformanceForecast
    next_steps: list[RoadmapStep] = Field(default_factory=list)
    overall_scores: OverallScores
    final_verdict: str
    proud_of: str
    must_address: str
    full_report_markdown: str


class AnalystTriage(BaseModel):
    language: DetectedLanguage = "en"
    primary_intent: IntentKind = "explain"
    domain: DomainKind = "general"
    requested_output_format: OutputFormat = "none"
    attached_file_path: str = ""
    requires_workspace_access: bool = False
    requires_computer_use: bool = False
    estimated_complexity: ComplexityKind = "low"
    time_budget: TimeBudget = "normal"
    agents_to_run: list[Literal["architect", "coder", "qa", "analyst"]] = Field(default_factory=list)
    skip_agents: list[Literal["architect", "coder", "qa", "analyst"]] = Field(default_factory=list)
    route: RouteKind = "analyst_only"
    reason: str = ""
    user_response_markdown: str = ""
    refined_goal: str = ""


class RunArtifact(BaseModel):
    id: str = Field(default_factory=_new_id)
    absolute_path: str
    source_absolute_path: str | None = None
    file_name: str
    file_extension: str
    mime_type: str
    file_size: int
    generated_by_agent: AgentName
    previewable: bool = False
    preview_kind: PreviewKind = "binary"
    preview_text: str = ""
    description: str = ""
    open_url: str
    download_url: str
    created_at: str


class ExecutionPlan(BaseModel):
    source: Literal["coder", "qa"] = "coder"
    summary: str = ""
    operations: list[FileOperation] = Field(default_factory=list)
    commands: list[CommandSpec] = Field(default_factory=list)


class RunRequest(BaseModel):
    goal: str
    workspace_root: str | None = None
    attached_file_path: str | None = None
    model: str | None = None
    mode: RunMode = "auto"
    locale: Literal["en", "ar", "auto"] = "auto"


class ActionResponse(BaseModel):
    ok: bool = True
    status: RunStatus
    message: str


class WorkspaceSnapshot(BaseModel):
    root: str
    language_profile: dict[str, int] = Field(default_factory=dict)
    tree: list[str] = Field(default_factory=list)
    excerpts: dict[str, str] = Field(default_factory=dict)


class SwarmEvent(BaseModel):
    id: str = Field(default_factory=_new_id)
    level: SwarmEventLevel = "info"
    source: str = "system"
    message: str
    timestamp: str
    details: dict[str, Any] = Field(default_factory=dict)


class SwarmTaskAction(BaseModel):
    kind: SwarmActionKind
    reason: str = ""
    path: str = ""
    content: str = ""
    encoding: str = "utf-8"
    command: str = ""
    cwd: str = ""
    title: str = ""
    prompt: str = ""
    task_type: SwarmTaskType = "analysis"
    dependencies: list[str] = Field(default_factory=list)
    expected_outputs: list[str] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)


class SwarmPlannedTask(BaseModel):
    id: str = Field(default_factory=_new_id)
    title: str
    task_type: SwarmTaskType = "analysis"
    prompt: str
    dependencies: list[str] = Field(default_factory=list)
    priority: int = 50
    expected_outputs: list[str] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)


class SwarmWorkerPlan(BaseModel):
    summary: str = ""
    reply: str = ""
    actions: list[SwarmTaskAction] = Field(default_factory=list)
    completion_notes: list[str] = Field(default_factory=list)
    mark_done: bool = True


class RouterDecision(BaseModel):
    route: ExecutionRoute = "chat"
    reply: str = ""
    reason: str = ""


class SwarmManagerPlan(BaseModel):
    summary: str = ""
    reply: str = ""
    requested_workers: int = 1
    tasks: list[SwarmPlannedTask] = Field(default_factory=list)


class SwarmTask(BaseModel):
    id: str = Field(default_factory=_new_id)
    title: str
    task_type: SwarmTaskType = "analysis"
    prompt: str
    dependencies: list[str] = Field(default_factory=list)
    status: SwarmTaskStatus = "pending"
    priority: int = 50
    assigned_worker_id: str | None = None
    assigned_worker_name: str | None = None
    attempts: int = 0
    max_attempts: int = 3
    created_at: str
    updated_at: str
    started_at: str | None = None
    completed_at: str | None = None
    output_summary: str = ""
    reply: str = ""
    generated_paths: list[str] = Field(default_factory=list)
    artifact_ids: list[str] = Field(default_factory=list)
    verification: dict[str, Any] = Field(default_factory=dict)
    error: str = ""
    metadata: dict[str, Any] = Field(default_factory=dict)


class SwarmWorkerState(BaseModel):
    id: str = Field(default_factory=_new_id)
    name: str
    status: SwarmWorkerStatus = "idle"
    task_id: str | None = None
    current_task: str = ""
    system_prompt: str = ""
    last_output: str = ""
    last_error: str = ""
    started_at: str
    updated_at: str
    last_heartbeat: str
    completed_task_ids: list[str] = Field(default_factory=list)
    generated_paths: list[str] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)


class SwarmRunState(BaseModel):
    run_id: str
    goal: str
    workspace_root: str
    status: str = "queued"
    manager_summary: str = ""
    manager_reply: str = ""
    final_reply: str = ""
    requested_workers: int = 1
    max_workers: int = 20
    created_at: str
    updated_at: str
    restart_requested: bool = False
    restart_reason: str = ""
    dependency_graph: dict[str, list[str]] = Field(default_factory=dict)
    tasks: list[SwarmTask] = Field(default_factory=list)
    workers: list[SwarmWorkerState] = Field(default_factory=list)
    events: list[SwarmEvent] = Field(default_factory=list)
    metrics: dict[str, Any] = Field(default_factory=dict)


class SwarmStateDocument(BaseModel):
    version: int = 1
    updated_at: str
    runs: dict[str, SwarmRunState] = Field(default_factory=dict)


class MessageRecord(BaseModel):
    id: str = Field(default_factory=_new_id)
    role: MessageRole
    agent: str | None = None
    content: str
    timestamp: str
    phase: int | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class RunRecord(BaseModel):
    id: str = Field(default_factory=_new_id)
    title: str = ""
    goal: str
    workspace_root: str
    model: str
    mode: RunMode
    locale: Literal["en", "ar", "auto"]
    execution_route: ExecutionRoute | None = None
    attached_file_path: str | None = None
    detected_language: DetectedLanguage = "en"
    status: RunStatus = "queued"
    created_at: str
    updated_at: str
    cancel_requested: bool = False
    logs: dict[str, list[AgentLogEntry]] = Field(
        default_factory=lambda: {"architect": [], "coder": [], "qa": [], "analyst": [], "system": []}
    )
    messages: list[MessageRecord] = Field(default_factory=list)
    artifacts: list[RunArtifact] = Field(default_factory=list)
    workspace_snapshot: WorkspaceSnapshot | None = None
    architect_plan: ArchitectPlan | None = None
    coder_plan: CoderPlan | None = None
    qa_review: QAReview | None = None
    analyst_report: AnalystReport | None = None
    execution_plan: ExecutionPlan | None = None
    swarm_run: SwarmRunState | None = None
    report_path: str | None = None
    output_path: str | None = None
    error_message: str | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)
