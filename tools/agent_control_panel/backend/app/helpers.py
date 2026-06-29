from __future__ import annotations

from pathlib import Path
import subprocess
from typing import Iterable
import json
import re
from datetime import datetime, timezone


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def strip_code_fences(text: str) -> str:
    cleaned = text.strip()
    if cleaned.startswith("```"):
        cleaned = re.sub(r"^```[a-zA-Z0-9_-]*\s*", "", cleaned)
        cleaned = re.sub(r"\s*```$", "", cleaned)
    return cleaned.strip()


def extract_json_document(text: str) -> dict:
    cleaned = strip_code_fences(text)
    try:
        return json.loads(cleaned)
    except json.JSONDecodeError:
        pass

    start = cleaned.find("{")
    end = cleaned.rfind("}")
    if start == -1 or end == -1 or end <= start:
        raise ValueError("No JSON object found in model output.")
    return json.loads(cleaned[start : end + 1])


def read_text_utf8(path: Path, limit: int | None = None) -> str:
    data = path.read_text(encoding="utf-8", errors="strict")
    if limit is None:
        return data
    return data[:limit]


def write_text_utf8(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", errors="strict")


def append_text_utf8(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8", errors="strict", newline="") as handle:
        handle.write(content)


def ensure_within_workspace(root: Path, candidate: Path) -> Path:
    resolved_root = root.resolve()
    resolved_candidate = candidate.resolve()
    try:
        resolved_candidate.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(f"Path escapes workspace root: {candidate}") from exc
    return resolved_candidate


def sanitize_relative_path(path_value: str) -> str:
    normalized = path_value.replace("\\", "/").strip().lstrip("/")
    normalized = re.sub(r"/{2,}", "/", normalized)
    return normalized


def quote_windows_argument(value: str) -> str:
    text = str(value or "")
    if not text:
        return '""'
    if len(text) >= 2 and text.startswith('"') and text.endswith('"'):
        return text
    return subprocess.list2cmdline([text])


def detect_language_from_text(text: str) -> str:
    arabic_chars = len(re.findall(r"[\u0600-\u06FF]", text or ""))
    latin_chars = len(re.findall(r"[A-Za-z]", text or ""))
    return "ar" if arabic_chars > latin_chars else "en"


def summarize_title(text: str, limit: int = 48) -> str:
    normalized = re.sub(r"\s+", " ", (text or "").strip())
    if not normalized:
        return "Untitled Session"
    if len(normalized) <= limit:
        return normalized
    return normalized[: max(1, limit - 3)].rstrip() + "..."


def slugify_filename(text: str, limit: int = 40) -> str:
    ascii_like = re.sub(r"[^\w\s-]", "", text or "", flags=re.UNICODE).strip().lower()
    ascii_like = re.sub(r"[-\s]+", "-", ascii_like)
    if not ascii_like:
        ascii_like = "session"
    return ascii_like[:limit].strip("-") or "session"


def is_destructive_command(command: str) -> bool:
    patterns = (
        r"\bdel\b",
        r"\brd\b",
        r"\brmdir\b",
        r"\bremove-item\b",
        r"\bmv\b",
        r"\bmove-item\b",
        r"\bren\b",
        r"\brename-item\b",
        r"\bgit\s+reset\b",
        r"\bformat\b",
        r"\breg\s+delete\b",
        r"\bsc\s+delete\b",
    )
    lowered = command.lower()
    return any(re.search(pattern, lowered) for pattern in patterns)


def build_tree_lines(paths: Iterable[Path], root: Path, limit: int = 120) -> list[str]:
    lines: list[str] = []
    for path in list(paths)[:limit]:
        try:
            rel = path.relative_to(root).as_posix()
        except ValueError:
            rel = path.name
        lines.append(rel)
    return lines
