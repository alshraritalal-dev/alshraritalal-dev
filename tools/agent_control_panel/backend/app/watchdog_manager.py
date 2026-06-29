from __future__ import annotations

import logging
from pathlib import Path
from typing import Callable

try:
    from watchdog.events import FileSystemEvent, FileSystemEventHandler
    from watchdog.observers import Observer
except Exception:  # pragma: no cover - optional runtime dependency
    FileSystemEvent = object  # type: ignore[assignment]
    FileSystemEventHandler = object  # type: ignore[assignment]
    Observer = None  # type: ignore[assignment]


logger = logging.getLogger(__name__)


class _SourceChangeHandler(FileSystemEventHandler):  # type: ignore[misc]
    def __init__(self, callback: Callable[[Path], None]) -> None:
        super().__init__()
        self.callback = callback

    def on_any_event(self, event: FileSystemEvent) -> None:  # pragma: no cover - depends on local FS events
        if getattr(event, "is_directory", False):
            return
        path = Path(str(getattr(event, "src_path", "")))
        if path.suffix.lower() not in {".py", ".json", ".js", ".ts", ".vue", ".css"}:
            return
        if "__pycache__" in path.parts:
            return
        self.callback(path)


class WatchdogManager:
    def __init__(self, roots: list[Path], callback: Callable[[Path], None]) -> None:
        self.roots = roots
        self.callback = callback
        self._observer = Observer() if Observer is not None else None

    def start(self) -> None:
        if self._observer is None:
            logger.warning("watchdog is not installed; source hot-reload monitoring is disabled.")
            return
        handler = _SourceChangeHandler(self.callback)
        for root in self.roots:
            if root.exists():
                self._observer.schedule(handler, str(root), recursive=True)
        self._observer.start()

    def stop(self) -> None:
        if self._observer is None:
            return
        self._observer.stop()
        self._observer.join(timeout=3)
