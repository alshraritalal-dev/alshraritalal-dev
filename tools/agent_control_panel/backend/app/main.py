from __future__ import annotations

import asyncio
import logging
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .artifacts import ArtifactService
from .computer_use import ComputerUseService
from .config import get_settings
from .dynamic_orchestrator import DynamicSwarmOrchestrator
from .execution import ExecutionEngine
from .helpers import detect_language_from_text, utc_now_iso
from .llm import OllamaClient
from .schemas import ActionResponse, RunRecord, RunRequest
from .state import RunStore
from .swarm_registry import SwarmRegistry
from .watchdog_manager import WatchdogManager


logger = logging.getLogger(__name__)

settings = get_settings()
store = RunStore(settings.session_root)
llm = OllamaClient(settings.ollama_url)
computer_use = ComputerUseService(settings.ollama_url)
artifacts = ArtifactService(settings)
execution = ExecutionEngine(store, settings, artifacts, computer_use)
registry = SwarmRegistry(settings.swarm_state_path, store)
orchestrator = DynamicSwarmOrchestrator(store, llm, settings, artifacts, computer_use, registry)
watchdog_manager: WatchdogManager | None = None


class RenameBody(BaseModel):
    title: str


class ComputerActionBody(BaseModel):
    action: str
    arguments: dict[str, Any] = Field(default_factory=dict)


class AdminActionResponse(BaseModel):
    ok: bool
    message: str
    supported: bool = True


def _handle_source_change(path: Path) -> None:
    settings.reload_touch_path.write_text(f"{utc_now_iso()}\n{path}\n", encoding="utf-8")
    logger.info("Source change detected by watchdog: %s", path)
    if os.getenv("AGENT_PANEL_SELF_RESTART", "0") == "1":  # pragma: no cover - local process behavior
        os._exit(0)


@asynccontextmanager
async def lifespan(_: FastAPI):
    global watchdog_manager
    watchdog_manager = WatchdogManager([settings.app_root], _handle_source_change)
    watchdog_manager.start()
    yield
    if watchdog_manager is not None:
        watchdog_manager.stop()
    await llm.close()


app = FastAPI(
    title="Local Agent Swarm",
    version="3.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:5173", "http://localhost:5173"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def _resolve_workspace_root(request: RunRequest) -> Path:
    if request.workspace_root:
        candidate = Path(request.workspace_root).expanduser()
    else:
        candidate = settings.project_root
    if not candidate.exists() or not candidate.is_dir():
        raise HTTPException(status_code=400, detail=f"Workspace root does not exist: {candidate}")
    return candidate.resolve()


def _run_summary(run: RunRecord) -> dict[str, Any]:
    return {
        "id": run.id,
        "title": run.title,
        "created_at": run.created_at,
        "updated_at": run.updated_at,
        "status": run.status,
        "execution_route": run.execution_route,
        "message_count": len(run.messages),
        "report_language": run.analyst_report.language if run.analyst_report else run.metadata.get("report_language"),
        "workspace_root": run.workspace_root,
        "swarm": run.swarm_run.model_dump(mode="json") if run.swarm_run is not None else None,
    }


def _resolve_artifact(run_id: str | None, artifact_id: str | None, path: str | None):
    if run_id and artifact_id:
        run = store.get_run(run_id)
        artifact = next((item for item in run.artifacts if item.id == artifact_id), None)
        if artifact is None:
            raise HTTPException(status_code=404, detail="Artifact not found.")
        target = Path(artifact.absolute_path).expanduser().resolve()
        if not target.exists():
            raise HTTPException(status_code=404, detail=f"Artifact file is missing on disk: {target}")
        return artifact, target

    if path:
        candidate = Path(path).expanduser().resolve()
        if not candidate.exists():
            raise HTTPException(status_code=404, detail=f"Artifact file does not exist: {candidate}")
        if settings.artifact_root.resolve() not in [candidate, *candidate.parents]:
            raise HTTPException(status_code=400, detail="Path is outside the managed artifact store.")
        return None, candidate

    raise HTTPException(status_code=400, detail="Provide run_id and artifact_id, or a managed artifact path.")


@app.get("/api/health")
async def health() -> dict[str, Any]:
    llm_status = "ok"
    details: dict[str, Any] = {}
    try:
        details = await llm.health()
    except Exception as exc:
        llm_status = f"unavailable: {exc}"
    return {
        "ok": True,
        "ollama": llm_status,
        "defaultModel": settings.default_model,
        "analystModel": settings.analyst_model,
        "projectRoot": str(settings.project_root),
        "frontendDist": str(settings.frontend_root / "dist"),
        "computerUse": computer_use.describe_capabilities(),
        "swarmStatePath": str(settings.swarm_state_path),
        "watchdog": bool(watchdog_manager is not None),
        "details": details,
    }


@app.get("/api/config")
async def config() -> dict[str, Any]:
    return {
        "defaultModel": settings.default_model,
        "analystModel": settings.analyst_model,
        "projectRoot": str(settings.project_root),
        "workspaceOptions": [str(settings.project_root), str(settings.panel_root)],
        "reportRoot": str(settings.report_root),
        "artifactRoot": str(settings.artifact_root),
        "outputPath": str(settings.output_path),
        "swarmStatePath": str(settings.swarm_state_path),
    }


@app.get("/api/runs")
async def list_runs() -> list[dict[str, Any]]:
    return [_run_summary(run) for run in store.list_runs()]


@app.get("/api/sessions")
async def list_sessions() -> list[dict[str, Any]]:
    return [_run_summary(run) for run in store.list_runs()]


@app.get("/api/runs/{run_id}")
async def get_run(run_id: str) -> dict[str, Any]:
    try:
        return store.get_run(run_id).model_dump(mode="json")
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.get("/api/runs/{run_id}/artifacts")
async def list_run_artifacts(run_id: str) -> list[dict[str, Any]]:
    try:
        run = store.get_run(run_id)
        return [artifact.model_dump(mode="json") for artifact in run.artifacts]
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.get("/api/runs/{run_id}/swarm")
async def get_swarm_run(run_id: str) -> dict[str, Any]:
    try:
        return await orchestrator.get_swarm_payload(run_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.get("/api/runs/{run_id}/swarm/tasks")
async def get_swarm_tasks(run_id: str) -> list[dict[str, Any]]:
    try:
        payload = await orchestrator.get_swarm_payload(run_id)
        return (payload.get("swarm") or {}).get("tasks", [])
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.get("/api/runs/{run_id}/swarm/workers")
async def get_swarm_workers(run_id: str) -> list[dict[str, Any]]:
    try:
        payload = await orchestrator.get_swarm_payload(run_id)
        return (payload.get("swarm") or {}).get("workers", [])
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.get("/api/swarm/state")
async def get_global_swarm_state() -> dict[str, Any]:
    return await orchestrator.get_global_state()


@app.get("/api/sessions/{run_id}")
async def get_session(run_id: str) -> dict[str, Any]:
    return await get_run(run_id)


@app.post("/api/runs")
async def create_run(request: RunRequest) -> dict[str, Any]:
    workspace_root = _resolve_workspace_root(request)
    detected_language = detect_language_from_text(request.goal)
    run = await store.create_run(request, str(workspace_root), request.model or settings.default_model, detected_language)
    await orchestrator.launch(run.id)
    return run.model_dump(mode="json")


@app.post("/api/computer-action")
async def computer_action(body: ComputerActionBody) -> dict[str, Any]:
    try:
        result = await asyncio.to_thread(computer_use.run_action, body.action, **body.arguments)
        return {"ok": True, "action": body.action, "result": result}
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/computer_action")
async def computer_action_alias(body: ComputerActionBody) -> dict[str, Any]:
    return await computer_action(body)


@app.get("/api/artifacts/open")
async def open_artifact(
    run_id: str | None = Query(default=None),
    artifact_id: str | None = Query(default=None),
    path: str | None = Query(default=None),
) -> FileResponse:
    artifact, target = _resolve_artifact(run_id, artifact_id, path)
    media_type = artifact.mime_type if artifact is not None else None
    return FileResponse(target, media_type=media_type)


@app.get("/api/artifacts/download")
async def download_artifact(
    run_id: str | None = Query(default=None),
    artifact_id: str | None = Query(default=None),
    path: str | None = Query(default=None),
) -> FileResponse:
    artifact, target = _resolve_artifact(run_id, artifact_id, path)
    filename = artifact.file_name if artifact is not None else target.name
    media_type = artifact.mime_type if artifact is not None else None
    return FileResponse(target, media_type=media_type, filename=filename)


@app.post("/api/runs/{run_id}/approve-next", response_model=ActionResponse)
async def approve_next(run_id: str) -> ActionResponse:
    run = store.get_run(run_id)
    if run.swarm_run is not None:
        return ActionResponse(status=run.status, message="Swarm mode runs execute autonomously. No pending manual approval.")
    await execution.approve_next(run_id)
    return ActionResponse(status=store.get_run(run_id).status, message="Applied one pending step.")


@app.post("/api/runs/{run_id}/approve-all", response_model=ActionResponse)
async def approve_all(run_id: str) -> ActionResponse:
    run = store.get_run(run_id)
    if run.swarm_run is not None:
        return ActionResponse(status=run.status, message="Swarm mode runs execute autonomously. No pending manual approval.")
    await execution.approve_all(run_id)
    return ActionResponse(status=store.get_run(run_id).status, message="Applied all pending steps.")


@app.post("/api/runs/{run_id}/force-execute", response_model=ActionResponse)
async def force_execute(run_id: str) -> ActionResponse:
    run = store.get_run(run_id)
    if run.swarm_run is not None:
        return ActionResponse(status=run.status, message="Swarm mode is already running with full autonomous execution.")
    await execution.force_execute(run_id)
    return ActionResponse(status=store.get_run(run_id).status, message="Force Execute applied the raw coder plan.")


@app.post("/api/runs/{run_id}/cancel", response_model=ActionResponse)
async def cancel_run(run_id: str) -> ActionResponse:
    try:
        run = await store.mark_cancel_requested(run_id)
        return ActionResponse(status=run.status, message="Cancellation requested.")
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.post("/api/runs/{run_id}/swarm/tasks/{task_id}/retry")
async def retry_swarm_task(run_id: str, task_id: str) -> dict[str, Any]:
    try:
        return await orchestrator.retry_task(run_id, task_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.delete("/api/runs/{run_id}/swarm/state")
async def clear_swarm_state(run_id: str) -> dict[str, Any]:
    try:
        cleared = await orchestrator.clear_swarm_state(run_id)
        return {"ok": cleared, "message": "Swarm state cleared." if cleared else "No swarm state existed for this run."}
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/admin/restart-uvicorn", response_model=AdminActionResponse)
async def restart_uvicorn() -> AdminActionResponse:
    if os.getenv("AGENT_PANEL_SELF_RESTART", "0") != "1":
        return AdminActionResponse(
            ok=False,
            supported=False,
            message="No external supervisor is configured, so Uvicorn cannot be restarted safely from inside the app.",
        )

    async def _restart() -> None:
        await asyncio.sleep(0.2)
        _handle_source_change(settings.reload_touch_path)

    asyncio.create_task(_restart())
    return AdminActionResponse(ok=True, supported=True, message="Uvicorn restart requested.")


@app.patch("/api/runs/{run_id}/title")
async def rename_run(run_id: str, body: RenameBody) -> dict[str, Any]:
    try:
        run = await store.rename_run(run_id, body.title)
        return run.model_dump(mode="json")
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.patch("/api/sessions/{run_id}/title")
async def rename_session(run_id: str, body: RenameBody) -> dict[str, Any]:
    return await rename_run(run_id, body)


@app.delete("/api/runs/{run_id}")
async def delete_run(run_id: str) -> dict[str, Any]:
    try:
        run = store.get_run(run_id)
        if run.status in {"queued", "planning", "applying"}:
            raise HTTPException(status_code=400, detail="Cannot delete a run while it is active. Halt it first.")
        try:
            await orchestrator.clear_swarm_state(run_id)
        except ValueError:
            pass
        await store.delete_run(run_id)
        return {"success": True}
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.delete("/api/sessions/{run_id}")
async def delete_session(run_id: str) -> dict[str, Any]:
    return await delete_run(run_id)


@app.websocket("/ws/runs/{run_id}")
async def run_socket(websocket: WebSocket, run_id: str) -> None:
    await websocket.accept()
    try:
        run = store.get_run(run_id)
    except KeyError:
        await websocket.send_json({"type": "error", "message": f"Unknown run id: {run_id}"})
        await websocket.close(code=4404)
        return

    queue = store.events.subscribe(run_id)
    await websocket.send_json({"type": "state", "run": run.model_dump(mode="json")})
    try:
        while True:
            event = await queue.get()
            await websocket.send_json(event)
    except WebSocketDisconnect:
        store.events.unsubscribe(run_id, queue)


dist_dir = settings.frontend_root / "dist"
if dist_dir.exists():
    app.mount("/assets", StaticFiles(directory=dist_dir / "assets"), name="assets")

    @app.get("/")
    async def serve_index() -> FileResponse:
        return FileResponse(dist_dir / "index.html")
