from __future__ import annotations

from pathlib import Path
from collections import Counter
import re

from .helpers import build_tree_lines, read_text_utf8
from .schemas import WorkspaceSnapshot


TEXT_EXTENSIONS = {
    ".cpp",
    ".cc",
    ".cxx",
    ".c",
    ".h",
    ".hpp",
    ".hh",
    ".inl",
    ".ixx",
    ".py",
    ".md",
    ".txt",
    ".json",
    ".toml",
    ".yaml",
    ".yml",
    ".ini",
    ".cmake",
    ".hlsl",
    ".usf",
    ".glsl",
    ".js",
    ".ts",
    ".vue",
}

BLOCKED_PARTS = {
    ".git",
    ".cache",
    ".deps",
    ".venv",
    ".vs",
    ".idea",
    ".vscode",
    "__pycache__",
    "node_modules",
    "dist",
    "build",
    "out",
    "runtime",
    "coverage",
    "site-packages",
    "vendor",
    "third_party",
}

HIGH_VALUE_FILES = {
    "README.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
    "package.json",
    "pnpm-lock.yaml",
    "package-lock.json",
    "requirements.txt",
    "pyproject.toml",
    "vite.config.js",
    "tailwind.config.js",
    "tsconfig.json",
}

PRIORITY_SEGMENTS = {
    "/src/": 24,
    "/app/": 24,
    "/frontend/": 18,
    "/backend/": 18,
    "/tools/": 16,
    "/agents/": 16,
    "/components/": 14,
    "/lib/": 12,
    "/scripts/": 10,
    "/config/": 10,
    "/tests/": 10,
    "/docs/": 4,
}


def _goal_tokens(goal: str) -> list[str]:
    return [token for token in re.findall(r"[a-zA-Z0-9_]{3,}", goal.lower()) if token]


def _list_candidate_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in BLOCKED_PARTS for part in path.parts):
            continue
        if path.suffix.lower() in TEXT_EXTENSIONS or path.name in HIGH_VALUE_FILES:
            files.append(path)
    return sorted(files)


def _score_file(path: Path, goal_tokens: list[str]) -> int:
    score = 0
    lowered = path.as_posix().lower()
    if path.name in HIGH_VALUE_FILES:
        score += 30
    for segment, bonus in PRIORITY_SEGMENTS.items():
        if segment in lowered:
            score += bonus
    if lowered.count("/") <= 4:
        score += 10
    score += sum(8 for token in goal_tokens if token in lowered)
    if goal_tokens and any(token in path.stem.lower() for token in goal_tokens):
        score += 12
    return score


def detect_workspace_snapshot(root: Path, goal: str) -> WorkspaceSnapshot:
    files = _list_candidate_files(root)
    extension_counter: Counter[str] = Counter()
    for file_path in files:
        ext = file_path.suffix.lower() or file_path.name
        extension_counter[ext] += 1

    goal_tokens = _goal_tokens(goal)
    prioritized = sorted(files, key=lambda path: (-_score_file(path, goal_tokens), len(path.as_posix())))
    tree = build_tree_lines(prioritized, root, limit=160)

    excerpts: dict[str, str] = {}
    for file_path in prioritized[:10]:
        try:
            if file_path.stat().st_size > 200_000:
                continue
            rel = file_path.relative_to(root).as_posix()
            excerpts[rel] = read_text_utf8(file_path, limit=2400)
        except Exception:
            continue

    profile = {
        key.lstrip(".") if key.startswith(".") else key: value
        for key, value in extension_counter.most_common(12)
    }

    return WorkspaceSnapshot(
        root=str(root),
        language_profile=profile,
        tree=tree,
        excerpts=excerpts,
    )
