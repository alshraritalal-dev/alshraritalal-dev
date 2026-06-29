from __future__ import annotations

import asyncio
import json
from pathlib import Path
from typing import Any, Iterable

from .helpers import utc_now_iso
from .schemas import (
    RunRecord,
    SwarmEvent,
    SwarmPlannedTask,
    SwarmRunState,
    SwarmStateDocument,
    SwarmTask,
    SwarmTaskStatus,
    SwarmWorkerState,
)
from .state import RunStore


class SwarmRegistry:
    def __init__(self, path: Path, store: RunStore | None = None) -> None:
        self.path = path
        self.store = store
        self._lock = asyncio.Lock()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.path.exists():
            self._write_document(SwarmStateDocument(updated_at=utc_now_iso()))

    async def read_document(self) -> SwarmStateDocument:
        async with self._lock:
            return self._read_document()

    async def get_run(self, run_id: str) -> SwarmRunState | None:
        async with self._lock:
            document = self._read_document()
            run_state = document.runs.get(run_id)
        return run_state.model_copy(deep=True) if run_state is not None else None

    async def remove_run(self, run_id: str) -> bool:
        async with self._lock:
            document = self._read_document()
            removed = document.runs.pop(run_id, None)
            if removed is not None:
                self._write_document(document)
        if removed is None:
            return False
        await self._clear_run_snapshot(run_id)
        return True

    async def create_or_resume_run(
        self,
        run: RunRecord,
        *,
        requested_workers: int,
        max_workers: int = 20,
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            existing = document.runs.get(run.id)
            if existing is None:
                run_state = SwarmRunState(
                    run_id=run.id,
                    goal=run.goal,
                    workspace_root=run.workspace_root,
                    status=run.status,
                    requested_workers=requested_workers,
                    max_workers=max_workers,
                    created_at=utc_now_iso(),
                    updated_at=utc_now_iso(),
                )
                document.runs[run.id] = run_state
            else:
                run_state = existing
                run_state.goal = run.goal
                run_state.workspace_root = run.workspace_root
                run_state.status = run.status
                run_state.requested_workers = requested_workers
                run_state.max_workers = max_workers
                run_state.updated_at = utc_now_iso()
                self._recover_inflight_state(run_state)
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run.id, snapshot)
        return snapshot

    async def seed_plan(
        self,
        run_id: str,
        *,
        manager_summary: str,
        manager_reply: str,
        requested_workers: int,
        tasks: Iterable[SwarmPlannedTask],
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            run_state.manager_summary = manager_summary
            run_state.manager_reply = manager_reply
            run_state.requested_workers = min(max(1, requested_workers), run_state.max_workers)
            run_state.tasks = [
                SwarmTask(
                    id=planned.id,
                    title=planned.title,
                    task_type=planned.task_type,
                    prompt=planned.prompt,
                    dependencies=list(planned.dependencies),
                    priority=planned.priority,
                    created_at=utc_now_iso(),
                    updated_at=utc_now_iso(),
                    metadata=dict(planned.metadata),
                )
                for planned in tasks
            ]
            run_state.dependency_graph = {task.id: list(task.dependencies) for task in run_state.tasks}
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def add_tasks(self, run_id: str, tasks: Iterable[SwarmPlannedTask]) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            for planned in tasks:
                run_state.tasks.append(
                    SwarmTask(
                        id=planned.id,
                        title=planned.title,
                        task_type=planned.task_type,
                        prompt=planned.prompt,
                        dependencies=list(planned.dependencies),
                        priority=planned.priority,
                        created_at=utc_now_iso(),
                        updated_at=utc_now_iso(),
                        metadata=dict(planned.metadata),
                    )
                )
            run_state.dependency_graph = {task.id: list(task.dependencies) for task in run_state.tasks}
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def register_workers(self, run_id: str, workers: list[SwarmWorkerState]) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            existing = {worker.id: worker for worker in run_state.workers}
            for worker in workers:
                existing[worker.id] = worker
            run_state.workers = sorted(existing.values(), key=lambda item: item.name)
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def claim_next_task(self, run_id: str, worker_id: str, worker_name: str) -> SwarmTask | None:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            worker = self._find_worker(run_state, worker_id)
            ready = [
                task
                for task in run_state.tasks
                if task.status == "pending" and self._dependencies_satisfied(run_state, task.dependencies)
            ]
            ready.sort(key=lambda item: (-item.priority, item.created_at, item.title.lower()))
            if not ready:
                return None
            task = ready[0]
            task.status = "working"
            task.assigned_worker_id = worker_id
            task.assigned_worker_name = worker_name
            task.attempts += 1
            task.started_at = task.started_at or utc_now_iso()
            task.updated_at = utc_now_iso()
            worker.status = "working"
            worker.task_id = task.id
            worker.current_task = task.title
            worker.updated_at = utc_now_iso()
            worker.last_heartbeat = worker.updated_at
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = task.model_copy(deep=True)
            run_snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, run_snapshot)
        return snapshot

    async def update_worker(self, run_id: str, worker_id: str, **changes: Any) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            worker = self._find_worker(run_state, worker_id)
            for key, value in changes.items():
                if hasattr(worker, key):
                    setattr(worker, key, value)
            worker.updated_at = utc_now_iso()
            worker.last_heartbeat = worker.updated_at
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def complete_task(
        self,
        run_id: str,
        task_id: str,
        *,
        worker_id: str,
        output_summary: str = "",
        reply: str = "",
        generated_paths: Iterable[str] | None = None,
        artifact_ids: Iterable[str] | None = None,
        verification: dict[str, Any] | None = None,
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            task = self._find_task(run_state, task_id)
            task.status = "done"
            task.output_summary = output_summary
            task.reply = reply
            task.generated_paths = list(generated_paths or [])
            task.artifact_ids = list(artifact_ids or [])
            task.verification = dict(verification or {})
            task.error = ""
            task.completed_at = utc_now_iso()
            task.updated_at = task.completed_at
            worker = self._find_worker(run_state, worker_id)
            worker.status = "idle"
            worker.task_id = None
            worker.current_task = ""
            worker.last_output = output_summary or reply
            worker.updated_at = utc_now_iso()
            worker.last_heartbeat = worker.updated_at
            if task.id not in worker.completed_task_ids:
                worker.completed_task_ids.append(task.id)
            for path in task.generated_paths:
                if path not in worker.generated_paths:
                    worker.generated_paths.append(path)
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def fail_task(
        self,
        run_id: str,
        task_id: str,
        *,
        worker_id: str,
        error: str,
        retryable: bool,
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            task = self._find_task(run_state, task_id)
            if retryable and task.attempts < task.max_attempts:
                task.status = "pending"
                task.assigned_worker_id = None
                task.assigned_worker_name = None
            else:
                task.status = "error"
            task.error = error
            task.updated_at = utc_now_iso()
            worker = self._find_worker(run_state, worker_id)
            worker.status = "idle" if retryable and task.status == "pending" else "error"
            worker.task_id = None
            worker.current_task = ""
            worker.last_error = error
            worker.updated_at = utc_now_iso()
            worker.last_heartbeat = worker.updated_at
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def retry_task(self, run_id: str, task_id: str) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            task = self._find_task(run_state, task_id)
            if task.status == "working":
                raise ValueError("Cannot retry a task that is currently working.")
            task.status = "pending"
            task.assigned_worker_id = None
            task.assigned_worker_name = None
            task.started_at = None
            task.completed_at = None
            task.output_summary = ""
            task.reply = ""
            task.generated_paths = []
            task.artifact_ids = []
            task.verification = {}
            task.error = ""
            task.updated_at = utc_now_iso()
            if run_state.status in {"failed", "cancelled", "completed"}:
                run_state.status = "applying"
                run_state.final_reply = ""
            for worker in run_state.workers:
                if worker.task_id == task_id:
                    worker.status = "idle"
                    worker.task_id = None
                    worker.current_task = ""
                    worker.updated_at = utc_now_iso()
                    worker.last_heartbeat = worker.updated_at
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def set_run_status(
        self,
        run_id: str,
        status: str,
        *,
        final_reply: str | None = None,
        restart_requested: bool | None = None,
        restart_reason: str | None = None,
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            run_state.status = status
            if final_reply is not None:
                run_state.final_reply = final_reply
            if restart_requested is not None:
                run_state.restart_requested = restart_requested
            if restart_reason is not None:
                run_state.restart_reason = restart_reason
            run_state.updated_at = utc_now_iso()
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    async def append_event(
        self,
        run_id: str,
        *,
        level: str,
        source: str,
        message: str,
        details: dict[str, Any] | None = None,
    ) -> SwarmRunState:
        async with self._lock:
            document = self._read_document()
            run_state = self._require_run(document, run_id)
            run_state.events.append(
                SwarmEvent(
                    level=level,
                    source=source,
                    message=message,
                    timestamp=utc_now_iso(),
                    details=details or {},
                )
            )
            run_state.events = run_state.events[-200:]
            run_state.updated_at = utc_now_iso()
            self._refresh_metrics(run_state)
            self._write_document(document)
            snapshot = run_state.model_copy(deep=True)
        await self._sync_run_snapshot(run_id, snapshot)
        return snapshot

    def _read_document(self) -> SwarmStateDocument:
        payload = json.loads(self.path.read_text(encoding="utf-8"))
        return SwarmStateDocument.model_validate(payload)

    def _write_document(self, document: SwarmStateDocument) -> None:
        document.updated_at = utc_now_iso()
        self.path.write_text(
            json.dumps(document.model_dump(mode="json"), ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    def _require_run(self, document: SwarmStateDocument, run_id: str) -> SwarmRunState:
        run_state = document.runs.get(run_id)
        if run_state is None:
            raise KeyError(f"Unknown swarm run id: {run_id}")
        return run_state

    def _find_task(self, run_state: SwarmRunState, task_id: str) -> SwarmTask:
        for task in run_state.tasks:
            if task.id == task_id:
                return task
        raise KeyError(f"Unknown swarm task id: {task_id}")

    def _find_worker(self, run_state: SwarmRunState, worker_id: str) -> SwarmWorkerState:
        for worker in run_state.workers:
            if worker.id == worker_id:
                return worker
        raise KeyError(f"Unknown swarm worker id: {worker_id}")

    def _dependencies_satisfied(self, run_state: SwarmRunState, dependencies: list[str]) -> bool:
        if not dependencies:
            return True
        statuses = {task.id: task.status for task in run_state.tasks}
        return all(statuses.get(dep) == "done" for dep in dependencies)

    def _recover_inflight_state(self, run_state: SwarmRunState) -> None:
        for task in run_state.tasks:
            if task.status == "working":
                task.status = "pending"
                task.assigned_worker_id = None
                task.assigned_worker_name = None
                task.updated_at = utc_now_iso()
        for worker in run_state.workers:
            if worker.status in {"planning", "working", "verifying"}:
                worker.status = "restarting"
                worker.task_id = None
                worker.current_task = ""
                worker.updated_at = utc_now_iso()
                worker.last_heartbeat = worker.updated_at

    def _refresh_metrics(self, run_state: SwarmRunState) -> None:
        counts = {status: 0 for status in ("pending", "working", "done", "error", "blocked", "cancelled")}
        for task in run_state.tasks:
            counts[task.status] = counts.get(task.status, 0) + 1
        worker_counts: dict[str, int] = {}
        for worker in run_state.workers:
            worker_counts[worker.status] = worker_counts.get(worker.status, 0) + 1
        run_state.metrics = {
            "taskCounts": counts,
            "workerCounts": worker_counts,
            "totalTasks": len(run_state.tasks),
            "totalWorkers": len(run_state.workers),
        }
        run_state.updated_at = utc_now_iso()

    async def _sync_run_snapshot(self, run_id: str, run_state: SwarmRunState) -> None:
        if self.store is None:
            return
        try:
            run = self.store.get_run(run_id)
        except KeyError:
            return
        run.swarm_run = run_state
        await self.store.save_run(run)
        await self.store.publish_swarm_state(run_id, run_state.model_dump(mode="json"))

    async def _clear_run_snapshot(self, run_id: str) -> None:
        if self.store is None:
            return
        try:
            run = self.store.get_run(run_id)
        except KeyError:
            return
        run.swarm_run = None
        await self.store.save_run(run)
        await self.store.publish_swarm_state(run_id, None)
