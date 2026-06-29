from __future__ import annotations

import ast
import asyncio
import json
import re
import time
from pathlib import Path
from typing import Any

from .artifacts import ArtifactService
from .config import Settings
from .execution import ExecutionEngine
from .helpers import (
    append_text_utf8,
    detect_language_from_text,
    extract_json_document,
    is_destructive_command,
    slugify_filename,
)
from .llm import OllamaClient
from .prompts import (
    analyst_direct_system_prompt,
    analyst_system_prompt,
    analyst_triage_system_prompt,
    architect_system_prompt,
    coder_system_prompt,
    qa_system_prompt,
)
from .schemas import (
    AnalystReport,
    AnalystTriage,
    ArchitectPlan,
    CoderPlan,
    ExecutionPlan,
    PerformanceForecast,
    OverallScores,
    QAReview,
    RunRecord,
)
from .state import RunStore
from .workspace import detect_workspace_snapshot


PHASES = {
    "architect": {"phase": 1, "display": "Architect"},
    "coder": {"phase": 2, "display": "Coder"},
    "qa": {"phase": 3, "display": "QA"},
    "analyst": {"phase": 4, "display": "Analyst"},
}

VALID_AGENTS = ("architect", "coder", "qa", "analyst")


class AgentOrchestrator:
    def __init__(
        self,
        store: RunStore,
        llm: OllamaClient,
        execution: ExecutionEngine,
        settings: Settings,
        artifacts: ArtifactService,
    ) -> None:
        self.store = store
        self.llm = llm
        self.execution = execution
        self.settings = settings
        self.artifacts = artifacts

    async def start(self, run_id: str) -> None:
        run = self.store.get_run(run_id)
        run.status = "planning"
        await self.store.save_run(run)

        try:
            await self._ensure_not_cancelled(run.id)

            triage = await self._run_analyst_triage(run.id)
            run = self.store.get_run(run.id)
            detected_language = detect_language_from_text(run.goal)
            triage.language = detected_language
            triage.user_response_markdown = self._normalized_triage_reply(triage, detected_language)
            run.detected_language = detected_language
            if triage.attached_file_path and not run.attached_file_path:
                run.attached_file_path = triage.attached_file_path
            if triage.refined_goal.strip():
                run.metadata["effective_goal"] = triage.refined_goal.strip()
            run.metadata["triage"] = triage.model_dump(mode="json")
            sequence = self._resolve_agent_sequence(triage)
            run.metadata["selected_agents"] = sequence
            await self.store.save_run(run)

            await self.store.append_log(
                run.id,
                "system",
                f"Routing decision: {triage.route} | agents={', '.join(sequence) if sequence else 'none'}",
            )

            if triage.route in {"reply_only", "computer_use_only"}:
                run.status = "completed"
                run.metadata["status_summary"] = {
                    "intake": f"[done] Analyst intake  : {triage.route}",
                    "decision": f"[done] Routing reason : {triage.reason}",
                    "agents": f"[done] Agents selected: {', '.join(sequence) if sequence else 'Analyst only'}",
                }
                await self.store.save_run(run)
                await self.store.publish_pipeline_complete(run.id)
                return

            if self._needs_workspace_context(triage, sequence):
                effective_goal = self._effective_goal(run)
                snapshot = detect_workspace_snapshot(Path(run.workspace_root), effective_goal)
                run.workspace_snapshot = snapshot
                await self.store.append_log(
                    run.id,
                    "system",
                    f"Workspace indexed: {len(snapshot.tree)} visible files in prompt snapshot.",
                )
                await self.store.save_run(run)

            if triage.route == "analyst_only":
                run.status = "completed"
                await self.store.save_run(run)
                await self._run_analyst_direct(run.id)
                await self._update_status_summary(run.id)
                await self.store.publish_pipeline_complete(run.id)
                return

            if "architect" in sequence:
                architect_plan = await self._run_architect(run.id)
                run = self.store.get_run(run.id)
                run.architect_plan = architect_plan
                await self.store.save_run(run)

            if "coder" in sequence:
                coder_plan = await self._run_coder(run.id)
                run = self.store.get_run(run.id)
                run.coder_plan = coder_plan
                await self.store.save_run(run)

            if "qa" in sequence:
                qa_review = await self._run_qa(run.id)
                run = self.store.get_run(run.id)
                run.qa_review = qa_review
                await self.store.save_run(run)

            run = self.store.get_run(run.id)
            if run.coder_plan is not None or run.qa_review is not None:
                run.execution_plan = self._select_execution_plan(
                    run.coder_plan or CoderPlan(),
                    run.qa_review or QAReview(),
                    run.mode,
                )
                await self.store.save_run(run)

            execution_requested = self._has_execution_work(run)
            final_analyst_requested = "analyst" in sequence

            if "coder" in sequence and not execution_requested:
                raise RuntimeError("Coder produced no executable file operations or commands.")

            if not execution_requested:
                run.status = "completed"
                await self.store.save_run(run)
                if final_analyst_requested:
                    await self.finalize(run.id)
                else:
                    await self._update_status_summary(run.id)
                    await self.store.publish_pipeline_complete(run.id)
                return

            if run.mode == "step":
                run.status = "awaiting_approval"
                await self.store.append_log(
                    run.id,
                    "system",
                    "Step-by-step mode is active. Review the queued changes before applying them.",
                )
                await self.store.save_run(run)
                return

            if "qa" in sequence and run.qa_review and run.qa_review.verdict == "needs_override":
                run.status = "awaiting_override"
                await self.store.append_log(
                    run.id,
                    "qa",
                    "QA requested override. You can apply the reviewed plan manually or force the raw coder plan.",
                    kind="status",
                )
                await self.store.save_run(run)
                return

            await self.execution.apply_auto(run.id)
            run = self.store.get_run(run.id)
            if run.status == "completed":
                if final_analyst_requested:
                    await self.finalize(run.id)
                else:
                    await self._update_status_summary(run.id)
                    await self.store.publish_pipeline_complete(run.id)
        except asyncio.CancelledError:
            run = self.store.get_run(run_id)
            run.status = "cancelled"
            run.error_message = "Generation stopped by user."
            await self.store.append_log(run.id, "system", "Pipeline cancelled by user.", kind="error")
            await self.store.save_run(run)
            await self.store.publish_pipeline_error(run.id, "Generation stopped by user.")
        except Exception as exc:
            run = self.store.get_run(run_id)
            run.status = "failed"
            run.error_message = str(exc)
            await self.store.append_log(run.id, "system", f"Pipeline failed: {exc}", kind="error")
            await self.store.save_run(run)
            await self.store.publish_pipeline_error(run.id, str(exc))

    async def finalize(self, run_id: str) -> AnalystReport | None:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        sequence = self._resolve_agent_sequence(triage)

        if "analyst" not in sequence:
            await self._update_status_summary(run.id)
            return None

        if run.analyst_report is not None:
            await self._update_status_summary(run.id)
            await self.store.publish_pipeline_complete(run.id)
            return run.analyst_report

        analyst_report = await self._run_analyst(run_id)
        run = self.store.get_run(run_id)
        run.analyst_report = analyst_report
        run.report_path, run.output_path = self._save_analyst_report(run, analyst_report)
        run.metadata["report_language"] = analyst_report.language
        await self.store.save_run(run)

        report_artifact = await asyncio.to_thread(
            self.artifacts.stage_file,
            run.id,
            Path(run.report_path),
            generated_by_agent="analyst",
            workspace_root=run.workspace_root,
            description="Final analyst report",
        )
        await self.store.add_artifact(run.id, report_artifact)
        await self._update_status_summary(run.id)
        await self.store.publish_pipeline_complete(run.id)
        return analyst_report

    async def _run_analyst_triage(self, run_id: str) -> AnalystTriage:
        run = self.store.get_run(run_id)
        display = "Analyst"
        phase = 0
        analyst_model = await self._resolve_analyst_model(run)
        await self._ensure_not_cancelled(run.id)
        await self.store.append_log(run.id, "analyst", "Analyst intake started.")
        await self.store.publish_agent_start(run.id, display, phase)
        started = time.perf_counter()

        raw = await self.llm.chat(
            model=analyst_model,
            messages=[
                {"role": "system", "content": analyst_triage_system_prompt()},
                {
                    "role": "user",
                    "content": (
                        f"User request:\n{run.goal}\n\n"
                        f"Attached file path:\n{run.attached_file_path or '(none)'}\n\n"
                        "Return the routing decision now."
                    ),
                },
            ],
            on_token=lambda token: self.store.stream_token(run.id, "analyst", token, phase),
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )

        duration_ms = int((time.perf_counter() - started) * 1000)
        await self.store.append_log(run.id, "analyst", raw, kind="result")
        triage = self._parse_triage(raw, run)
        detected_language = detect_language_from_text(run.goal)
        triage.language = detected_language
        triage.user_response_markdown = self._normalized_triage_reply(triage, detected_language)

        tool_appendix = await self._execute_tool_calls(run.id, "analyst", raw)
        if triage.route == "computer_use_only" and not tool_appendix:
            tool_appendix = await self._execute_tool_calls(run.id, "analyst", run.goal)

        if tool_appendix:
            triage.user_response_markdown = triage.user_response_markdown.rstrip() + "\n\n" + tool_appendix

        await self.store.add_message(
            run.id,
            role="assistant",
            agent="Analyst",
            phase=None,
            content=triage.user_response_markdown,
            metadata={
                "durationMs": duration_ms,
                "model": analyst_model,
                "kind": "triage",
                "route": triage.route,
            },
        )
        latest_run = self.store.get_run(run.id)
        latest_run.metadata.setdefault("phaseTimings", {})["analyst_intake"] = {
            "phase": phase,
            "durationMs": duration_ms,
            "model": analyst_model,
        }
        await self.store.save_run(latest_run)
        await self.store.publish_agent_done(run.id, display, phase, triage.user_response_markdown, duration_ms)
        return triage

    async def _run_architect(self, run_id: str) -> ArchitectPlan:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        effective_goal = self._effective_goal(run)
        raw = await self._run_phase(
            run=run,
            agent_key="architect",
            model=run.model,
            system_prompt=architect_system_prompt(run.locale if run.locale != "auto" else run.detected_language),
            user_prompt=(
                f"Goal:\n{effective_goal}\n\n"
                f"Triage context:\n{json.dumps(triage.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
                f"{self._attachment_context_block(run)}\n\n"
                "Produce the execution blueprint now."
            ),
            completion_to_message=lambda payload: self._architect_markdown(
                ArchitectPlan.model_validate(extract_json_document(payload))
            ),
            message_metadata={"kind": "phase_output"},
        )
        try:
            return ArchitectPlan.model_validate(extract_json_document(raw))
        except Exception:
            return ArchitectPlan()

    async def _run_coder(self, run_id: str) -> CoderPlan:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        effective_goal = self._effective_goal(run)
        raw = await self._run_phase(
            run=run,
            agent_key="coder",
            model=run.model,
            system_prompt=coder_system_prompt(run.locale if run.locale != "auto" else run.detected_language),
            user_prompt=(
                f"Goal:\n{effective_goal}\n\n"
                f"Triage context:\n{json.dumps(triage.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Architect plan:\n{json.dumps((run.architect_plan or ArchitectPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
                f"{self._attachment_context_block(run)}\n\n"
                "Generate the implementation operations and validation commands."
            ),
            completion_to_message=lambda payload: self._coder_markdown(
                CoderPlan.model_validate(extract_json_document(payload))
            ),
            message_metadata={"kind": "phase_output"},
        )
        try:
            plan = CoderPlan.model_validate(extract_json_document(raw))
        except Exception:
            repaired = await self._repair_json_output(run, "coder", raw)
            try:
                plan = CoderPlan.model_validate(extract_json_document(repaired))
            except Exception:
                plan = CoderPlan()
        plan = self._normalize_coder_plan(run, triage, plan)
        for command in plan.commands:
            command.requires_approval = command.requires_approval or is_destructive_command(command.command)
        for operation in plan.operations:
            operation.requires_approval = operation.requires_approval or operation.kind == "delete_file"
        return plan

    async def _run_qa(self, run_id: str) -> QAReview:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        effective_goal = self._effective_goal(run)
        raw = await self._run_phase(
            run=run,
            agent_key="qa",
            model=run.model,
            system_prompt=qa_system_prompt(run.locale if run.locale != "auto" else run.detected_language),
            user_prompt=(
                f"Goal:\n{effective_goal}\n\n"
                f"Triage context:\n{json.dumps(triage.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Architect plan:\n{json.dumps((run.architect_plan or ArchitectPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Coder draft:\n{json.dumps((run.coder_plan or CoderPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
                f"{self._attachment_context_block(run)}\n\n"
                "Return a constructive review. Prefer corrected operations over blocking."
            ),
            completion_to_message=lambda payload: self._qa_markdown(QAReview.model_validate(extract_json_document(payload))),
            message_metadata={"kind": "phase_output"},
        )
        try:
            review = QAReview.model_validate(extract_json_document(raw))
        except Exception:
            review = QAReview()
        for command in review.corrected_commands:
            command.requires_approval = command.requires_approval or is_destructive_command(command.command)
        for operation in review.corrected_operations:
            operation.requires_approval = operation.requires_approval or operation.kind == "delete_file"
        return review

    async def _run_analyst(self, run_id: str) -> AnalystReport:
        run = self.store.get_run(run_id)
        effective_goal = self._effective_goal(run)
        triage = self._triage_from_run(run)
        analyst_model = await self._resolve_analyst_model(run)
        raw = await self._run_phase(
            run=run,
            agent_key="analyst",
            model=analyst_model,
            system_prompt=analyst_system_prompt(),
            user_prompt=(
                f"Original user request:\n{run.goal}\n\n"
                f"Effective engineering goal:\n{effective_goal}\n\n"
                f"Detected language: {run.detected_language}\n\n"
                f"Triage context:\n{json.dumps(triage.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Architect output:\n{json.dumps((run.architect_plan or ArchitectPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Coder output:\n{json.dumps((run.coder_plan or CoderPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"QA output:\n{json.dumps((run.qa_review or QAReview()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Execution plan with statuses:\n{json.dumps((run.execution_plan or ExecutionPlan()).model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Generated artifacts:\n{json.dumps([artifact.model_dump(mode='json') for artifact in run.artifacts], ensure_ascii=False, indent=2)}\n\n"
                f"{self._attachment_context_block(run)}\n\n"
                "Produce the full phase-4 analysis report now."
            ),
            completion_to_message=lambda payload: AnalystReport.model_validate(
                extract_json_document(payload)
            ).full_report_markdown,
            message_metadata={"kind": "final_report"},
        )
        try:
            return AnalystReport.model_validate(extract_json_document(raw))
        except Exception:
            return AnalystReport(
                language=detect_language_from_text(run.goal),
                executive_summary=raw[:500],
                strongest_design_decision="",
                biggest_architectural_risk="",
                technical_debt="LOW",
                qa_effectiveness_verdict="",
                qa_thoroughness="ADEQUATE",
                performance_forecast=PerformanceForecast(
                    expected_fps_impact="LOW",
                    memory_footprint_estimate="unknown",
                    biggest_performance_bottleneck="unknown",
                    highest_roi_optimization="unknown",
                ),
                overall_scores=OverallScores(architecture=0, code_quality=0, qa_coverage=0, overall=0),
                final_verdict="",
                proud_of="",
                must_address="",
                full_report_markdown=raw,
            )

    async def _run_analyst_direct(self, run_id: str) -> str:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        analyst_model = await self._resolve_analyst_model(run)
        static_reply = self._build_static_direct_reply(run)
        if static_reply:
            phase = PHASES["analyst"]["phase"]
            display = PHASES["analyst"]["display"]
            started = time.perf_counter()
            await self.store.append_log(run.id, "analyst", "Analyst phase started.")
            await self.store.publish_agent_start(run.id, display, phase)
            await self.store.append_log(run.id, "analyst", static_reply, kind="result")
            duration_ms = int((time.perf_counter() - started) * 1000)
            await self.store.add_message(
                run.id,
                role="assistant",
                agent=display,
                phase=phase,
                content=static_reply,
                metadata={"durationMs": duration_ms, "model": "local-static-analysis", "kind": "direct_reply"},
            )
            latest_run = self.store.get_run(run_id)
            latest_run.metadata.setdefault("phaseTimings", {})["analyst"] = {
                "phase": phase,
                "durationMs": duration_ms,
                "model": "local-static-analysis",
            }
            await self.store.save_run(latest_run)
            await self.store.publish_agent_done(run.id, display, phase, static_reply, duration_ms)
            latest_run = self.store.get_run(run_id)
            latest_run.report_path, latest_run.output_path = self._save_direct_reply(latest_run, static_reply)
            latest_run.metadata["report_language"] = latest_run.detected_language
            await self.store.save_run(latest_run)

            report_artifact = await asyncio.to_thread(
                self.artifacts.stage_file,
                latest_run.id,
                Path(latest_run.report_path),
                generated_by_agent="analyst",
                workspace_root=latest_run.workspace_root,
                description="Direct analyst response",
            )
            await self.store.add_artifact(latest_run.id, report_artifact)
            return static_reply

        raw = await self._run_phase(
            run=run,
            agent_key="analyst",
            model=analyst_model,
            system_prompt=analyst_direct_system_prompt(run.detected_language),
            user_prompt=(
                f"Original user request:\n{run.goal}\n\n"
                f"Detected language: {run.detected_language}\n\n"
                f"Triage context:\n{json.dumps(triage.model_dump(mode='json'), ensure_ascii=False, indent=2)}\n\n"
                f"Workspace snapshot:\n{json.dumps((run.workspace_snapshot or {}).model_dump(mode='json') if run.workspace_snapshot else {}, ensure_ascii=False, indent=2)}\n\n"
                f"{self._attachment_context_block(run)}\n\n"
                "Answer the user directly now."
            ),
            completion_to_message=lambda payload: str(payload).strip(),
            message_metadata={"kind": "direct_reply"},
        )
        reply_markdown = str(raw).strip()
        latest_run = self.store.get_run(run_id)
        latest_run.report_path, latest_run.output_path = self._save_direct_reply(latest_run, reply_markdown)
        latest_run.metadata["report_language"] = latest_run.detected_language
        await self.store.save_run(latest_run)

        report_artifact = await asyncio.to_thread(
            self.artifacts.stage_file,
            latest_run.id,
            Path(latest_run.report_path),
            generated_by_agent="analyst",
            workspace_root=latest_run.workspace_root,
            description="Direct analyst response",
        )
        await self.store.add_artifact(latest_run.id, report_artifact)
        return reply_markdown

    def _build_static_direct_reply(self, run: RunRecord) -> str | None:
        triage = self._triage_from_run(run)
        if triage.primary_intent not in {"explain", "analyze", "review"}:
            return None
        if not run.attached_file_path:
            return None

        target = Path(run.attached_file_path).expanduser()
        if not target.exists() or target.suffix.lower() != ".py":
            return None

        try:
            source = target.read_text(encoding="utf-8", errors="replace")
            tree = ast.parse(source)
        except Exception:
            return None

        top_level_functions: list[str] = []
        classes: list[tuple[str, list[str]]] = []
        for node in tree.body:
            if isinstance(node, ast.ClassDef):
                methods = [
                    child.name
                    for child in node.body
                    if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef))
                ]
                classes.append((node.name, methods))
            elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                top_level_functions.append(node.name)

        file_name = target.name
        primary_class = classes[0][0] if classes else None
        primary_methods = classes[0][1] if classes else []
        line_count = len(source.splitlines())

        method_flow = self._summarize_method_flow(primary_methods, run.detected_language)
        if run.detected_language == "ar":
            class_block = (
                "\n".join(f"- `{name}`: {', '.join(f'`{method}`' for method in methods[:10])}" for name, methods in classes)
                or "- لا توجد كلاسات معرفة في هذا الملف."
            )
            function_block = (
                "\n".join(f"- `{name}`" for name in top_level_functions[:12]) or "- لا توجد دوال علوية مستقلة."
            )
            return (
                f"## شرح `{file_name}`\n\n"
                f"- هذا الملف يحتوي تقريباً على `{line_count}` سطراً ويعمل كطبقة تنسيق وتشغيل داخل النظام.\n"
                f"- العنصر المركزي فيه هو `{primary_class}`، وهو المسؤول عن إدارة دورة تشغيل الطلب من الاستقبال حتى الإنهاء.\n\n"
                "## ماذا يفعل عملياً\n\n"
                "- يستقبل الطلب، يمرره على مرحلة التحليل الأولى، ثم يحدد أي وكلاء يجب تشغيلهم.\n"
                "- يبني snapshot من مساحة العمل عند الحاجة حتى يمرر السياق للمعماري أو المبرمج أو المحلل.\n"
                "- يشغل مراحل `architect` و`coder` و`qa` و`analyst` حسب مسار الطلب.\n"
                "- يختار خطة التنفيذ، يطبقها عبر محرك التنفيذ، ثم يحفظ التقارير والـ artifacts.\n\n"
                "## الكلاسات والوظائف الرئيسية\n\n"
                f"{class_block}\n\n"
                "## تسلسل التدفق الأهم\n\n"
                f"{method_flow}\n\n"
                "## الدوال العلوية في الملف\n\n"
                f"{function_block}\n\n"
                "## الخلاصة\n\n"
                f"`{file_name}` هو عقل التنسيق في الباكند: يقرر المسار، يجمع المخرجات، ويضمن أن التنفيذ النهائي والتقرير يتم حفظهما بشكل منظم."
            )

        class_block = (
            "\n".join(f"- `{name}`: {', '.join(f'`{method}`' for method in methods[:10])}" for name, methods in classes)
            or "- No classes are defined in this file."
        )
        function_block = "\n".join(f"- `{name}`" for name in top_level_functions[:12]) or "- No standalone top-level functions."
        return (
            f"## `{file_name}` Overview\n\n"
            f"- This file is about `{line_count}` lines long and acts as an orchestration layer in the backend.\n"
            f"- Its central type is `{primary_class}`, which manages the run lifecycle from intake through final output.\n\n"
            "## What It Does\n\n"
            "- Accepts a run, performs analyst triage, and decides which agents should execute.\n"
            "- Builds a workspace snapshot when downstream phases need code or file context.\n"
            "- Runs the architect, coder, QA, and analyst phases depending on the selected route.\n"
            "- Builds and applies execution plans, then saves reports and artifacts.\n\n"
            "## Main Classes And Methods\n\n"
            f"{class_block}\n\n"
            "## Important Runtime Flow\n\n"
            f"{method_flow}\n\n"
            "## Top-Level Functions\n\n"
            f"{function_block}\n\n"
            "## Bottom Line\n\n"
            f"`{file_name}` is the backend coordinator. It decides the path for each request, collects phase outputs, hands execution to the execution engine, and persists the final response artifacts."
        )

    def _summarize_method_flow(self, methods: list[str], language: str) -> str:
        if not methods:
            return "- No method flow could be inferred from the file structure."

        labels_en = {
            "start": "Entry point for the run lifecycle.",
            "finalize": "Builds the final analyst output and persists the report artifact.",
            "_run_analyst_triage": "Performs intake routing and decides which agents to run.",
            "_run_architect": "Generates the architecture plan for implementation work.",
            "_run_coder": "Generates file operations and validation commands.",
            "_run_qa": "Reviews and corrects the coder output when needed.",
            "_run_analyst": "Builds the full final analyst report for multi-phase runs.",
            "_run_analyst_direct": "Handles direct analyst-only answers without the full pipeline.",
            "_run_phase": "Common phase runner for model calls, streaming, logs, and stored messages.",
            "_execute_tool_calls": "Executes tool triggers such as screenshots, file reads, and input actions.",
            "_select_execution_plan": "Chooses the final execution plan from coder and QA outputs.",
            "_normalize_coder_plan": "Normalizes output paths and formats before execution.",
            "_save_analyst_report": "Writes the final analyst markdown report to disk.",
            "_save_direct_reply": "Writes the analyst-only markdown reply to disk.",
        }
        labels_ar = {
            "start": "نقطة الدخول الرئيسية لدورة الطلب كاملة.",
            "finalize": "يبني المخرج النهائي للمحلل ويحفظ التقرير كـ artifact.",
            "_run_analyst_triage": "ينفذ تحليل الاستقبال الأولي ويقرر أي وكلاء سيعملون.",
            "_run_architect": "ينتج الخطة المعمارية للأعمال التنفيذية.",
            "_run_coder": "ينتج عمليات الملفات وأوامر التحقق.",
            "_run_qa": "يراجع مخرجات المبرمج ويصححها عند الحاجة.",
            "_run_analyst": "يبني التقرير النهائي الكامل للمحلل في المسارات الكبيرة.",
            "_run_analyst_direct": "يعالج ردود المحلل المباشرة بدون تشغيل كل المراحل.",
            "_run_phase": "مشغل عام للمراحل مع البث اللحظي واللوقات وتخزين الرسائل.",
            "_execute_tool_calls": "ينفذ أوامر الأدوات مثل اللقطات وقراءة الملفات وإدخال المستخدم.",
            "_select_execution_plan": "يختار خطة التنفيذ النهائية من مخرجات المبرمج وQA.",
            "_normalize_coder_plan": "يوحد مسارات وأنواع المخرجات قبل التنفيذ.",
            "_save_analyst_report": "يحفظ تقرير المحلل النهائي كملف Markdown.",
            "_save_direct_reply": "يحفظ رد المحلل المباشر كملف Markdown.",
        }
        labels = labels_ar if language == "ar" else labels_en
        bullets = [f"- `{name}`: {labels[name]}" for name in methods if name in labels]
        if not bullets:
            bullets = [f"- `{name}`" for name in methods[:12]]
        return "\n".join(bullets)

    def _normalized_triage_reply(self, triage: AnalystTriage, language: str) -> str:
        if language == "ar":
            mapping = {
                "reply_only": "كيف أقدر أساعدك؟",
                "analyst_only": "أحلل الطلب والسياق المرفق الآن ثم أرجع لك بالشرح النهائي مباشرة.",
                "architect_only": "سأبني لك الخطة المعمارية مباشرة بدون تنفيذ ملفات.",
                "architect_plus_coder": "سأمرر الطلب إلى المعماري والمبرمج وأرجع لك بالمخرجات الفعلية.",
                "full_pipeline": "سأشغل المسار الكامل محلياً وأرجع لك بالمخرجات الفعلية والملفات الناتجة.",
                "file_modification": "سأعدل الملف المستهدف مباشرة وأرجع لك بالنسخة النهائية الناتجة.",
                "computer_use_only": "ألتقط الشاشة وأحللها الآن.",
            }
        else:
            mapping = {
                "reply_only": "How can I help?",
                "analyst_only": "I am analyzing the provided context now and will return the final explanation directly.",
                "architect_only": "I will produce the architecture plan directly without file execution.",
                "architect_plus_coder": "I will route this through the Architect and Coder agents and return real outputs.",
                "full_pipeline": "I will run the full local pipeline and return the actual outputs and generated files.",
                "file_modification": "I will modify the target file directly and return the finished artifact.",
                "computer_use_only": "I am capturing and analyzing the screen now.",
            }
        return mapping.get(triage.route, triage.user_response_markdown)

    async def _run_phase(
        self,
        *,
        run: RunRecord,
        agent_key: str,
        model: str,
        system_prompt: str,
        user_prompt: str,
        completion_to_message,
        message_metadata: dict[str, Any] | None = None,
    ) -> str:
        phase = PHASES[agent_key]["phase"]
        display = PHASES[agent_key]["display"]
        await self._ensure_not_cancelled(run.id)
        await self.store.append_log(run.id, agent_key, f"{display} phase started.")
        await self.store.publish_agent_start(run.id, display, phase)
        started = time.perf_counter()

        raw = await self.llm.chat(
            model=model,
            messages=[
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            on_token=lambda token: self.store.stream_token(run.id, agent_key, token, phase),
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )

        duration_ms = int((time.perf_counter() - started) * 1000)
        await self.store.append_log(run.id, agent_key, raw, kind="result")
        tool_appendix = await self._execute_tool_calls(run.id, agent_key, raw)
        try:
            message_content = completion_to_message(raw)
        except Exception as parse_err:
            asyncio.ensure_future(
                self.store.append_log(
                    run.id,
                    agent_key,
                    f"Warning: JSON parse failed, using raw text. Error: {parse_err}",
                    kind="status",
                )
            )
            message_content = raw
        if tool_appendix:
            message_content = message_content.rstrip() + "\n\n" + tool_appendix
        await self.store.add_message(
            run.id,
            role="assistant",
            agent=display,
            phase=phase,
            content=message_content,
            metadata={"durationMs": duration_ms, "model": model, **(message_metadata or {})},
        )
        latest_run = self.store.get_run(run.id)
        latest_run.metadata.setdefault("phaseTimings", {})[agent_key] = {
            "phase": phase,
            "durationMs": duration_ms,
            "model": model,
        }
        await self.store.save_run(latest_run)
        await self.store.publish_agent_done(run.id, display, phase, message_content, duration_ms)
        return raw

    async def _execute_tool_calls(self, run_id: str, agent_key: str, raw_text: str) -> str:
        computer_use = self.execution.computer_use
        if computer_use is None:
            return ""

        display = PHASES.get(agent_key, {}).get("display", agent_key.title())
        phase = PHASES.get(agent_key, {}).get("phase")
        normalized_text = str(raw_text or "")
        lower_text = normalized_text.lower()
        appendix_blocks: list[str] = []

        if any(
            trigger in lower_text
            for trigger in (
                "take a screenshot",
                "screenshot",
                "capture screen",
                "read screen",
                "what is on screen",
                "what's on screen",
                "on your screen",
                "on the screen",
                "describe the screen",
            )
        ):
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            screenshot_dir = self.artifacts.run_directory(run_id) / "screenshots"
            screenshot_dir.mkdir(parents=True, exist_ok=True)
            screenshot_path = screenshot_dir / f"{agent_key}_{timestamp}.png"
            screenshot_result = await asyncio.to_thread(computer_use.take_screenshot, str(screenshot_path))
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="take_screenshot",
                result=screenshot_result,
                phase=phase,
            )
            vision_result = await asyncio.to_thread(
                computer_use.read_screen_with_vision,
                "Describe everything important on this screen in a concise technical way.",
                screenshot_result["path"],
            )
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="read_screen_with_vision",
                result=vision_result,
                phase=phase,
            )
            artifact = await asyncio.to_thread(
                self.artifacts.stage_file,
                run_id,
                Path(screenshot_result["path"]),
                generated_by_agent=agent_key,
                workspace_root=self.store.get_run(run_id).workspace_root,
                description=str(vision_result.get("response", "")),
            )
            await self.store.add_artifact(run_id, artifact)
            appendix_blocks.append(
                "### Tool Execution - Screen Capture\n"
                f"- Screenshot saved to `{screenshot_result['path']}`\n"
                f"- Vision result: {vision_result.get('response', 'No description returned.')}"
            )

        for match in re.finditer(r"READ_FILE\s*:\s*([^\r\n]+)", normalized_text, flags=re.IGNORECASE):
            target_path = self._clean_tool_argument(match.group(1))
            if not target_path:
                continue
            file_result = await asyncio.to_thread(computer_use.read_file_any_type, target_path)
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="read_file_any_type",
                result=file_result,
                phase=phase,
            )
            appendix_blocks.append(self._format_file_tool_appendix(target_path, file_result))

        for match in re.finditer(r"LIST_DIR\s*:\s*([^\r\n]+)", normalized_text, flags=re.IGNORECASE):
            target_path = self._clean_tool_argument(match.group(1))
            if not target_path:
                continue
            directory_result = await asyncio.to_thread(computer_use.list_directory, target_path)
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="list_directory",
                result=directory_result,
                phase=phase,
            )
            appendix_blocks.append(self._format_directory_tool_appendix(target_path, directory_result))

        for match in re.finditer(r"CLICK\s*:\s*(\d+)\s*,\s*(\d+)", normalized_text, flags=re.IGNORECASE):
            x = int(match.group(1))
            y = int(match.group(2))
            click_result = await asyncio.to_thread(computer_use.click, x, y)
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="click",
                result=click_result,
                phase=phase,
            )
            appendix_blocks.append(f"### Tool Execution - Click\n- Clicked at `{x}, {y}`.")

        for match in re.finditer(r"TYPE\s*:\s*([^\r\n]+)", normalized_text, flags=re.IGNORECASE):
            text = self._clean_tool_argument(match.group(1), preserve_trailing_text=True)
            if not text:
                continue
            type_result = await asyncio.to_thread(computer_use.type_text, text)
            await self._publish_tool_result(
                run_id=run_id,
                agent_key=agent_key,
                display=display,
                tool="type_text",
                result=type_result,
                phase=phase,
            )
            appendix_blocks.append(f"### Tool Execution - Type\n- Typed text: `{text}`")

        return "\n\n".join(block for block in appendix_blocks if block).strip()

    async def _publish_tool_result(
        self,
        *,
        run_id: str,
        agent_key: str,
        display: str,
        tool: str,
        result: dict[str, Any],
        phase: int | None,
    ) -> None:
        preview = json.dumps(result, ensure_ascii=False)
        if len(preview) > 1600:
            preview = preview[:1600] + "..."
        log_entry = await self.store.append_log(run_id, agent_key, f"{tool} -> {preview}", kind="result")
        await self.store.publish_tool_executed(
            run_id,
            agent=display,
            agent_key=agent_key,
            tool=tool,
            result=result,
            log_entry=log_entry,
            phase=phase,
        )

    def _clean_tool_argument(self, value: str, preserve_trailing_text: bool = False) -> str:
        cleaned = str(value or "").strip()
        if preserve_trailing_text:
            cleaned = cleaned.strip().strip("`").strip()
            cleaned = cleaned.rstrip('",]}')
            return cleaned.strip()
        cleaned = cleaned.strip().strip("`").strip().strip('"').strip("'")
        cleaned = cleaned.rstrip('",]}')
        return cleaned.strip()

    def _format_file_tool_appendix(self, target_path: str, result: dict[str, Any]) -> str:
        if result.get("mode") == "text":
            content = str(result.get("content", ""))
            if len(content) > 4000:
                content = content[:4000] + "\n... [truncated]"
            return (
                "### Tool Execution - Read File\n"
                f"- Path: `{target_path}`\n"
                f"- Mode: `{result.get('mode')}`\n\n"
                "```text\n"
                f"{content}\n"
                "```"
            )
        content = str(result.get("content", ""))
        if len(content) > 4000:
            content = content[:4000] + "... [truncated]"
        return (
            "### Tool Execution - Read File\n"
            f"- Path: `{target_path}`\n"
            f"- Mode: `{result.get('mode')}`\n"
            f"- MIME type: `{result.get('mime_type')}`\n"
            f"- Base64 preview: `{content}`"
        )

    def _format_directory_tool_appendix(self, target_path: str, result: dict[str, Any]) -> str:
        entries = result.get("entries") or []
        lines = []
        for entry in entries[:40]:
            kind = "dir" if entry.get("is_dir") else "file"
            size_part = "" if entry.get("is_dir") else f" ({entry.get('size_bytes', 0)} bytes)"
            lines.append(f"- [{kind}] {entry.get('name')}{size_part}")
        if len(entries) > 40:
            lines.append(f"- ... and {len(entries) - 40} more entries")
        listing = "\n".join(lines) if lines else "- Directory is empty."
        return "### Tool Execution - Directory Listing\n" f"- Path: `{target_path}`\n{listing}"

    async def _update_status_summary(self, run_id: str) -> None:
        run = self.store.get_run(run_id)
        triage = self._triage_from_run(run)
        plan = run.execution_plan or ExecutionPlan()
        applied_ops = sum(1 for item in plan.operations if item.status == "applied")
        failed_ops = sum(1 for item in plan.operations if item.status == "failed")
        applied_cmds = sum(1 for item in plan.commands if item.status == "applied")
        failed_cmds = sum(1 for item in plan.commands if item.status == "failed")
        run.metadata["status_summary"] = {
            "intake": f"[done] Analyst intake  : {triage.route}",
            "agents": f"[done] Agents selected: {', '.join(run.metadata.get('selected_agents', [])) or 'none'}",
            "files": f"[done] File ops        : {applied_ops} applied / {failed_ops} failed",
            "commands": f"[done] Commands        : {applied_cmds} applied / {failed_cmds} failed",
            "artifacts": f"[done] Artifacts       : {len(run.artifacts)}",
            "saved": f"[done] Report saved to : {run.report_path or '-'}",
        }
        await self.store.save_run(run)

    def _needs_workspace_context(self, triage: AnalystTriage, sequence: list[str]) -> bool:
        return triage.requires_workspace_access or any(agent in {"architect", "coder", "qa"} for agent in sequence)

    def _has_execution_work(self, run: RunRecord) -> bool:
        plan = run.execution_plan
        if plan is None:
            return False
        return bool(plan.operations or plan.commands)

    def _resolve_agent_sequence(self, triage: AnalystTriage) -> list[str]:
        requested = [agent for agent in triage.agents_to_run if agent in VALID_AGENTS]
        if not requested:
            requested = list(self._default_agents_for_route(triage.route))
        skip = {agent for agent in triage.skip_agents if agent in VALID_AGENTS}
        sequence = [agent for agent in requested if agent not in skip]
        if triage.route in {"reply_only", "computer_use_only"}:
            return []
        return sequence

    def _default_agents_for_route(self, route: str) -> tuple[str, ...]:
        defaults = {
            "analyst_only": ("analyst",),
            "architect_only": ("architect",),
            "architect_plus_coder": ("architect", "coder"),
            "full_pipeline": ("architect", "coder", "qa", "analyst"),
            "file_modification": ("architect", "coder", "qa", "analyst"),
        }
        return defaults.get(route, ("architect", "coder", "qa", "analyst"))

    def _triage_from_run(self, run: RunRecord) -> AnalystTriage:
        payload = run.metadata.get("triage") or {}
        try:
            return AnalystTriage.model_validate(payload)
        except Exception:
            return self._fallback_triage(run)

    def _parse_triage(self, raw: str, run: RunRecord) -> AnalystTriage:
        try:
            return AnalystTriage.model_validate(extract_json_document(raw))
        except Exception:
            return self._fallback_triage(run)

    def _fallback_triage(self, run: RunRecord) -> AnalystTriage:
        goal = run.goal.strip()
        lowered = goal.lower()
        language = detect_language_from_text(goal)

        if re.fullmatch(r"(hi|hello|hey|thanks|thank you|how are you|مرحبا|السلام عليكم|كيف حالك)[!. ]*", lowered):
            return AnalystTriage(
                language=language,
                route="reply_only",
                reason="Fallback triage detected casual conversation.",
                user_response_markdown="كيف أقدر أساعدك؟" if language == "ar" else "How can I help?",
            )

        return AnalystTriage(
            language=language,
            primary_intent="generate_file",
            domain="code",
            requested_output_format="none",
            attached_file_path=run.attached_file_path or "",
            requires_workspace_access=True,
            requires_computer_use=False,
            estimated_complexity="medium",
            time_budget="normal",
            agents_to_run=["architect", "coder", "qa", "analyst"],
            skip_agents=[],
            route="full_pipeline",
            reason="Fallback triage defaulted to the engineering pipeline.",
            user_response_markdown=(
                "سأمرر الطلب عبر مسار الهندسة المحلي وأرجع لك بالمخرجات الفعلية."
                if language == "ar"
                else "I will route this through the local engineering pipeline and return real outputs."
            ),
            refined_goal=goal,
        )

    def _normalize_coder_plan(self, run: RunRecord, triage: AnalystTriage, plan: CoderPlan) -> CoderPlan:
        if triage.route == "file_modification" and run.attached_file_path:
            for operation in plan.operations:
                if operation.kind in {"write_file", "append_file"}:
                    operation.path = run.attached_file_path
                    operation.source_path = run.attached_file_path
                    if not operation.reason:
                        operation.reason = "Targeted the attached file for in-place modification."
                    break

        expected_extension = self._expected_extension(triage.requested_output_format)
        if not expected_extension or not plan.operations:
            return plan

        has_expected_output = any(
            operation.path.lower().endswith(expected_extension) for operation in plan.operations if operation.kind != "delete_file"
        )
        if expected_extension in {".pdf", ".docx"} and has_expected_output:
            plan.commands = []
        if has_expected_output:
            return plan

        inferred_name = self._infer_requested_filename(run.goal, expected_extension)
        if not inferred_name and triage.attached_file_path:
            inferred_name = Path(triage.attached_file_path).name

        if not inferred_name:
            inferred_name = f"generated_output{expected_extension}"

        for operation in plan.operations:
            if operation.kind in {"write_file", "append_file"}:
                operation.path = inferred_name
                if not operation.reason:
                    operation.reason = f"Normalized output target to match requested {expected_extension} file."
                break
        if expected_extension in {".pdf", ".docx"}:
            plan.commands = []
        return plan

    def _expected_extension(self, requested_format: str) -> str:
        mapping = {
            "py": ".py",
            "pdf": ".pdf",
            "docx": ".docx",
            "md": ".md",
            "json": ".json",
            "html": ".html",
            "txt": ".txt",
            "csv": ".csv",
            "image": ".png",
            "zip": ".zip",
        }
        return mapping.get(requested_format, "")

    def _infer_requested_filename(self, goal: str, expected_extension: str) -> str:
        patterns = (
            rf"called\s+([A-Za-z0-9_\-\.]+{re.escape(expected_extension)})",
            rf"named\s+([A-Za-z0-9_\-\.]+{re.escape(expected_extension)})",
            rf"file\s+([A-Za-z0-9_\-\.]+{re.escape(expected_extension)})",
            rf"ملف\s+([A-Za-z0-9_\-\.]+{re.escape(expected_extension)})",
        )
        for pattern in patterns:
            match = re.search(pattern, goal, flags=re.IGNORECASE)
            if match:
                return match.group(1).strip()

        generic_match = re.search(r"([A-Za-z0-9_\-\.]+\.[A-Za-z0-9]+)", goal)
        if generic_match and generic_match.group(1).lower().endswith(expected_extension):
            return generic_match.group(1).strip()
        return ""

    async def _ensure_not_cancelled(self, run_id: str) -> None:
        if self.store.get_run(run_id).cancel_requested:
            raise asyncio.CancelledError("Generation stopped by user.")

    async def _repair_json_output(self, run: RunRecord, agent_key: str, raw: str) -> str:
        await self.store.append_log(
            run.id,
            agent_key,
            "Attempting JSON repair for invalid model output.",
            kind="status",
        )
        repair_prompt = (
            "You are repairing an invalid JSON response from an agent. "
            "Return valid JSON only. Preserve the intended operations, commands, and notes. "
            "Escape embedded source code so the JSON parses cleanly."
        )
        return await self.llm.chat(
            model=run.model,
            messages=[
                {"role": "system", "content": repair_prompt},
                {"role": "user", "content": raw},
            ],
            should_cancel=lambda: self.store.get_run(run.id).cancel_requested,
        )

    def _effective_goal(self, run: RunRecord) -> str:
        return str(run.metadata.get("effective_goal") or run.goal)

    async def _resolve_analyst_model(self, run: RunRecord) -> str:
        preferred = self.settings.analyst_model.strip()
        fallback = run.model.strip()
        try:
            health = await self.llm.health()
            models = health.get("models", [])
            available = {
                str(item.get("name", "")).strip()
                for item in models
                if item.get("name")
            } | {
                str(item.get("model", "")).strip()
                for item in models
                if item.get("model")
            }
        except Exception:
            available = set()

        if preferred and (not available or preferred in available):
            return preferred

        if preferred and fallback and preferred != fallback:
            await self.store.append_log(
                run.id,
                "system",
                f"Analyst model '{preferred}' is not available. Falling back to '{fallback}'.",
            )
        return fallback or preferred

    def _select_execution_plan(self, coder_plan: CoderPlan, qa_review: QAReview, mode: str) -> ExecutionPlan:
        if qa_review.corrected_operations or qa_review.corrected_commands:
            plan = ExecutionPlan(
                source="qa",
                summary=qa_review.summary or coder_plan.summary,
                operations=qa_review.corrected_operations or coder_plan.operations,
                commands=qa_review.corrected_commands or coder_plan.commands,
            )
        else:
            plan = ExecutionPlan(
                source="coder",
                summary=coder_plan.summary,
                operations=coder_plan.operations,
                commands=coder_plan.commands,
            )

        if mode == "step":
            for item in plan.operations:
                item.requires_approval = True
            for item in plan.commands:
                item.requires_approval = True

        return plan

    def _save_analyst_report(self, run: RunRecord, report: AnalystReport) -> tuple[str, str]:
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        slug = slugify_filename(run.title or run.goal)
        report_path = self.settings.report_root / f"{timestamp}_{run.id}_analyst_report.md"
        output_path = self.settings.output_path

        report_path.write_text(report.full_report_markdown.strip() + "\n", encoding="utf-8")
        append_text_utf8(
            output_path,
            (
                f"\n\n# Session {run.id} - Analyst Report\n"
                f"Title: {run.title}\n"
                f"Language: {report.language}\n"
                f"Saved: {report_path}\n\n"
                f"{report.full_report_markdown.strip()}\n"
            ),
        )
        run.metadata["reportFileName"] = f"report_{slug}_{timestamp}.md"
        return str(report_path), str(output_path)

    def _save_direct_reply(self, run: RunRecord, markdown_text: str) -> tuple[str, str]:
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        slug = slugify_filename(run.title or run.goal)
        report_path = self.settings.report_root / f"{timestamp}_{run.id}_analyst_reply.md"
        output_path = self.settings.output_path

        report_path.write_text(markdown_text.strip() + "\n", encoding="utf-8")
        append_text_utf8(
            output_path,
            (
                f"\n\n# Session {run.id} - Analyst Direct Reply\n"
                f"Title: {run.title}\n"
                f"Language: {run.detected_language}\n"
                f"Saved: {report_path}\n\n"
                f"{markdown_text.strip()}\n"
            ),
        )
        run.metadata["reportFileName"] = f"reply_{slug}_{timestamp}.md"
        return str(report_path), str(output_path)

    def _attachment_context_block(self, run: RunRecord) -> str:
        if not run.attached_file_path:
            return "Attached file context:\n(none)"

        target = Path(run.attached_file_path).expanduser()
        if not target.exists():
            return f"Attached file context:\nPath provided but not found: {target}"

        try:
            content = self.artifacts.extract_text_for_prompt(target, max_chars=8000)
        except Exception as exc:
            return f"Attached file context:\nUnable to read {target}: {exc}"

        if not content.strip():
            return f"Attached file context:\nPath: {target}\nNo extractable text content was found."

        return (
            f"Attached file context:\nPath: {target.resolve()}\n\n"
            "```text\n"
            f"{content}\n"
            "```"
        )

    def _architect_markdown(self, plan: ArchitectPlan) -> str:
        steps = "\n".join(f"- {step}" for step in plan.execution_strategy) or "- No execution steps provided."
        deliverables = "\n".join(f"- {item}" for item in plan.deliverables) or "- No deliverables listed."
        files = "\n".join(f"- `{item}`" for item in plan.file_targets) or "- No file targets listed."
        commands = "\n".join(f"- `{item}`" for item in plan.commands) or "- No commands proposed."
        return (
            "## Architecture Blueprint\n\n"
            f"{plan.objective}\n\n"
            "### Execution Strategy\n"
            f"{steps}\n\n"
            "### Deliverables\n"
            f"{deliverables}\n\n"
            "### Target Files\n"
            f"{files}\n\n"
            "### Validation Commands\n"
            f"{commands}\n"
        )

    def _coder_markdown(self, plan: CoderPlan) -> str:
        operations = "\n".join(
            f"- `{item.kind}` -> `{item.path}` - {item.reason or 'No reason provided.'}" for item in plan.operations[:12]
        ) or "- No file operations generated."
        commands = "\n".join(
            f"- `{item.command}` - {item.reason or 'No reason provided.'}" for item in plan.commands
        ) or "- No commands generated."
        notes = "\n".join(f"- {item}" for item in plan.notes) or "- No implementation notes."
        return (
            "## Implementation Draft\n\n"
            f"{plan.summary}\n\n"
            "### Planned File Operations\n"
            f"{operations}\n\n"
            "### Validation Commands\n"
            f"{commands}\n\n"
            "### Notes\n"
            f"{notes}\n"
        )

    def _qa_markdown(self, review: QAReview) -> str:
        fixes = "\n".join(f"- {item}" for item in review.fixes) or "- No fixes listed."
        corrected_files = "\n".join(
            f"- `{item.kind}` -> `{item.path}` - {item.reason or 'Corrective operation'}"
            for item in review.corrected_operations[:12]
        ) or "- No corrected file operations."
        corrected_commands = "\n".join(
            f"- `{item.command}` - {item.reason or 'Corrective command'}" for item in review.corrected_commands
        ) or "- No corrected commands."
        return (
            "## QA Review\n\n"
            f"**Verdict:** `{review.verdict}`\n\n"
            f"{review.summary}\n\n"
            "### Fixes\n"
            f"{fixes}\n\n"
            "### Corrected Operations\n"
            f"{corrected_files}\n\n"
            "### Corrected Commands\n"
            f"{corrected_commands}\n\n"
            "### Rationale\n"
            f"{review.rationale or 'No rationale provided.'}\n"
        )
