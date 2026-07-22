"""LLM client — Anthropic or OpenAI — JSON-only MIDI generation."""
from __future__ import annotations

import json
import os
import time
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Optional

import httpx

from app.prompt_loader import load_brain_prompt


@dataclass
class LlmResult:
    ok: bool
    text: str = ""
    error: str = ""
    latency_ms: int = 0
    raw: str = ""
    model: str = ""
    tokens: int = 0


class LlmClient:
    def __init__(
        self,
        provider: str | None = None,
        api_key: str | None = None,
        model: str | None = None,
    ):
        config = _load_config()
        llm_config = config.get("llm", {})
        self.provider = (
            provider
            or os.environ.get("MIDI_AGENT_PROVIDER")
            or llm_config.get("provider", "anthropic")
        ).lower()
        self.models = {
            "small": os.environ.get(
                "MIDI_AGENT_SMALL_MODEL",
                llm_config.get("small_model", "claude-haiku-4-5"),
            ),
            "mid": os.environ.get(
                "MIDI_AGENT_MID_MODEL",
                llm_config.get("mid_model", "claude-sonnet-5"),
            ),
            "strong": os.environ.get(
                "MIDI_AGENT_STRONG_MODEL",
                llm_config.get("strong_model", "claude-sonnet-5"),
            ),
        }
        self.timeout = float(llm_config.get("timeout_seconds", 90))
        if self.provider in ("openai", "gpt"):
            self.provider = "openai"
            self.api_key = api_key or os.environ.get("OPENAI_API_KEY", "")
            selected = model or os.environ.get("OPENAI_MODEL")
        else:
            self.provider = "anthropic"
            self.api_key = api_key or os.environ.get("ANTHROPIC_API_KEY", "")
            selected = model or os.environ.get("ANTHROPIC_MODEL")
        if selected:
            self.models["strong"] = selected
            self.models["mid"] = selected
        self.model = self.models["strong"]

    def has_key(self) -> bool:
        return bool(self.api_key)

    def generate_json(
        self,
        user_message: str,
        *,
        session_blob: str = "",
        taste_blob: str = "",
        fingerprint_blob: str = "",
        system_extra: str = "",
    ) -> LlmResult:
        brain = load_brain_prompt()
        system = brain
        if system_extra:
            system += "\n\n" + system_extra
        system += (
            "\n\nIMPORTANT: Respond with ONLY the JSON object from §6. "
            "No markdown fences, no prose before or after."
        )

        user = user_message
        if session_blob:
            user += f"\n\n[SESSION STATE]\n{session_blob}"
        if taste_blob:
            user += f"\n\n[TASTE PROFILE]\n{taste_blob}"
        if fingerprint_blob:
            user += f"\n\n[STYLE FINGERPRINTS]\n{fingerprint_blob}"

        return self.call_json(
            stage="legacy_generator",
            system=system,
            user=user,
            tier="strong",
        )

    def call_json(
        self,
        *,
        stage: str,
        system: str,
        user: str,
        tier: str,
    ) -> LlmResult:
        model = self.models.get(tier, self.models["strong"])
        if not self.api_key:
            return LlmResult(
                ok=False,
                error="Missing API key (ANTHROPIC_API_KEY or OPENAI_API_KEY)",
                model=model,
            )
        t0 = time.time()
        try:
            if self.provider == "openai":
                result = self._openai(system, user, model)
            else:
                result = self._anthropic(system, user, model)
            result.latency_ms = int((time.time() - t0) * 1000)
            result.model = model
            return result
        except Exception as e:
            return LlmResult(
                ok=False,
                error=f"{stage}: {e}",
                latency_ms=int((time.time() - t0) * 1000),
                model=model,
            )

    def _anthropic(self, system: str, user: str, model: str) -> LlmResult:
        body = {
            "model": model,
            "max_tokens": 8192,
            "system": system,
            "messages": [{"role": "user", "content": user}],
        }
        headers = {
            "x-api-key": self.api_key,
            "anthropic-version": "2023-06-01",
            "content-type": "application/json",
        }
        with httpx.Client(timeout=self.timeout) as client:
            r = client.post("https://api.anthropic.com/v1/messages", headers=headers, json=body)
            raw = r.text
            if r.status_code >= 400:
                return LlmResult(ok=False, error=f"Anthropic HTTP {r.status_code}: {raw[:400]}", raw=raw)
            data = r.json()
            text = ""
            for block in data.get("content", []):
                if block.get("type") == "text":
                    text += block.get("text", "")
            if not text.strip():
                return LlmResult(ok=False, error="Empty Anthropic response", raw=raw)
            usage = data.get("usage", {})
            tokens = int(usage.get("input_tokens", 0)) + int(
                usage.get("output_tokens", 0)
            )
            return LlmResult(ok=True, text=text.strip(), raw=raw, tokens=tokens)

    def _openai(self, system: str, user: str, model: str) -> LlmResult:
        body = {
            "model": model,
            "max_tokens": 8192,
            "temperature": 0.7,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "response_format": {"type": "json_object"},
        }
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "content-type": "application/json",
        }
        with httpx.Client(timeout=self.timeout) as client:
            r = client.post(
                "https://api.openai.com/v1/chat/completions",
                headers=headers,
                json=body,
            )
            raw = r.text
            if r.status_code >= 400:
                return LlmResult(ok=False, error=f"OpenAI HTTP {r.status_code}: {raw[:400]}", raw=raw)
            data = r.json()
            text = data["choices"][0]["message"]["content"]
            if not text or not text.strip():
                return LlmResult(ok=False, error="Empty OpenAI response", raw=raw)
            return LlmResult(
                ok=True,
                text=text.strip(),
                raw=raw,
                tokens=int(data.get("usage", {}).get("total_tokens", 0)),
            )


def _load_config() -> Dict[str, Any]:
    path = Path(__file__).resolve().parents[1] / "config.toml"
    if not path.exists():
        return {}
    with path.open("rb") as handle:
        return tomllib.load(handle)
