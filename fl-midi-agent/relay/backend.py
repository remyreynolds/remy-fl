"""HTTP client for the existing brain backend."""
from __future__ import annotations

from datetime import date
from typing import Any

import httpx

from .config import RelayConfig
from .models import FeedbackRequest, GenerateRequest, NoteContract


class BackendError(RuntimeError):
    pass


def normalized_session_id(value: str | None) -> str:
    cleaned = (value or "").strip()
    return cleaned or f"fl-default-{date.today().isoformat()}"


class BrainBackendClient:
    def __init__(
        self,
        config: RelayConfig,
        transport: httpx.AsyncBaseTransport | None = None,
    ):
        self.config = config
        self.transport = transport

    def _headers(self) -> dict[str, str]:
        if not self.config.api_key:
            return {}
        return {"Authorization": f"Bearer {self.config.api_key}"}

    async def generate(self, request: GenerateRequest) -> NoteContract:
        data = await self.generate_full(request)
        contract_data = data.get("json", data)
        try:
            return NoteContract.model_validate(contract_data)
        except Exception as exc:
            raise BackendError(f"Brain backend contract invalid: {exc}") from exc

    async def generate_full(self, request: GenerateRequest) -> dict[str, Any]:
        payload = request.model_dump()
        payload["session_id"] = normalized_session_id(request.session_id)
        data = await self.request_json("POST", "/generate", payload)
        if not isinstance(data, dict):
            raise BackendError("Brain backend returned a non-object generation")
        contract_data = data.get("json", data)
        try:
            NoteContract.model_validate(contract_data)
        except Exception as exc:
            raise BackendError(f"Brain backend contract invalid: {exc}") from exc
        if "json" in data:
            return {**data["json"], **{key: value for key, value in data.items() if key != "json"}}
        return data

    async def feedback(self, request: FeedbackRequest) -> dict[str, Any]:
        payload = request.model_dump()
        payload["session_id"] = normalized_session_id(request.session_id)
        return await self.request_json("POST", "/feedback", payload, timeout=30.0)

    async def request_json(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | None = None,
        *,
        timeout: float | None = None,
    ) -> Any:
        try:
            async with httpx.AsyncClient(
                timeout=httpx.Timeout(
                    timeout or self.config.backend_timeout_seconds,
                    connect=2.0,
                ),
                transport=self.transport,
            ) as client:
                response = await client.request(
                    method,
                    f"{self.config.backend_url}{path}",
                    json=payload if payload is not None else None,
                    headers=self._headers(),
                )
        except httpx.HTTPError as exc:
            raise BackendError(f"Brain backend unreachable: {exc}") from exc
        if response.status_code >= 400:
            raise BackendError(
                f"Brain backend returned HTTP {response.status_code}: "
                f"{_extract_error(response)}"
            )
        try:
            value = response.json()
        except ValueError as exc:
            raise BackendError("Brain backend returned invalid JSON") from exc
        return value

    async def health(self) -> bool:
        try:
            async with httpx.AsyncClient(
                timeout=httpx.Timeout(2.0, connect=1.0),
                transport=self.transport,
            ) as client:
                response = await client.get(f"{self.config.backend_url}/health")
            return response.status_code < 500
        except httpx.HTTPError:
            return False


def _extract_error(response: httpx.Response) -> str:
    try:
        data = response.json()
        if isinstance(data, dict):
            value = data.get("detail") or data.get("error") or data.get("message")
            if value:
                return str(value)
    except ValueError:
        pass
    return response.text[:500] or "unknown error"

