from __future__ import annotations

import base64
import logging
import mimetypes
import os
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Callable

import httpx
import mss
import pyautogui
from PIL import Image


logger = logging.getLogger(__name__)

pyautogui.FAILSAFE = False
pyautogui.PAUSE = 0.1

PERMISSIONS_MANIFEST = {
    "screen_capture": True,
    "screen_reading": True,
    "mouse_control": True,
    "keyboard_input": True,
    "file_read": True,
    "file_write": True,
    "directory_listing": True,
    "shell_execution": True,
    "file_verification": True,
    "vision_model": "llava:7b",
}

TEXT_SUFFIXES = {
    ".txt",
    ".md",
    ".json",
    ".yaml",
    ".yml",
    ".toml",
    ".ini",
    ".cfg",
    ".py",
    ".js",
    ".ts",
    ".tsx",
    ".jsx",
    ".vue",
    ".css",
    ".html",
    ".xml",
    ".csv",
    ".log",
    ".sh",
    ".ps1",
    ".bat",
    ".cmd",
    ".cpp",
    ".c",
    ".h",
    ".hpp",
}

OutputCallback = Callable[[dict[str, Any]], None]


class ComputerUseService:
    def __init__(self, ollama_url: str, vision_model: str = "llava:7b") -> None:
        self.ollama_url = ollama_url.rstrip("/")
        self.vision_model = vision_model

    def available_actions(self) -> list[str]:
        return [
            "take_screenshot",
            "read_screen_with_vision",
            "click",
            "type_text",
            "press_key",
            "move_mouse",
            "scroll",
            "read_file_any_type",
            "write_file",
            "verify_file",
            "write_verify_commit",
            "list_directory",
            "get_screen_size",
            "run_shell_command",
        ]

    def describe_capabilities(self) -> dict[str, Any]:
        return {
            "manifest": PERMISSIONS_MANIFEST,
            "actions": self.available_actions(),
        }

    def run_action(self, action: str, **kwargs: Any) -> dict[str, Any]:
        normalized = self._normalize_action(action)
        handlers: dict[str, Callable[..., dict[str, Any]]] = {
            "take_screenshot": self.take_screenshot,
            "read_screen_with_vision": self.read_screen_with_vision,
            "click": self.click,
            "type_text": self.type_text,
            "press_key": self.press_key,
            "move_mouse": self.move_mouse,
            "scroll": self.scroll,
            "read_file_any_type": self.read_file_any_type,
            "write_file": self.write_file,
            "verify_file": self.verify_file,
            "write_verify_commit": self.write_verify_commit,
            "list_directory": self.list_directory,
            "get_screen_size": self.get_screen_size,
            "run_shell_command": self.run_shell_command,
        }
        if normalized not in handlers:
            raise ValueError(f"Unsupported computer action: {action}")
        return self._invoke_with_retry(normalized, lambda: handlers[normalized](**kwargs))

    def take_screenshot(self, output_path: str | None = None) -> dict[str, Any]:
        target = Path(output_path).expanduser() if output_path else Path.home() / "agent_control_panel_capture.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        with mss.mss() as sct:
            saved = sct.shot(output=str(target))
        with Image.open(saved) as image:
            width, height = image.size
        return {
            "ok": True,
            "action": "take_screenshot",
            "path": str(Path(saved).resolve()),
            "width": width,
            "height": height,
        }

    def read_screen_with_vision(
        self,
        prompt: str = "What do you see?",
        screenshot_path: str | None = None,
        model: str | None = None,
    ) -> dict[str, Any]:
        image_path = Path(screenshot_path).expanduser().resolve() if screenshot_path else None
        if image_path is not None and image_path.exists():
            with Image.open(image_path) as image:
                width, height = image.size
            screenshot = {
                "ok": True,
                "action": "take_screenshot",
                "path": str(image_path),
                "width": width,
                "height": height,
            }
        else:
            screenshot = self.take_screenshot(screenshot_path)
            image_path = Path(screenshot["path"])
        image_b64 = base64.b64encode(image_path.read_bytes()).decode("ascii")
        payload = {
            "model": model or self.vision_model,
            "prompt": prompt,
            "images": [image_b64],
            "stream": False,
        }
        with httpx.Client(timeout=httpx.Timeout(180.0, connect=10.0)) as client:
            response = client.post(f"{self.ollama_url}/api/generate", json=payload)
            response.raise_for_status()
            data = response.json()
        return {
            "ok": True,
            "action": "read_screen_with_vision",
            "model": payload["model"],
            "prompt": prompt,
            "screenshot": screenshot,
            "response": str(data.get("response", "")).strip(),
        }

    def click(self, x: int, y: int, button: str = "left") -> dict[str, Any]:
        pyautogui.click(x=int(x), y=int(y), button=button)
        return {"ok": True, "action": "click", "x": int(x), "y": int(y), "button": button}

    def type_text(self, text: str, interval: float = 0.0) -> dict[str, Any]:
        pyautogui.write(text, interval=float(interval))
        return {"ok": True, "action": "type_text", "text": text, "length": len(text)}

    def press_key(self, key: str) -> dict[str, Any]:
        pyautogui.press(key)
        return {"ok": True, "action": "press_key", "key": key}

    def move_mouse(self, x: int, y: int, duration: float = 0.0) -> dict[str, Any]:
        pyautogui.moveTo(int(x), int(y), duration=float(duration))
        return {"ok": True, "action": "move_mouse", "x": int(x), "y": int(y), "duration": float(duration)}

    def scroll(self, amount: int) -> dict[str, Any]:
        pyautogui.scroll(int(amount))
        return {"ok": True, "action": "scroll", "amount": int(amount)}

    def read_file_any_type(self, path: str, max_bytes: int = 250_000) -> dict[str, Any]:
        target = Path(path).expanduser().resolve()
        if not target.exists():
            raise FileNotFoundError(f"File not found: {target}")
        data = target.read_bytes()
        mime_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        truncated = len(data) > max_bytes
        payload = data[:max_bytes]
        if self._is_text_file(target, mime_type):
            return {
                "ok": True,
                "action": "read_file_any_type",
                "path": str(target),
                "mime_type": mime_type,
                "mode": "text",
                "content": payload.decode("utf-8", errors="replace"),
                "truncated": truncated,
                "size_bytes": len(data),
            }
        return {
            "ok": True,
            "action": "read_file_any_type",
            "path": str(target),
            "mime_type": mime_type,
            "mode": "base64",
            "content": base64.b64encode(payload).decode("ascii"),
            "truncated": truncated,
            "size_bytes": len(data),
        }

    def write_file(self, path: str, content: str, encoding: str = "utf-8", append: bool = False) -> dict[str, Any]:
        target = Path(path).expanduser().resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        if append:
            with target.open("a", encoding=encoding, errors="strict", newline="") as handle:
                handle.write(content)
        else:
            target.write_text(content, encoding=encoding, errors="strict")
        return {
            "ok": True,
            "action": "write_file",
            "path": str(target),
            "encoding": encoding,
            "append": append,
            "bytes_written": len(content.encode(encoding, errors="strict")),
        }

    def verify_file(
        self,
        path: str,
        *,
        expected_substring: str | None = None,
        min_size_bytes: int = 1,
    ) -> dict[str, Any]:
        target = Path(path).expanduser().resolve()
        if not target.exists():
            raise FileNotFoundError(f"Verification failed because the file does not exist: {target}")
        size = target.stat().st_size
        if size < int(min_size_bytes):
            raise RuntimeError(f"Verification failed because the file is too small: {target} ({size} bytes)")
        mime_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        content_preview = ""
        if expected_substring or self._is_text_file(target, mime_type):
            content_preview = target.read_text(encoding="utf-8", errors="replace")
        if expected_substring and expected_substring not in content_preview:
            raise RuntimeError(f"Verification failed because the expected substring was not found in: {target}")
        return {
            "ok": True,
            "action": "verify_file",
            "path": str(target),
            "size_bytes": size,
            "mime_type": mime_type,
            "preview": content_preview[:1200],
        }

    def write_verify_commit(
        self,
        path: str,
        content: str,
        *,
        encoding: str = "utf-8",
        append: bool = False,
        expected_substring: str | None = None,
    ) -> dict[str, Any]:
        write_result = self.write_file(path, content, encoding=encoding, append=append)
        verify_result = self.verify_file(
            write_result["path"],
            expected_substring=expected_substring,
            min_size_bytes=1,
        )
        return {
            "ok": True,
            "action": "write_verify_commit",
            "write": write_result,
            "verify": verify_result,
            "path": write_result["path"],
        }

    def list_directory(self, path: str) -> dict[str, Any]:
        target = Path(path).expanduser().resolve()
        if not target.exists():
            raise FileNotFoundError(f"Directory not found: {target}")
        if not target.is_dir():
            raise NotADirectoryError(f"Not a directory: {target}")
        entries = []
        for child in sorted(target.iterdir(), key=lambda item: (not item.is_dir(), item.name.lower())):
            entries.append(
                {
                    "name": child.name,
                    "path": str(child),
                    "is_dir": child.is_dir(),
                    "size_bytes": None if child.is_dir() else child.stat().st_size,
                }
            )
        return {"ok": True, "action": "list_directory", "path": str(target), "entries": entries}

    def get_screen_size(self) -> dict[str, Any]:
        width, height = pyautogui.size()
        return {"ok": True, "action": "get_screen_size", "width": int(width), "height": int(height)}

    def run_shell_command(
        self,
        command: str,
        *,
        cwd: str | None = None,
        timeout_seconds: int = 900,
        shell: str = "powershell",
        on_output: OutputCallback | None = None,
    ) -> dict[str, Any]:
        working_directory = Path(cwd).expanduser().resolve() if cwd else Path.cwd().resolve()
        if not working_directory.exists():
            raise FileNotFoundError(f"Shell working directory does not exist: {working_directory}")

        invocation = self._build_shell_invocation(command, shell)
        started = time.perf_counter()
        stdout_lines: list[str] = []
        stderr_lines: list[str] = []

        process = subprocess.Popen(
            invocation,
            cwd=str(working_directory),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

        def pump(stream: Any, channel: str, sink: list[str]) -> None:
            if stream is None:
                return
            for line in iter(stream.readline, ""):
                cleaned = line.rstrip()
                sink.append(cleaned)
                if on_output is not None:
                    on_output({"channel": channel, "line": cleaned})
            stream.close()

        stdout_thread = threading.Thread(target=pump, args=(process.stdout, "stdout", stdout_lines), daemon=True)
        stderr_thread = threading.Thread(target=pump, args=(process.stderr, "stderr", stderr_lines), daemon=True)
        stdout_thread.start()
        stderr_thread.start()

        try:
            return_code = process.wait(timeout=max(1, int(timeout_seconds)))
        except subprocess.TimeoutExpired as exc:
            process.kill()
            raise RuntimeError(f"Shell command timed out after {timeout_seconds} seconds: {command}") from exc

        stdout_thread.join(timeout=1.0)
        stderr_thread.join(timeout=1.0)
        elapsed_ms = int((time.perf_counter() - started) * 1000)
        result = {
            "ok": return_code == 0,
            "action": "run_shell_command",
            "command": command,
            "shell": shell,
            "cwd": str(working_directory),
            "return_code": return_code,
            "stdout": "\n".join(stdout_lines).strip(),
            "stderr": "\n".join(stderr_lines).strip(),
            "elapsed_ms": elapsed_ms,
        }
        if on_output is not None:
            on_output({"channel": "status", "line": f"return_code={return_code}"})
        return result

    def _invoke_with_retry(self, action: str, callback: Callable[[], dict[str, Any]]) -> dict[str, Any]:
        last_error: Exception | None = None
        for attempt in range(1, 3):
            try:
                return callback()
            except Exception as exc:  # pragma: no cover - retry path
                last_error = exc
                logger.exception("Computer action '%s' failed on attempt %s: %s", action, attempt, exc)
        raise RuntimeError(f"Computer action '{action}' failed after 2 attempts: {last_error}") from last_error

    def _normalize_action(self, action: str) -> str:
        normalized = str(action or "").strip().lower().replace("-", "_")
        aliases = {
            "screenshot": "take_screenshot",
            "read_screen": "read_screen_with_vision",
            "vision": "read_screen_with_vision",
            "type": "type_text",
            "keypress": "press_key",
            "mouse_move": "move_mouse",
            "read_file": "read_file_any_type",
            "list_dir": "list_directory",
            "screen_size": "get_screen_size",
            "shell": "run_shell_command",
            "command": "run_shell_command",
        }
        return aliases.get(normalized, normalized)

    def _build_shell_invocation(self, command: str, shell: str) -> list[str]:
        normalized = str(shell or "powershell").strip().lower()
        if normalized in {"powershell", "pwsh", "ps"}:
            return ["powershell", "-NoLogo", "-NoProfile", "-Command", command]
        if normalized == "cmd":
            return ["cmd.exe", "/d", "/s", "/c", command]
        return [normalized, command]

    def _is_text_file(self, path: Path, mime_type: str) -> bool:
        return mime_type.startswith("text/") or path.suffix.lower() in TEXT_SUFFIXES
