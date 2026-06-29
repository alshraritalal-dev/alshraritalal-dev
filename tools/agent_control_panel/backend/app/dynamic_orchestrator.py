from __future__ import annotations

import asyncio
import json
from pathlib import Path
import re
from typing import Any

from .artifacts import ArtifactService
from .computer_use import ComputerUseService
from .config import Settings
from .helpers import detect_language_from_text, extract_json_document, summarize_title
from .llm import OllamaClient
from .schemas import (
    RouterDecision,
    RunRecord,
    SwarmManagerPlan,
    SwarmPlannedTask,
    SwarmTask,
    SwarmTaskAction,
    SwarmWorkerPlan,
    SwarmWorkerState,
)
from .state import RunStore
from .swarm_prompts import (
    direct_chat_system_prompt,
    swarm_manager_system_prompt,
    swarm_repair_system_prompt,
    swarm_router_system_prompt,
    swarm_worker_system_prompt,
)
from .swarm_registry import SwarmRegistry
from .workspace import detect_workspace_snapshot


class WorkerActionError(RuntimeError):
    def __init__(self, action: SwarmTaskAction, message: str, *, retryable: bool = True) -> None:
        super().__init__(message)
        self.action = action
        self.retryable = retryable


class DynamicSwarmOrchestrator:
    def __init__(
        self,
        store: RunStore,
        llm: OllamaClient,
        settings: Settings,
        artifacts: ArtifactService,
        computer_use: ComputerUseService,
        registry: SwarmRegistry,
    ) -> None:
        self.store = store
        self.llm = llm
        self.settings = settings
        self.artifacts = artifacts
        self.computer_use = computer_use
        self.registry = registry
        self._active_jobs: dict[str, asyncio.Task[None]] = {}
        self._job_lock = asyncio.Lock()

    async def launch(self, run_id: str, *, resume: bool = False) -> bool:
        async with self._job_lock:
            current = self._active_jobs.get(run_id)
            if current is not None and not current.done():
                return False
            task = asyncio.create_task(self._run_job(run_id, resume=resume))
            self._active_jobs[run_id] = task
            task.add_done_callback(lambda _: self._active_jobs.pop(run_id, None))
            return True

    async def start(self, run_id: str) -> None:
        run = self.store.get_run(run_id)
        run.status = "planning"
        await self.store.save_run(run)

        try:
            decision = await self._route_run(run)
            run = self.store.get_run(run_id)
            run.execution_route = decision.route
            run.metadata["route_reason"] = decision.reason
            run.metadata["swarmMode"] = decision.route == "swarm"
            await self.store.save_run(run)
            await self.store.publish_route_selected(run.id, decision.route, decision.reason)

            if decision.route == "chat":
                await self._complete_chat_run(run.id, decision)
                return

            await self._start_swarm_run(run.id)
        except asyncio.CancelledError:
            await self._mark_run_cancelled(run_id, "Run cancelled by user.")
        except Exception as exc:
            await self._mark_run_failed(run_id, str(exc))

    async def resume(self, run_id: str) -> None:
        run = self.store.get_run(run_id)
        run.execution_route = run.execution_route or "swarm"
        run.cancel_requested = False
        run.error_message = None
        run.status = "applying"
        if run.workspace_snapshot is None:
            run.workspace_snapshot = detect_workspace_snapshot(Path(run.workspace_root), run.goal)
        await self.store.save_run(run)
        await self.store.publish_route_selected(run.id, "swarm", run.metadata.get("route_reason", "Resumed swarm run."))
        await self._resume_swarm_run(run.id)

    async def retry_task(self, run_id: str, task_id: str) -> dict[str, Any]:
        run = self.store.get_run(run_id)
        if (run.execution_route or ("swarm" if run.swarm_run else "chat")) != "swarm":
            raise ValueError("Only swarm runs support task retry.")
        await self.registry.retry_task(run_id, task_id)
        await self.registry.append_event(
            run_id,
            level="warning",
            source="manager",
            message=f"Task {task_id} was re-queued for retry.",
            details={"taskId": task_id},
        )
        run = self.store.get_run(run_id)
        run.cancel_requested = False
        run.error_message = None
        run.status = "applying"
        await self.store.save_run(run)
        await self.registry.set_run_status(run_id, "applying", final_reply="")
        await self.launch(run_id, resume=True)
        return await self.get_swarm_payload(run_id)

    async def clear_swarm_state(self, run_id: str) -> bool:
        run = self.store.get_run(run_id)
        if run.status in {"queued", "planning", "applying"}:
            raise ValueError("Cannot clear swarm state while the run is active. Halt the swarm first.")
        cleared = await self.registry.remove_run(run_id)
        if not cleared:
            return False
        run = self.store.get_run(run_id)
        run.swarm_run = None
        await self.store.save_run(run)
        return True

    async def get_swarm_state(self, run_id: str) -> dict[str, Any]:
        state = await self.registry.get_run(run_id)
        if state is None:
            raise KeyError(f"Unknown swarm run id: {run_id}")
        return state.model_dump(mode="json")

    async def get_swarm_payload(self, run_id: str) -> dict[str, Any]:
        run = self.store.get_run(run_id)
        route = run.execution_route or ("swarm" if run.swarm_run else "chat")
        state = await self.registry.get_run(run_id) if route == "swarm" else None
        return {
            "route": route,
            "swarm": state.model_dump(mode="json") if state is not None else None,
        }

    async def get_global_state(self) -> dict[str, Any]:
        document = await self.registry.read_document()
        return document.model_dump(mode="json")

    async def _run_job(self, run_id: str, *, resume: bool) -> None:
        if resume:
            await self.resume(run_id)
        else:
            await self.start(run_id)

    async def _route_run(self, run: RunRecord) -> RouterDecision:
        attachment_context = self._attachment_context(run)
        raw = await self.llm.chat(
            model=run.model,
            messages=[
                {"role": "system", "content": swarm_router_system_prompt()},
                {
                    "role": "user",
                    "content": (
                        f"Goal:\n{run.goal}\n\n"
                        f"Workspace root:\n{run.workspace_root}\n\n"
                        f"Attached file path:\n{run.attached_file_path or '(none)'}\n\n"
                        f"{attachment_context}\n\n"
                        "Classify this request now."
                    ),
                },
            ],
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )
        try:
            decision = RouterDecision.model_validate(extract_json_document(raw))
        except Exception:
            decision = self._fallback_route(run)
        decision.reason = decision.reason.strip() or (
            "Direct informational response." if decision.route == "chat" else "Actionable engineering work detected."
        )
        return decision

    async def _complete_chat_run(self, run_id: str, decision: RouterDecision) -> None:
        run = self.store.get_run(run_id)
        await self.store.publish_agent_start(run.id, "Manager", 0)
        raw_reply = await self.llm.chat(
            model=run.model,
            messages=[
                {"role": "system", "content": direct_chat_system_prompt()},
                {
                    "role": "user",
                    "content": (
                        f"User message:\n{run.goal}\n\n"
                        f"Attached file path:\n{run.attached_file_path or '(none)'}\n\n"
                        f"{self._attachment_context(run)}\n\n"
                        "Reply now."
                    ),
                },
            ],
            on_token=lambda token: self.store.stream_token(run.id, "manager", token, None),
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )
        reply = raw_reply.strip() or decision.reply.strip() or (
            "كيف أقدر أخدمك؟" if detect_language_from_text(run.goal) == "ar" else "How can I help?"
        )
        await self.store.add_message(
            run.id,
            role="assistant",
            agent="Manager",
            content=reply,
            metadata={"kind": "chat_reply", "routeReason": decision.reason},
        )
        await self.store.publish_agent_done(run.id, "Manager", 0, reply)
        run = self.store.get_run(run.id)
        run.status = "completed"
        run.error_message = None
        await self.store.save_run(run)
        await self.store.publish_pipeline_complete(run.id)

    async def _start_swarm_run(self, run_id: str) -> None:
        run = self.store.get_run(run_id)
        run.workspace_snapshot = detect_workspace_snapshot(Path(run.workspace_root), run.goal)
        await self.store.save_run(run)

        manager_plan = await self._plan_run(run)
        requested_workers = min(max(1, manager_plan.requested_workers), 20)

        await self.registry.create_or_resume_run(run, requested_workers=requested_workers, max_workers=20)
        await self.registry.seed_plan(
            run.id,
            manager_summary=manager_plan.summary,
            manager_reply=manager_plan.reply,
            requested_workers=requested_workers,
            tasks=manager_plan.tasks,
        )
        await self.registry.append_event(
            run.id,
            level="info",
            source="manager",
            message="Swarm plan initialized.",
            details={"requestedWorkers": requested_workers, "taskCount": len(manager_plan.tasks)},
        )

        kickoff = manager_plan.reply or manager_plan.summary or "Swarm execution started."
        await self.store.add_message(
            run.id,
            role="assistant",
            agent="Manager",
            content=kickoff,
            metadata={"kind": "swarm_manager"},
        )

        workers = self._build_workers(requested_workers)
        await self.registry.register_workers(run.id, workers)

        run = self.store.get_run(run.id)
        run.status = "applying"
        run.error_message = None
        run.metadata["status_summary"] = {
            "mode": "[done] Architecture     : Dynamic Swarm",
            "workers": f"[done] Worker pool      : {requested_workers}",
            "tasks": f"[done] Initial tasks    : {len(manager_plan.tasks)}",
            "registry": f"[done] Shared state     : {self.settings.swarm_state_path}",
        }
        await self.store.save_run(run)
        await self.registry.set_run_status(run.id, "applying")

        await self._process_swarm(run.id, workers)

    async def _resume_swarm_run(self, run_id: str) -> None:
        state = await self.registry.get_run(run_id)
        if state is None:
            raise RuntimeError(f"Cannot resume swarm run {run_id}: registry state is missing.")
        workers = state.workers or self._build_workers(min(max(1, state.requested_workers), 20))
        if not state.workers:
            await self.registry.register_workers(run_id, workers)
        await self.registry.set_run_status(run_id, "applying", final_reply="")
        await self._process_swarm(run_id, workers)

    async def _process_swarm(self, run_id: str, workers: list[SwarmWorkerState]) -> None:
        worker_tasks = [asyncio.create_task(self._worker_loop(run_id, worker)) for worker in workers]
        await asyncio.gather(*worker_tasks)

        if self.store.get_run(run_id).cancel_requested:
            await self._mark_run_cancelled(run_id, "Swarm run cancelled by user.")
            return

        final_state = await self.registry.get_run(run_id)
        if final_state is None:
            raise RuntimeError(f"Swarm state disappeared for run {run_id}")

        final_reply = await self._compose_final_reply(run_id, final_state)
        has_errors = any(task.status == "error" for task in final_state.tasks)
        unsettled = [task for task in final_state.tasks if task.status in {"pending", "working"}]
        if unsettled and not has_errors:
            has_errors = True

        run = self.store.get_run(run_id)
        run.status = "failed" if has_errors else "completed"
        if has_errors and not run.error_message:
            run.error_message = "One or more swarm tasks failed or remained unresolved."
        else:
            run.error_message = None
        await self.store.save_run(run)
        await self.registry.set_run_status(run.id, "failed" if has_errors else "completed", final_reply=final_reply)
        await self.store.add_message(
            run.id,
            role="assistant",
            agent="Swarm",
            content=final_reply,
            metadata={"kind": "swarm_final"},
        )
        if has_errors:
            await self.store.publish_pipeline_error(run.id, run.error_message or "Swarm run failed.")
        else:
            await self.store.publish_pipeline_complete(run.id)

    async def _plan_run(self, run: RunRecord) -> SwarmManagerPlan:
        await self.store.publish_agent_start(run.id, "Manager", 0)
        raw = await self.llm.chat(
            model=run.model,
            messages=[
                {"role": "system", "content": swarm_manager_system_prompt()},
                {
                    "role": "user",
                    "content": (
                        f"Goal:\n{run.goal}\n\n"
                        f"Workspace root:\n{run.workspace_root}\n\n"
                        f"Attached file path:\n{run.attached_file_path or '(none)'}\n\n"
                        f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
                        "Return the manager plan now."
                    ),
                },
            ],
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )

        try:
            plan = SwarmManagerPlan.model_validate(extract_json_document(raw))
        except Exception:
            plan = self._fallback_plan(run)

        if not plan.tasks:
            plan = self._fallback_plan(run)
        if not plan.reply:
            language = detect_language_from_text(run.goal)
            plan.reply = (
                "بدأت تشغيل السوارم. سأوزع المهام على العمال وأرجع لك بالمخرجات الفعلية."
                if language == "ar"
                else "Swarm execution started. I am distributing the work across local workers now."
            )
        await self.store.publish_agent_done(run.id, "Manager", 0, plan.reply)
        return plan

    def _fallback_plan(self, run: RunRecord) -> SwarmManagerPlan:
        language = detect_language_from_text(run.goal)
        inferred_file = self._infer_requested_file(run.goal)
        summary_title = summarize_title(run.goal, limit=56)
        goal_lower = run.goal.lower()

        if inferred_file:
            coding_task = SwarmPlannedTask(
                title=f"Create or update {inferred_file}",
                task_type="coding",
                prompt=(
                    f"Produce the requested file `{inferred_file}` or update it to satisfy the goal.\n\n"
                    f"Goal:\n{run.goal}\n\n"
                    "Use concrete file actions and verify the result."
                ),
                priority=100,
                expected_outputs=[inferred_file],
            )
            verification_task = SwarmPlannedTask(
                title=f"Verify {inferred_file}",
                task_type="verification",
                prompt=f"Verify that `{inferred_file}` exists, is valid, and satisfies the goal.\n\nGoal:\n{run.goal}",
                dependencies=[coding_task.id],
                priority=80,
                expected_outputs=[inferred_file],
            )
            return SwarmManagerPlan(
                summary=f"Recovered a grounded plan for {inferred_file}.",
                reply=(
                    f"بدأت تنفيذ الملف المطلوب `{inferred_file}` مع التحقق منه."
                    if language == "ar"
                    else f"I recovered the task graph and started implementing `{inferred_file}` with verification."
                ),
                requested_workers=2,
                tasks=[coding_task, verification_task],
            )

        analysis_signals = (
            "analyze workspace",
            "inspect project",
            "scan project",
            "review codebase",
            "audit",
            "حلل",
            "افحص",
            "راجع",
        )
        if any(signal in goal_lower for signal in analysis_signals):
            return SwarmManagerPlan(
                summary="Recovered an analysis-only swarm plan.",
                reply=(
                    "بدأت تحليل مساحة العمل وبناء ملخص تنفيذي."
                    if language == "ar"
                    else "I recovered the task graph and started a workspace analysis pass."
                ),
                requested_workers=1,
                tasks=[
                    SwarmPlannedTask(
                        title=f"Analyze request: {summary_title}",
                        task_type="analysis",
                        prompt=(
                            "Inspect the relevant workspace files and summarize the request with concrete findings.\n\n"
                            f"Goal:\n{run.goal}"
                        ),
                        priority=90,
                    )
                ],
            )

        actionable_signals = (
            "create",
            "write",
            "build",
            "implement",
            "fix",
            "debug",
            "refactor",
            "update",
            "modify",
            "generate",
            "save",
            "frontend",
            "backend",
            "api",
            "component",
            "server",
            "script",
            "run",
            "execute",
            "install",
            "صلح",
            "أنشئ",
            "انشئ",
            "اكتب",
            "نفذ",
            "شغل",
            "عدل",
            "حدّث",
            "حدث",
        )
        if any(signal in goal_lower for signal in actionable_signals):
            task_title = f"Execute request: {summary_title}"
            coding_task = SwarmPlannedTask(
                title=task_title,
                task_type="coding",
                prompt=(
                    "Execute the user's engineering request with real file or terminal actions.\n\n"
                    f"Goal:\n{run.goal}\n\n"
                    "Keep the implementation grounded in the workspace and verify the result."
                ),
                priority=100,
            )
            verification_task = SwarmPlannedTask(
                title=f"Verify request: {summary_title}",
                task_type="verification",
                prompt=(
                    "Review the work produced by the execution task, verify changed outputs, and report defects if any remain.\n\n"
                    f"Goal:\n{run.goal}"
                ),
                dependencies=[coding_task.id],
                priority=75,
            )
            return SwarmManagerPlan(
                summary=f"Recovered an actionable plan for: {summary_title}.",
                reply=(
                    "استعدت مسار تنفيذ واضح للمهمة وبدأت التشغيل."
                    if language == "ar"
                    else "I recovered a grounded execution path for the request and started the swarm."
                ),
                requested_workers=2,
                tasks=[coding_task, verification_task],
            )

        raise RuntimeError(
            "Manager planning returned invalid output and no actionable fallback task graph could be inferred from the request."
        )

    def _build_workers(self, count: int) -> list[SwarmWorkerState]:
        now = self._now()
        workers: list[SwarmWorkerState] = []
        for index in range(1, count + 1):
            workers.append(
                SwarmWorkerState(
                    name=f"Worker {index:02d}",
                    status="idle",
                    started_at=now,
                    updated_at=now,
                    last_heartbeat=now,
                )
            )
        return workers

    async def _worker_loop(self, run_id: str, worker: SwarmWorkerState) -> None:
        await self.registry.update_worker(run_id, worker.id, status="idle")
        while True:
            run = self.store.get_run(run_id)
            if run.cancel_requested:
                return
            state = await self.registry.get_run(run_id)
            if state is None:
                return
            if self._swarm_settled(state):
                return

            task = await self.registry.claim_next_task(run_id, worker.id, worker.name)
            if task is None:
                await self.registry.update_worker(run_id, worker.id, status="idle")
                if self._swarm_deadlocked(state):
                    return
                await asyncio.sleep(0.4)
                continue

            try:
                await self._run_task(run_id, worker, task)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                await self.registry.append_event(
                    run_id,
                    level="error",
                    source=worker.name,
                    message=f"Task '{task.title}' failed.",
                    details={"error": str(exc), "taskId": task.id},
                )
                await self.registry.fail_task(run_id, task.id, worker_id=worker.id, error=str(exc), retryable=False)

    async def _run_task(self, run_id: str, worker: SwarmWorkerState, task: SwarmTask) -> None:
        await self.store.publish_agent_start(run_id, worker.name, 0)
        await self.registry.update_worker(run_id, worker.id, status="planning", current_task=task.title)
        plan = await self._prepare_worker_plan(run_id, worker, task)
        execution = await self._execute_plan(run_id, worker, task, plan)
        await self.registry.complete_task(
            run_id,
            task.id,
            worker_id=worker.id,
            output_summary=execution["summary"],
            reply=execution["reply"],
            generated_paths=execution["generated_paths"],
            artifact_ids=execution["artifact_ids"],
            verification=execution["verification"],
        )

        message_parts = [f"## {task.title}", execution["summary"]]
        if execution["reply"]:
            message_parts.append(execution["reply"])
        if execution["generated_paths"]:
            rendered = "\n".join(f"- `{Path(path).name}`" for path in execution["generated_paths"])
            message_parts.append(f"### Generated Files\n{rendered}")
        await self.store.add_message(
            run_id,
            role="assistant",
            agent=worker.name,
            content="\n\n".join(part for part in message_parts if part).strip(),
            metadata={"kind": "swarm_worker", "taskId": task.id, "taskType": task.task_type},
        )
        await self.store.publish_agent_done(run_id, worker.name, 0, execution["summary"])

    async def _prepare_worker_plan(self, run_id: str, worker: SwarmWorkerState, task: SwarmTask) -> SwarmWorkerPlan:
        run = self.store.get_run(run_id)
        state = await self.registry.get_run(run_id)
        if state is None:
            raise RuntimeError(f"Missing swarm state for run {run_id}")
        system_prompt = swarm_worker_system_prompt(worker.name, task.task_type)
        dependency_context = self._dependency_context(state, task)
        attachment_context = self._attachment_context(run)
        user_prompt = (
            f"Run goal:\n{run.goal}\n\n"
            f"Task title:\n{task.title}\n\n"
            f"Task instructions:\n{task.prompt}\n\n"
            f"Workspace root:\n{run.workspace_root}\n\n"
            f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
            f"Dependency context:\n{dependency_context}\n\n"
            f"{attachment_context}\n\n"
            "Return the worker plan now."
        )
        raw = await self.llm.chat(
            model=run.model,
            messages=[{"role": "system", "content": system_prompt}, {"role": "user", "content": user_prompt}],
            should_cancel=lambda: self.store.get_run(run_id).cancel_requested,
        )
        try:
            plan = SwarmWorkerPlan.model_validate(extract_json_document(raw))
        except Exception:
            plan = self._fallback_worker_plan(run, task)
        if not plan.actions:
            plan = self._fallback_worker_plan(run, task)
        plan = self._coerce_plan_for_task(run, task, plan)
        await self.registry.update_worker(run_id, worker.id, status="working", system_prompt=system_prompt, last_output=plan.summary)
        return plan

    def _fallback_worker_plan(self, run: RunRecord, task: SwarmTask) -> SwarmWorkerPlan:
        if task.task_type == "response":
            language = detect_language_from_text(run.goal)
            reply = "كيف أقدر أخدمك؟" if language == "ar" else "How can I help?"
            return SwarmWorkerPlan(summary="Direct response prepared.", actions=[SwarmTaskAction(kind="reply", content=reply)])
        inferred_file = self._infer_requested_file(run.goal) or self._infer_requested_file(task.prompt)
        inferred_content = self._infer_literal_content(run.goal) or self._infer_literal_content(task.prompt)
        if task.task_type == "coding" and inferred_file:
            return SwarmWorkerPlan(
                summary=f"Fallback created {inferred_file}.",
                reply=f"Created and verified `{inferred_file}`.",
                actions=[
                    SwarmTaskAction(
                        kind="write_file",
                        path=inferred_file,
                        content=inferred_content or "Generated by swarm fallback.\n",
                        reason="Fallback created a real file because worker JSON was invalid.",
                    ),
                    SwarmTaskAction(kind="verify_file", path=inferred_file, reason="Fallback verification after write."),
                ],
            )
        if task.task_type == "verification" and inferred_file:
            return SwarmWorkerPlan(
                summary=f"Fallback verified {inferred_file}.",
                reply=f"Verified `{inferred_file}`.",
                actions=[SwarmTaskAction(kind="verify_file", path=inferred_file, reason="Fallback verification for inferred output file.")],
            )
        return SwarmWorkerPlan(
            summary="Worker fallback plan generated.",
            actions=[SwarmTaskAction(kind="reply", content=f"Task ready for manual follow-up: {task.title}")],
        )

    async def _execute_plan(
        self,
        run_id: str,
        worker: SwarmWorkerState,
        task: SwarmTask,
        plan: SwarmWorkerPlan,
        *,
        repair_depth: int = 0,
    ) -> dict[str, Any]:
        generated_paths: list[str] = []
        artifact_ids: list[str] = []
        verification: dict[str, Any] = {}
        reply_parts: list[str] = []
        action_results: list[dict[str, Any]] = []

        try:
            for action in plan.actions:
                result = await self._execute_action(run_id, worker, task, action, generated_paths, artifact_ids, verification)
                action_results.append(result)
                if action.kind == "reply":
                    reply_parts.append(action.content or result.get("content", ""))
        except WorkerActionError as exc:
            if exc.retryable and repair_depth < 2:
                repaired = await self._repair_worker_plan(run_id, worker, task, plan, exc)
                return await self._execute_plan(run_id, worker, task, repaired, repair_depth=repair_depth + 1)
            raise RuntimeError(str(exc)) from exc

        summary = plan.summary or f"Completed task: {task.title}"
        if not reply_parts and plan.reply:
            reply_parts.append(plan.reply)
        if not reply_parts and task.task_type == "response":
            reply_parts.append(summary)
        return {
            "summary": summary,
            "reply": "\n\n".join(part for part in reply_parts if part).strip(),
            "generated_paths": generated_paths,
            "artifact_ids": artifact_ids,
            "verification": verification,
            "action_results": action_results,
        }

    async def _repair_worker_plan(
        self,
        run_id: str,
        worker: SwarmWorkerState,
        task: SwarmTask,
        previous_plan: SwarmWorkerPlan,
        failure: WorkerActionError,
    ) -> SwarmWorkerPlan:
        run = self.store.get_run(run_id)
        raw = await self.llm.chat(
            model=run.model,
            messages=[
                {"role": "system", "content": swarm_repair_system_prompt()},
                {
                    "role": "user",
                    "content": (
                        f"Task title: {task.title}\n\n"
                        f"Task prompt:\n{task.prompt}\n\n"
                        f"Previous plan:\n{json.dumps(previous_plan.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                        f"Failed action:\n{json.dumps(failure.action.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                        f"Failure:\n{str(failure)}\n\n"
                        "Return a corrected worker plan JSON only."
                    ),
                },
            ],
            should_cancel=lambda: self.store.get_run(run_id).cancel_requested,
        )
        try:
            repaired = SwarmWorkerPlan.model_validate(extract_json_document(raw))
        except Exception as exc:
            raise RuntimeError(f"Worker repair failed for task '{task.title}': {exc}") from exc
        await self.registry.append_event(
            run_id,
            level="warning",
            source=worker.name,
            message=f"Worker repaired plan for task '{task.title}'.",
            details={"taskId": task.id},
        )
        return repaired

    async def _execute_action(
        self,
        run_id: str,
        worker: SwarmWorkerState,
        task: SwarmTask,
        action: SwarmTaskAction,
        generated_paths: list[str],
        artifact_ids: list[str],
        verification: dict[str, Any],
    ) -> dict[str, Any]:
        kind = action.kind
        if kind in {"write_file", "append_file"}:
            return await self._execute_file_write(
                run_id,
                worker,
                action,
                generated_paths,
                artifact_ids,
                verification,
                append=kind == "append_file",
            )
        if kind == "verify_file":
            if not action.path:
                raise WorkerActionError(action, "verify_file action is missing a path.")
            verify_result = await asyncio.to_thread(self.computer_use.verify_file, self._resolve_path(run_id, action.path))
            artifact_id = await self._maybe_stage_artifact(run_id, worker.name, Path(verify_result["path"]), artifact_ids)
            if artifact_id and artifact_id not in artifact_ids:
                artifact_ids.append(artifact_id)
            verification[action.path] = verify_result
            await self._publish_tool_result(run_id, worker, "verify_file", verify_result)
            return verify_result
        if kind == "run_command":
            return await self._execute_command(run_id, worker, task, action)
        if kind == "spawn_task":
            if not action.title or not action.prompt:
                raise WorkerActionError(action, "spawn_task requires both title and prompt.", retryable=False)
            planned = SwarmPlannedTask(
                title=action.title,
                task_type=action.task_type,
                prompt=action.prompt,
                dependencies=[task.id, *action.dependencies],
                priority=int(action.metadata.get("priority", 60)),
                expected_outputs=list(action.expected_outputs),
                metadata=dict(action.metadata),
            )
            await self.registry.add_tasks(run_id, [planned])
            result = {"ok": True, "action": "spawn_task", "taskId": planned.id, "title": planned.title}
            await self._publish_tool_result(run_id, worker, "spawn_task", result)
            return result
        if kind == "reply":
            result = {"ok": True, "action": "reply", "content": action.content}
            await self._publish_tool_result(run_id, worker, "reply", result)
            return result
        if kind in {"artifact_note", "noop"}:
            result = {"ok": True, "action": kind, "reason": action.reason}
            await self._publish_tool_result(run_id, worker, kind, result)
            return result
        raise WorkerActionError(action, f"Unsupported swarm action: {kind}", retryable=False)

    async def _execute_file_write(
        self,
        run_id: str,
        worker: SwarmWorkerState,
        action: SwarmTaskAction,
        generated_paths: list[str],
        artifact_ids: list[str],
        verification: dict[str, Any],
        *,
        append: bool,
    ) -> dict[str, Any]:
        if not action.path:
            raise WorkerActionError(action, "write action is missing a path.")
        target = self._resolve_path(run_id, action.path)
        write_result = await asyncio.to_thread(self.computer_use.write_file, target, action.content, action.encoding, append)
        try:
            artifact_verification = await asyncio.to_thread(self.artifacts.verify_output, Path(write_result["path"]))
        except Exception as exc:
            raise WorkerActionError(action, f"File verification failed for {target}: {exc}") from exc
        verification[target] = artifact_verification
        if target not in generated_paths:
            generated_paths.append(target)
        artifact_id = await self._maybe_stage_artifact(run_id, worker.name, Path(write_result["path"]), artifact_ids)
        if artifact_id and artifact_id not in artifact_ids:
            artifact_ids.append(artifact_id)
        await self._publish_tool_result(
            run_id,
            worker,
            "write_file" if not append else "append_file",
            {"write": write_result, "verify": artifact_verification},
        )
        return {"write": write_result, "verify": artifact_verification}

    async def _execute_command(
        self,
        run_id: str,
        worker: SwarmWorkerState,
        task: SwarmTask,
        action: SwarmTaskAction,
    ) -> dict[str, Any]:
        if not action.command:
            raise WorkerActionError(action, "run_command action is missing a command.")
        loop = asyncio.get_running_loop()

        def on_output(packet: dict[str, Any]) -> None:
            future = asyncio.run_coroutine_threadsafe(
                self._publish_shell_output(run_id, self._worker_agent_key(worker), packet),
                loop,
            )
            try:
                future.result(timeout=15)
            except Exception:
                pass

        result = await asyncio.to_thread(
            self.computer_use.run_shell_command,
            action.command,
            cwd=action.cwd or self.store.get_run(run_id).workspace_root,
            on_output=on_output,
        )
        await self._publish_tool_result(run_id, worker, "run_shell_command", result)
        if not result.get("ok"):
            stderr = result.get("stderr") or result.get("stdout") or "Unknown shell failure."
            raise WorkerActionError(action, f"Command failed for task '{task.title}': {stderr}")
        return result

    async def _publish_shell_output(self, run_id: str, agent: str, packet: dict[str, Any]) -> None:
        channel = str(packet.get("channel") or "stdout")
        line = str(packet.get("line") or "").rstrip()
        if not line:
            return
        kind = "stderr" if channel == "stderr" else "stdout"
        await self.store.append_log(run_id, agent.lower().replace(" ", "_"), line, kind=kind)

    async def _publish_tool_result(self, run_id: str, worker: SwarmWorkerState, tool: str, result: dict[str, Any]) -> None:
        log_entry = await self.store.append_log(
            run_id,
            self._worker_agent_key(worker),
            f"{tool} -> {json.dumps(result, ensure_ascii=False)[:1800]}",
            kind="result",
        )
        await self.store.publish_tool_executed(
            run_id,
            agent=worker.name,
            agent_key=self._worker_agent_key(worker),
            tool=tool,
            result=result,
            log_entry=log_entry,
        )

    async def _maybe_stage_artifact(self, run_id: str, generated_by_agent: str, path: Path, artifact_ids: list[str]) -> str | None:
        if not path.exists():
            return None
        artifact = await asyncio.to_thread(
            self.artifacts.stage_file,
            run_id,
            path,
            generated_by_agent=generated_by_agent,
            workspace_root=self.store.get_run(run_id).workspace_root,
        )
        await self.store.add_artifact(run_id, artifact)
        return artifact.id

    def _resolve_path(self, run_id: str, raw_path: str) -> str:
        run = self.store.get_run(run_id)
        candidate = Path(raw_path).expanduser()
        if candidate.is_absolute():
            return str(candidate.resolve())
        return str((Path(run.workspace_root).resolve() / candidate).resolve())

    def _dependency_context(self, state: Any, task: SwarmTask) -> str:
        if not task.dependencies:
            return "(none)"
        by_id = {item.id: item for item in state.tasks}
        lines: list[str] = []
        for dependency_id in task.dependencies:
            dependency = by_id.get(dependency_id)
            if dependency is None:
                continue
            lines.append(f"- {dependency.title} [{dependency.status}]")
            if dependency.output_summary:
                lines.append(f"  Summary: {dependency.output_summary}")
            if dependency.reply:
                lines.append(f"  Reply: {dependency.reply[:1000]}")
        return "\n".join(lines) or "(none)"

    def _attachment_context(self, run: RunRecord) -> str:
        if not run.attached_file_path:
            return "Attached file context:\n(none)"
        target = Path(run.attached_file_path).expanduser()
        if not target.exists():
            return f"Attached file context:\nMissing path: {target}"
        try:
            content = self.artifacts.extract_text_for_prompt(target, max_chars=6000)
        except Exception as exc:
            return f"Attached file context:\nUnable to read {target}: {exc}"
        return f"Attached file context:\nPath: {target.resolve()}\n\n```text\n{content}\n```"

    def _swarm_settled(self, state: Any) -> bool:
        task_counts = (state.metrics or {}).get("taskCounts", {})
        return task_counts.get("pending", 0) == 0 and task_counts.get("working", 0) == 0

    def _swarm_deadlocked(self, state: Any) -> bool:
        task_counts = (state.metrics or {}).get("taskCounts", {})
        if task_counts.get("working", 0) > 0:
            return False
        if task_counts.get("pending", 0) == 0:
            return False
        by_id = {task.id: task for task in state.tasks}
        for task in state.tasks:
            if task.status != "pending":
                continue
            if all(by_id.get(dep) and by_id[dep].status == "done" for dep in task.dependencies):
                return False
        return True

    async def _compose_final_reply(self, run_id: str, state: Any) -> str:
        completed = [task for task in state.tasks if task.status == "done"]
        failed = [task for task in state.tasks if task.status == "error"]
        artifacts = self.store.get_run(run_id).artifacts
        lines = [
            "## Swarm Summary",
            state.manager_summary or "Dynamic swarm execution finished.",
            "",
            f"- Completed tasks: {len(completed)}",
            f"- Failed tasks: {len(failed)}",
            f"- Worker pool: {len(state.workers)}",
            f"- Generated artifacts: {len(artifacts)}",
        ]
        if completed:
            lines.append("")
            lines.append("### Completed")
            for task in completed[:12]:
                lines.append(f"- {task.title}: {task.output_summary or 'done'}")
        if failed:
            lines.append("")
            lines.append("### Failed")
            for task in failed[:12]:
                lines.append(f"- {task.title}: {task.error or 'failed'}")
        if artifacts:
            lines.append("")
            lines.append("### Files")
            for artifact in artifacts[:20]:
                lines.append(f"- `{artifact.file_name}` by {artifact.generated_by_agent}")
        return "\n".join(lines).strip()

    def _fallback_route(self, run: RunRecord) -> RouterDecision:
        text = run.goal.strip()
        normalized = text.lower()
        if self._is_greeting(text):
            return RouterDecision(
                route="chat",
                reply="كيف أقدر أخدمك؟" if detect_language_from_text(text) == "ar" else "How can I help?",
                reason="Greeting or casual conversational input.",
            )

        action_signals = (
            "create",
            "write",
            "build",
            "implement",
            "fix",
            "debug",
            "refactor",
            "generate",
            "save",
            "modify",
            "update",
            "delete",
            "install",
            "run ",
            "execute",
            "terminal",
            "shell",
            "powershell",
            "cmd",
            "script",
            "component",
            "api",
            "backend",
            "frontend",
            "workspace",
            "project",
            "file",
            "path",
            "repo",
            "compile",
            "test",
            "deploy",
            "أنشئ",
            "انشئ",
            "اكتب",
            "نفذ",
            "شغل",
            "عدل",
            "حدّث",
            "حدث",
            "صلح",
            "ملف",
            "مجلد",
            "مشروع",
        )
        if run.attached_file_path and any(term in normalized for term in ("edit", "modify", "convert", "transform", "extract", "عدل", "حوّل", "استخرج")):
            return RouterDecision(route="swarm", reason="Attached file requires actionable transformation.")
        if self._infer_requested_file(text) or any(signal in normalized for signal in action_signals) or re.search(r"[A-Za-z]:\\|/api/|\.py\b|\.vue\b|\.json\b|\.md\b|\.pdf\b", text):
            return RouterDecision(route="swarm", reason="Actionable engineering or file workflow detected.")
        return RouterDecision(route="chat", reason="Informational or conversational prompt detected.")

    def _is_greeting(self, text: str) -> bool:
        normalized = text.lower().strip()
        greetings = {
            "hi",
            "hello",
            "hey",
            "thanks",
            "thank you",
            "how are you",
            "مرحبا",
            "السلام عليكم",
            "كيف حالك",
        }
        return normalized in greetings

    def _infer_requested_file(self, text: str) -> str:
        patterns = [
            r"(?:named|called|file named|file called)\s+([A-Za-z0-9_\-\.]+\.[A-Za-z0-9]+)",
            r"(?:اسمه|اسمها|باسم)\s+([A-Za-z0-9_\-\.]+\.[A-Za-z0-9]+)",
        ]
        for pattern in patterns:
            match = re.search(pattern, text, flags=re.IGNORECASE)
            if match:
                return match.group(1).strip()
        generic = re.search(r"([A-Za-z0-9_\-]+\.(?:txt|md|py|json|html|csv|pdf|docx|vue|css|js|ts))", text, flags=re.IGNORECASE)
        return generic.group(1).strip() if generic else ""

    def _infer_literal_content(self, text: str) -> str:
        patterns = [
            r"containing exactly:\s*(.+?)(?:\.\s*verify|\s+verify|$)",
            r"write(?: it)?\s+(?:with|containing)\s+(.+?)(?:\.\s*verify|\s+verify|$)",
            r"اكتب(?: فيه)?\s+(.+?)(?:\.\s*تحقق|\s+تحقق|$)",
        ]
        for pattern in patterns:
            match = re.search(pattern, text, flags=re.IGNORECASE | re.DOTALL)
            if match:
                return match.group(1).strip().strip("\"'`")
        return ""

    def _coerce_plan_for_task(self, run: RunRecord, task: SwarmTask, plan: SwarmWorkerPlan) -> SwarmWorkerPlan:
        action_kinds = {action.kind for action in plan.actions}
        inferred_file = self._infer_requested_file(run.goal) or self._infer_requested_file(task.prompt)
        if task.task_type == "coding" and inferred_file:
            has_material_action = bool(action_kinds & {"write_file", "append_file", "run_command", "spawn_task"})
            if not has_material_action:
                return self._fallback_worker_plan(run, task)
        if task.task_type == "verification" and inferred_file:
            has_verification_action = bool(action_kinds & {"verify_file", "run_command"})
            if not has_verification_action:
                return self._fallback_worker_plan(run, task)
        return plan

    def _worker_agent_key(self, worker: SwarmWorkerState) -> str:
        return worker.name.lower().replace(" ", "_")

    async def _mark_run_cancelled(self, run_id: str, message: str) -> None:
        run = self.store.get_run(run_id)
        run.status = "cancelled"
        run.error_message = message
        await self.store.save_run(run)
        if (run.execution_route or ("swarm" if run.swarm_run else "chat")) == "swarm":
            try:
                await self.registry.set_run_status(run.id, "cancelled", final_reply=message)
            except KeyError:
                pass
        await self.store.publish_pipeline_error(run.id, message)

    async def _mark_run_failed(self, run_id: str, message: str) -> None:
        run = self.store.get_run(run_id)
        run.status = "failed"
        run.error_message = message
        await self.store.save_run(run)
        if (run.execution_route or ("swarm" if run.swarm_run else "chat")) == "swarm":
            try:
                await self.registry.append_event(run.id, level="error", source="manager", message=message)
                await self.registry.set_run_status(run.id, "failed", final_reply=message)
            except KeyError:
                pass
        await self.store.publish_pipeline_error(run.id, message)

    def _now(self) -> str:
        from .helpers import utc_now_iso

        return utc_now_iso()
