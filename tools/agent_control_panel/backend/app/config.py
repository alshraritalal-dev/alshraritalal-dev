from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os


@dataclass(frozen=True)
class Settings:
    panel_root: Path
    backend_root: Path
    app_root: Path
    frontend_root: Path
    project_root: Path
    runtime_root: Path
    session_root: Path
    report_root: Path
    artifact_root: Path
    swarm_state_path: Path
    reload_touch_path: Path
    output_path: Path
    ollama_url: str
    default_model: str
    analyst_model: str
    api_host: str
    api_port: int


def get_settings() -> Settings:
    backend_root = Path(__file__).resolve().parents[1]
    app_root = backend_root / "app"
    panel_root = backend_root.parent
    frontend_root = panel_root / "frontend"
    project_root = panel_root.parents[1]
    runtime_root = panel_root / "runtime"
    session_root = runtime_root / "sessions"
    report_root = runtime_root / "reports"
    artifact_root = runtime_root / "artifacts"
    swarm_state_path = app_root / "swarm_state.json"
    reload_touch_path = runtime_root / ".reload-trigger"
    output_path = runtime_root / "output.txt"

    runtime_root.mkdir(parents=True, exist_ok=True)
    session_root.mkdir(parents=True, exist_ok=True)
    report_root.mkdir(parents=True, exist_ok=True)
    artifact_root.mkdir(parents=True, exist_ok=True)

    return Settings(
        panel_root=panel_root,
        backend_root=backend_root,
        app_root=app_root,
        frontend_root=frontend_root,
        project_root=project_root,
        runtime_root=runtime_root,
        session_root=session_root,
        report_root=report_root,
        artifact_root=artifact_root,
        swarm_state_path=swarm_state_path,
        reload_touch_path=reload_touch_path,
        output_path=output_path,
        ollama_url=os.getenv("OLLAMA_URL", "http://127.0.0.1:11434").rstrip("/"),
        default_model=os.getenv("AGENT_PANEL_MODEL", "qwen2.5-coder:14b"),
        analyst_model=os.getenv("AGENT_PANEL_ANALYST_MODEL", "llama3:8b"),
        api_host=os.getenv("AGENT_PANEL_HOST", "127.0.0.1"),
        api_port=int(os.getenv("AGENT_PANEL_PORT", "8008")),
    )
