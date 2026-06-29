from __future__ import annotations

import asyncio
import json
import shutil
from collections import defaultdict
from pathlib import Path

from .artifacts import ArtifactService
from .computer_use import ComputerUseService
from .config import Settings
from .helpers import ensure_within_workspace, is_destructive_command, quote_windows_argument, sanitize_relative_path
from .schemas import CommandSpec, ExecutionPlan, FileOperation, RunRecord
from .state import RunStore


class ExecutionEngine:
    def __init__(
        self,
        store: RunStore,
        settings: Settings,
        artifacts: ArtifactService,
        computer_use: ComputerUseService | None = None,
    ) -> None:
        self.store = store
        self.settings = settings
        self.artifacts = artifacts
        self.computer_use = computer_use
        self._locks: dict[str, asyncio.Lock] = defaultdict(asyncio.Lock)

    async def apply_auto(self, run_id: str) -> None:
        async with self._locks[run_id]:
            run = self.store.get_run(run_id)
            plan = run.execution_plan
            if plan is None:
                return
            run.status = "applying"
            await self.store.save_run(run)
            await self._apply_pending_items(run, plan, stop_before_manual=False)
            run.status = "completed" if not self._has_pending(plan) else "awaiting_approval"
            await self.store.save_run(run)

    async def approve_next(self, run_id: str) -> None:
        async with self._locks[run_id]:
            run = self.store.get_run(run_id)
            plan = run.execution_plan
            if plan is None:
                return
            run.status = "applying"
            await self.store.save_run(run)
            await self._apply_pending_items(run, plan, stop_before_manual=False, max_items=1)
            run.status = "completed" if not self._has_pending(plan) else "awaiting_approval"
            await self.store.save_run(run)

    async def approve_all(self, run_id: str) -> None:
        async with self._locks[run_id]:
            run = self.store.get_run(run_id)
            plan = run.execution_plan
            if plan is None:
                return
            run.status = "applying"
            await self.store.save_run(run)
            await self._apply_pending_items(run, plan, stop_before_manual=False)
            run.status = "completed" if not self._has_pending(plan) else "awaiting_approval"
            await self.store.save_run(run)

    async def force_execute(self, run_id: str) -> None:
        async with self._locks[run_id]:
            run = self.store.get_run(run_id)
            if run.coder_plan is None:
                return
            run.execution_plan = ExecutionPlan(
                source="coder",
                summary=run.coder_plan.summary,
                operations=run.coder_plan.operations,
                commands=run.coder_plan.commands,
            )
            await self.store.append_log(run.id, "system", "Force Execute enabled. Applying the raw coder plan.")
            run.status = "applying"
            await self.store.save_run(run)
            await self._apply_pending_items(run, run.execution_plan, stop_before_manual=False)
            run.status = "completed" if not self._has_pending(run.execution_plan) else "awaiting_approval"
            await self.store.save_run(run)

    def _has_pending(self, plan: ExecutionPlan) -> bool:
        return any(item.status == "pending" for item in plan.operations + plan.commands)

    async def _apply_pending_items(
        self,
        run: RunRecord,
        plan: ExecutionPlan,
        *,
        stop_before_manual: bool,
        max_items: int | None = None,
    ) -> bool:
        applied_count = 0

        for operation in plan.operations:
            if self.store.get_run(run.id).cancel_requested:
                raise asyncio.CancelledError("Execution cancelled by user.")
            if operation.status != "pending":
                continue
            if stop_before_manual and operation.requires_approval:
                return True
            await self._apply_operation(run, operation)
            applied_count += 1
            if max_items is not None and applied_count >= max_items:
                return False

        for command in plan.commands:
            if self.store.get_run(run.id).cancel_requested:
                raise asyncio.CancelledError("Execution cancelled by user.")
            if command.status != "pending":
                continue
            if stop_before_manual and command.requires_approval:
                return True
            await self._apply_command(run, command)
            applied_count += 1
            if max_items is not None and applied_count >= max_items:
                return False

        return False

    async def _apply_operation(self, run: RunRecord, operation: FileOperation) -> None:
        target = self._resolve_operation_target(run, operation.path)

        try:
            if operation.kind == "make_dir":
                target.mkdir(parents=True, exist_ok=True)
                if not target.is_dir():
                    raise RuntimeError(f"Directory was not created: {target}")
                operation.status = "applied"
                await self.store.append_log(run.id, "coder", f"FILE WRITE RESULT: applied - {target}", kind="result")
                return

            if operation.kind == "delete_file":
                await self.store.append_log(run.id, "coder", f"DELETING PATH: {target}", kind="status")
                if target.is_dir():
                    shutil.rmtree(target)
                elif target.exists():
                    target.unlink()
                if target.exists():
                    raise RuntimeError(f"Path still exists after delete: {target}")
                operation.status = "applied"
                await self.store.append_log(run.id, "coder", f"FILE WRITE RESULT: applied - {target}", kind="result")
                return

            encoded_bytes = len(operation.content.encode(operation.encoding, errors="strict"))
            await self.store.append_log(
                run.id,
                "coder",
                f"WRITING FILE: {target} ({encoded_bytes} bytes)",
                kind="status",
            )

            try:
                verification = await asyncio.to_thread(
                    self.artifacts.write_content_to_path,
                    target,
                    operation.content,
                    encoding=operation.encoding,
                    append=operation.kind == "append_file",
                )
            except Exception as exc:
                await self.store.append_log(
                    run.id,
                    "coder",
                    f"Primary write failed for {target}: {exc}",
                    kind="error",
                )
                verification = await self._attempt_alternative_write(run, target, operation, exc)

            operation.status = "applied"
            operation.error = None
            await self.store.append_log(
                run.id,
                "coder",
                f"FILE WRITE RESULT: applied - {target}",
                kind="result",
            )
            await self.store.append_log(
                run.id,
                "coder",
                f"VERIFIED ON DISK: {verification['path']}",
                kind="result",
            )

            artifact = await asyncio.to_thread(
                self.artifacts.stage_file,
                run.id,
                target,
                generated_by_agent="coder",
                workspace_root=run.workspace_root,
            )
            await self.store.add_artifact(run.id, artifact)
        except Exception as exc:
            operation.status = "failed"
            operation.error = str(exc)
            run.status = "failed"
            await self.store.append_log(run.id, "coder", f"Operation failed for {operation.path}: {exc}", kind="error")
            await self.store.save_run(run)
            raise

    async def _attempt_alternative_write(
        self,
        run: RunRecord,
        target: Path,
        operation: FileOperation,
        original_error: Exception,
    ) -> dict[str, str | int | bool]:
        suffix = target.suffix.lower()
        if suffix in {".pdf", ".docx"}:
            raise original_error

        await self.store.append_log(
            run.id,
            "coder",
            f"Retrying with direct UTF-8 write: {target}",
            kind="status",
        )
        target.parent.mkdir(parents=True, exist_ok=True)
        if operation.kind == "append_file":
            with target.open("a", encoding=operation.encoding, errors="strict", newline="") as handle:
                handle.write(operation.content)
        else:
            target.write_text(operation.content, encoding=operation.encoding, errors="strict")
        return await asyncio.to_thread(self.artifacts.verify_output, target)

    async def _apply_command(self, run: RunRecord, command: CommandSpec) -> None:
        computer_action = self._parse_computer_action(command.command)
        if computer_action is not None:
            await self._apply_computer_action(run, command, computer_action)
            return

        normalized_command = self._normalize_command_text(run, command.command)
        if normalized_command != command.command:
            command.command = normalized_command
            await self.store.append_log(
                run.id,
                "coder",
                f"NORMALIZED COMMAND: {normalized_command}",
                kind="status",
            )

        command.requires_approval = command.requires_approval or is_destructive_command(command.command)
        process = await asyncio.create_subprocess_shell(
            command.command,
            cwd=run.workspace_root,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        async def _stream(reader: asyncio.StreamReader | None, kind: str) -> None:
            if reader is None:
                return
            while True:
                line = await reader.readline()
                if not line:
                    break
                await self.store.append_log(run.id, "coder", line.decode("utf-8", errors="replace").rstrip(), kind=kind)

        await asyncio.gather(_stream(process.stdout, "stdout"), _stream(process.stderr, "stderr"))
        return_code = await process.wait()
        command.return_code = return_code
        if return_code == 0:
            command.status = "applied"
            command.error = None
            await self.store.append_log(run.id, "coder", f"Command passed: {command.command}", kind="result")
        else:
            command.status = "failed"
            command.error = f"Command exited with code {return_code}"
            run.status = "failed"
            await self.store.append_log(run.id, "coder", f"Command failed: {command.command}", kind="error")
            await self.store.save_run(run)
            raise RuntimeError(command.error)

    def _normalize_command_text(self, run: RunRecord, command_text: str) -> str:
        normalized = str(command_text or "").strip()
        if not normalized:
            return normalized

        candidates: set[str] = {str(Path(run.workspace_root).expanduser().resolve())}
        if run.attached_file_path:
            candidates.add(str(Path(run.attached_file_path).expanduser().resolve()))

        plan = run.execution_plan
        if plan is not None:
            for operation in plan.operations:
                if operation.path:
                    candidates.add(operation.path)
                    op_path = Path(operation.path).expanduser()
                    if op_path.is_absolute():
                        candidates.add(str(op_path.resolve()))
                if operation.source_path:
                    candidates.add(operation.source_path)
                    source_path = Path(operation.source_path).expanduser()
                    if source_path.is_absolute():
                        candidates.add(str(source_path.resolve()))

        for candidate in sorted((item for item in candidates if item), key=len, reverse=True):
            if " " not in candidate and "\t" not in candidate:
                continue
            quoted = quote_windows_argument(candidate)
            if quoted in normalized:
                continue
            normalized = normalized.replace(candidate, quoted)
        return normalized

    def _parse_computer_action(self, command_text: str) -> dict | None:
        prefix = "COMPUTER_ACTION"
        stripped = str(command_text or "").strip()
        if not stripped.upper().startswith(prefix):
            return None
        payload = stripped[len(prefix) :].strip()
        if not payload:
            raise ValueError("COMPUTER_ACTION command is missing a JSON payload.")
        data = json.loads(payload)
        if not isinstance(data, dict) or "action" not in data:
            raise ValueError("COMPUTER_ACTION payload must be a JSON object with an 'action' field.")
        return data

    async def _apply_computer_action(self, run: RunRecord, command: CommandSpec, payload: dict) -> None:
        if self.computer_use is None:
            raise RuntimeError("Computer use service is not configured.")
        action = str(payload.get("action", "")).strip()
        arguments = {key: value for key, value in payload.items() if key != "action"}
        try:
            result = await asyncio.to_thread(self.computer_use.run_action, action, **arguments)
            command.status = "applied"
            command.error = None
            preview = json.dumps(result, ensure_ascii=False)
            if len(preview) > 1200:
                preview = preview[:1200] + "..."
            await self.store.append_log(
                run.id,
                "coder",
                f"Computer action passed: {action} -> {preview}",
                kind="result",
            )
            await self._register_command_artifacts(run, action, result)
        except Exception as exc:
            command.status = "failed"
            command.error = str(exc)
            run.status = "failed"
            await self.store.append_log(run.id, "coder", f"Computer action failed: {action}: {exc}", kind="error")
            await self.store.save_run(run)
            raise

    async def _register_command_artifacts(self, run: RunRecord, action: str, result: dict) -> None:
        potential_path = result.get("path")
        description = str(result.get("response") or "")

        if not potential_path and isinstance(result.get("screenshot"), dict):
            potential_path = result["screenshot"].get("path")

        if not potential_path:
            return

        path = Path(str(potential_path)).expanduser().resolve()
        if not path.exists():
            return

        artifact = await asyncio.to_thread(
            self.artifacts.stage_file,
            run.id,
            path,
            generated_by_agent="coder",
            workspace_root=run.workspace_root,
            description=description,
        )
        await self.store.add_artifact(run.id, artifact)

    def _resolve_operation_target(self, run: RunRecord, raw_path: str) -> Path:
        candidate = Path(raw_path).expanduser()
        if candidate.is_absolute():
            resolved = candidate.resolve()
            return self._validate_absolute_target(run, resolved)

        root = Path(run.workspace_root).resolve()
        normalized = sanitize_relative_path(raw_path)
        target = root / normalized
        return ensure_within_workspace(root, target)

    def _validate_absolute_target(self, run: RunRecord, candidate: Path) -> Path:
        workspace_root = Path(run.workspace_root).resolve()
        allowed_paths = [workspace_root]

        if run.attached_file_path:
            attached_path = Path(run.attached_file_path).expanduser().resolve()
            allowed_paths.extend([attached_path, attached_path.parent])

        for allowed in allowed_paths:
            try:
                candidate.relative_to(allowed)
                return candidate
            except ValueError:
                if candidate == allowed:
                    return candidate
                continue

        raise ValueError(f"Absolute path is outside the allowed workspace scope: {candidate}")
