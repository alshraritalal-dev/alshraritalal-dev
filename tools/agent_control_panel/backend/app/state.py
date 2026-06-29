from __future__ import annotations

from collections import defaultdict
from pathlib import Path
from typing import Any
import asyncio
import json

from .helpers import summarize_title, utc_now_iso
from .schemas import AgentLogEntry, MessageRecord, RunArtifact, RunRecord, RunRequest


class EventHub:
    def __init__(self) -> None:
        self._queues: dict[str, list[asyncio.Queue[dict[str, Any]]]] = defaultdict(list)

    def subscribe(self, run_id: str) -> asyncio.Queue[dict[str, Any]]:
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self._queues[run_id].append(queue)
        return queue

    def unsubscribe(self, run_id: str, queue: asyncio.Queue[dict[str, Any]]) -> None:
        subscribers = self._queues.get(run_id, [])
        if queue in subscribers:
            subscribers.remove(queue)

    async def publish(self, run_id: str, event: dict[str, Any]) -> None:
        for queue in list(self._queues.get(run_id, [])):
            await queue.put(event)


class RunStore:
    def __init__(self, session_root: Path) -> None:
        self._session_root = session_root
        self._lock = asyncio.Lock()
        self._runs: dict[str, RunRecord] = {}
        self.events = EventHub()
        self._load_existing()

    def _load_existing(self) -> None:
        for file_path in sorted(self._session_root.glob("*.json")):
            try:
                data = json.loads(file_path.read_text(encoding="utf-8"))
                run = RunRecord.model_validate(data)
                if not run.title:
                    run.title = summarize_title(run.goal)
                self._runs[run.id] = run
            except Exception:
                continue

    async def create_run(self, request: RunRequest, workspace_root: str, model: str, detected_language: str) -> RunRecord:
        now = utc_now_iso()
        user_message = MessageRecord(role="user", content=request.goal, timestamp=now)
        run = RunRecord(
            title=summarize_title(request.goal),
            goal=request.goal,
            workspace_root=workspace_root,
            attached_file_path=request.attached_file_path,
            model=model,
            mode=request.mode,
            locale=request.locale,
            detected_language=detected_language,
            created_at=now,
            updated_at=now,
            messages=[user_message],
        )
        async with self._lock:
            self._runs[run.id] = run
            await self._persist(run)
        await self.publish_state(run)
        await self.events.publish(run.id, {"type": "message_saved", "message": user_message.model_dump(mode="json")})
        return run

    def get_run(self, run_id: str) -> RunRecord:
        run = self._runs.get(run_id)
        if run is None:
            raise KeyError(f"Unknown run id: {run_id}")
        return run

    def list_runs(self) -> list[RunRecord]:
        return sorted(self._runs.values(), key=lambda run: run.updated_at, reverse=True)

    async def save_run(self, run: RunRecord) -> None:
        run.updated_at = utc_now_iso()
        async with self._lock:
            self._runs[run.id] = run
            await self._persist(run)
        await self.publish_state(run)

    async def rename_run(self, run_id: str, title: str) -> RunRecord:
        run = self.get_run(run_id)
        run.title = title.strip() or run.title
        await self.save_run(run)
        return run

    async def delete_run(self, run_id: str) -> None:
        async with self._lock:
            run = self._runs.pop(run_id, None)
            if run is None:
                raise KeyError(f"Unknown run id: {run_id}")
            target = self._session_root / f"{run.id}.json"
            if target.exists():
                target.unlink()

    async def mark_cancel_requested(self, run_id: str) -> RunRecord:
        run = self.get_run(run_id)
        run.cancel_requested = True
        await self.save_run(run)
        await self.events.publish(run_id, {"type": "run_cancel_requested", "runId": run_id})
        return run

    async def append_log(self, run_id: str, agent: str, message: str, kind: str = "status") -> AgentLogEntry:
        run = self.get_run(run_id)
        entry = AgentLogEntry(agent=agent, message=message, kind=kind, timestamp=utc_now_iso())
        run.logs.setdefault(agent, []).append(entry)
        await self.save_run(run)
        await self.events.publish(run_id, {"type": "log", "entry": entry.model_dump(mode="json")})
        return entry

    async def add_message(
        self,
        run_id: str,
        *,
        role: str,
        content: str,
        agent: str | None = None,
        phase: int | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> MessageRecord:
        run = self.get_run(run_id)
        message = MessageRecord(
            role=role,
            agent=agent,
            phase=phase,
            content=content,
            timestamp=utc_now_iso(),
            metadata=metadata or {},
        )
        run.messages.append(message)
        await self.save_run(run)
        await self.events.publish(run_id, {"type": "message_saved", "message": message.model_dump(mode="json")})
        return message

    async def add_artifact(self, run_id: str, artifact: RunArtifact) -> RunArtifact:
        run = self.get_run(run_id)
        match_index = next(
            (
                index
                for index, existing in enumerate(run.artifacts)
                if (
                    existing.source_absolute_path
                    and artifact.source_absolute_path
                    and existing.source_absolute_path == artifact.source_absolute_path
                )
                or existing.absolute_path == artifact.absolute_path
            ),
            None,
        )
        if match_index is None:
            run.artifacts.append(artifact)
        else:
            run.artifacts[match_index] = artifact
        await self.save_run(run)
        await self.publish_artifact_created(run_id, artifact)
        return artifact

    async def publish_agent_start(self, run_id: str, agent: str, phase: int) -> None:
        await self.events.publish(run_id, {"type": "agent_start", "agent": agent, "phase": phase, "runId": run_id})

    async def publish_agent_done(
        self,
        run_id: str,
        agent: str,
        phase: int,
        full_output: str,
        duration_ms: int | None = None,
    ) -> None:
        event: dict[str, Any] = {
            "type": "agent_done",
            "agent": agent,
            "phase": phase,
            "full_output": full_output,
            "runId": run_id,
        }
        if duration_ms is not None:
            event["durationMs"] = duration_ms
        await self.events.publish(run_id, event)

    async def publish_pipeline_complete(self, run_id: str) -> None:
        await self.events.publish(run_id, {"type": "pipeline_complete", "runId": run_id})

    async def publish_pipeline_error(self, run_id: str, error: str) -> None:
        await self.events.publish(run_id, {"type": "pipeline_error", "runId": run_id, "error": error})

    async def publish_tool_executed(
        self,
        run_id: str,
        *,
        agent: str,
        agent_key: str,
        tool: str,
        result: dict[str, Any],
        log_entry: AgentLogEntry | None = None,
        phase: int | None = None,
    ) -> None:
        payload: dict[str, Any] = {
            "type": "tool_executed",
            "runId": run_id,
            "agent": agent,
            "agentKey": agent_key,
            "tool": tool,
            "result": result,
            "phase": phase,
        }
        if log_entry is not None:
            payload["log"] = log_entry.model_dump(mode="json")
        await self.events.publish(run_id, payload)

    async def publish_artifact_created(self, run_id: str, artifact: RunArtifact) -> None:
        await self.events.publish(
            run_id,
            {
                "type": "artifact_created",
                "runId": run_id,
                "artifact": artifact.model_dump(mode="json"),
            },
        )

    async def stream_token(self, run_id: str, agent: str, token: str, phase: int | None = None) -> None:
        await self.events.publish(run_id, {"type": "token", "agent": agent, "token": token, "phase": phase})
        await self.events.publish(run_id, {"type": "agent_chunk", "agent": agent, "chunk": token, "phase": phase, "runId": run_id})

    async def publish_state(self, run: RunRecord) -> None:
        await self.events.publish(run.id, {"type": "state", "run": run.model_dump(mode="json")})

    async def publish_route_selected(self, run_id: str, route: str, reason: str = "") -> None:
        await self.events.publish(run_id, {"type": "route_selected", "runId": run_id, "route": route, "reason": reason})

    async def publish_swarm_state(self, run_id: str, swarm_run: dict[str, Any] | None) -> None:
        await self.events.publish(run_id, {"type": "swarm_state", "runId": run_id, "swarm": swarm_run})

    async def _persist(self, run: RunRecord) -> None:
        target = self._session_root / f"{run.id}.json"
        target.write_text(json.dumps(run.model_dump(mode="json"), ensure_ascii=False, indent=2), encoding="utf-8")
