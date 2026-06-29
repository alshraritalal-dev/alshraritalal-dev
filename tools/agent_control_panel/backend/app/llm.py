from __future__ import annotations

import asyncio
import json
from typing import Awaitable, Callable

import httpx


TokenCallback = Callable[[str], Awaitable[None]]
CancelCallback = Callable[[], bool]


class OllamaClient:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")
        self._client = httpx.AsyncClient(timeout=httpx.Timeout(180.0, connect=10.0))

    async def close(self) -> None:
        await self._client.aclose()

    async def health(self) -> dict:
        response = await self._client.get(f"{self.base_url}/api/tags")
        response.raise_for_status()
        return response.json()

    async def chat(
        self,
        *,
        model: str,
        messages: list[dict[str, str]],
        on_token: TokenCallback | None = None,
        should_cancel: CancelCallback | None = None,
    ) -> str:
        try:
            return await self._chat_api_chat(
                model=model,
                messages=messages,
                on_token=on_token,
                should_cancel=should_cancel,
            )
        except httpx.HTTPStatusError as exc:
            if exc.response.status_code != 404:
                raise
        return await self._chat_api_generate(
            model=model,
            messages=messages,
            on_token=on_token,
            should_cancel=should_cancel,
        )

    async def _chat_api_chat(
        self,
        *,
        model: str,
        messages: list[dict[str, str]],
        on_token: TokenCallback | None,
        should_cancel: CancelCallback | None,
    ) -> str:
        payload = {
            "model": model,
            "messages": messages,
            "stream": True,
            "options": {
                "temperature": 0.15,
                "top_p": 0.95,
            },
        }

        output_chunks: list[str] = []
        async with self._client.stream("POST", f"{self.base_url}/api/chat", json=payload) as response:
            response.raise_for_status()
            async for line in response.aiter_lines():
                if should_cancel is not None and should_cancel():
                    raise asyncio.CancelledError("Run cancelled by user.")
                if not line:
                    continue
                packet = json.loads(line)
                if packet.get("error"):
                    raise RuntimeError(packet["error"])
                message = packet.get("message") or {}
                token = message.get("content", "")
                if token:
                    output_chunks.append(token)
                    if on_token is not None:
                        await on_token(token)
                if packet.get("done"):
                    break
        return "".join(output_chunks)

    async def _chat_api_generate(
        self,
        *,
        model: str,
        messages: list[dict[str, str]],
        on_token: TokenCallback | None,
        should_cancel: CancelCallback | None,
    ) -> str:
        payload = {
            "model": model,
            "prompt": self._messages_to_prompt(messages),
            "stream": True,
            "options": {
                "temperature": 0.15,
                "top_p": 0.95,
            },
        }

        output_chunks: list[str] = []
        async with self._client.stream("POST", f"{self.base_url}/api/generate", json=payload) as response:
            response.raise_for_status()
            async for line in response.aiter_lines():
                if should_cancel is not None and should_cancel():
                    raise asyncio.CancelledError("Run cancelled by user.")
                if not line:
                    continue
                packet = json.loads(line)
                if packet.get("error"):
                    raise RuntimeError(packet["error"])
                token = packet.get("response", "")
                if token:
                    output_chunks.append(token)
                    if on_token is not None:
                        await on_token(token)
                if packet.get("done"):
                    break
        return "".join(output_chunks)

    def _messages_to_prompt(self, messages: list[dict[str, str]]) -> str:
        prompt_parts: list[str] = []
        for message in messages:
            role = str(message.get("role", "user")).upper()
            content = str(message.get("content", ""))
            prompt_parts.append(f"{role}:\n{content}")
        prompt_parts.append("ASSISTANT:")
        return "\n\n".join(prompt_parts)
