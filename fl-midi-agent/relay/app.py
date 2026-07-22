"""FastAPI relay on 127.0.0.1:8765."""
from __future__ import annotations

import json
import logging
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import AsyncIterator
from urllib.parse import urlencode

from fastapi import FastAPI, Body, File, HTTPException, UploadFile
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from .backend import BackendError, BrainBackendClient, normalized_session_id
from .bridge import FileBridge
from .companion import CompanionStore
from .config import RelayConfig, load_config
from .logging_setup import configure_logging
from .models import FeedbackRequest, GenerateRequest


def create_app(
    config: RelayConfig | None = None,
    backend: BrainBackendClient | None = None,
    *,
    start_file_bridge: bool = True,
    logger: logging.Logger | None = None,
) -> FastAPI:
    cfg = config or load_config()
    log = logger or configure_logging(cfg.log_dir)
    client = backend or BrainBackendClient(cfg)
    bridge = FileBridge(cfg.bridge_dir, client, log)
    companion = CompanionStore(cfg.log_dir.parent / "companion.json")
    static_dir = Path(__file__).with_name("static")

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        if start_file_bridge:
            bridge.start()
        try:
            yield
        finally:
            if start_file_bridge:
                bridge.stop()

    app = FastAPI(title="MIDI Agent Relay", version="0.1.0", lifespan=lifespan)
    app.state.config = cfg
    app.state.backend = client
    app.state.file_bridge = bridge
    app.state.companion = companion

    @app.post("/generate")
    async def generate(request: GenerateRequest):
        started = time.monotonic()
        payload = request.model_dump()
        payload["session_id"] = normalized_session_id(request.session_id)
        queued = companion.dequeue(request.element.lower())
        if queued is not None:
            log.info("http_generate source=companion_queue role=%s", request.element)
            return queued.model_dump()
        try:
            contract = await client.generate(request)
        except BackendError as exc:
            latency_ms = int((time.monotonic() - started) * 1000)
            log.warning(
                "http_generate ok=false latency_ms=%d request=%s error=%s",
                latency_ms,
                _safe_json(payload),
                str(exc),
            )
            return JSONResponse(
                status_code=502,
                content={"ok": False, "error": str(exc)},
            )

        result = contract.model_dump()
        latency_ms = int((time.monotonic() - started) * 1000)
        log.info(
            "http_generate ok=true latency_ms=%d request=%s response=%s",
            latency_ms,
            _safe_json(payload),
            _safe_json(result),
        )
        return result

    @app.get("/health")
    async def health():
        reachable = await client.health()
        return {
            "status": "ok",
            "backend_reachable": reachable,
            "transport_hint": "http; file fallback available",
        }

    @app.post("/feedback")
    async def feedback(request: FeedbackRequest):
        try:
            return await client.feedback(request)
        except BackendError as exc:
            return JSONResponse(
                status_code=502,
                content={"ok": False, "error": str(exc)},
            )

    @app.post("/companion/generate")
    async def companion_generate(request: GenerateRequest):
        try:
            value = await client.generate_full(request)
        except BackendError as exc:
            raise HTTPException(status_code=502, detail=str(exc)) from exc
        value.setdefault(
            "plan_summary",
            _plan_summary(value),
        )
        return value

    @app.post("/companion/send-to-fl")
    async def send_to_fl(payload: dict = Body(...)):
        generation = payload.get("generation")
        if not isinstance(generation, dict):
            raise HTTPException(status_code=422, detail="generation is required")
        try:
            queued = companion.enqueue(generation, payload.get("roles"))
        except Exception as exc:
            raise HTTPException(status_code=422, detail=str(exc)) from exc
        return {
            "queued": queued,
            "instruction": (
                "Run MIDI Agent Apply in each target piano roll to consume "
                "the queued element."
            ),
        }

    @app.get("/companion/session/{session_id}")
    async def companion_session(session_id: str):
        return companion.session(session_id)

    @app.put("/companion/session/{session_id}")
    async def save_companion_session(session_id: str, payload: dict = Body(...)):
        companion.save_session(session_id, payload)
        return {"ok": True}

    @app.get("/companion/history")
    async def history(
        session_id: str, q: str = "", include_past: bool = True
    ):
        query = urlencode(
            {
                "session_id": session_id,
                "q": q,
                "include_past": str(include_past).lower(),
            }
        )
        return await _proxy(
            client,
            "GET",
            f"/companion/history?{query}",
        )

    @app.get("/companion/brain")
    async def brain_state():
        return await _proxy(client, "GET", "/companion/brain")

    @app.delete("/companion/brain/taste/{belief_id}")
    async def delete_taste(belief_id: str):
        return await _proxy(
            client, "DELETE", f"/companion/brain/taste/{belief_id}"
        )

    @app.patch("/companion/brain/heuristics/{heuristic_id}")
    async def toggle_heuristic(heuristic_id: int, payload: dict = Body(...)):
        return await _proxy(
            client,
            "PATCH",
            f"/companion/brain/heuristics/{heuristic_id}",
            payload,
        )

    @app.get("/companion/references")
    async def references():
        return await _proxy(client, "GET", "/companion/references")

    @app.post("/companion/references/analyze")
    async def analyze_reference(payload: dict = Body(...)):
        return await _proxy(
            client, "POST", "/companion/references/analyze", payload
        )

    @app.post("/companion/references/analyze-midi")
    async def analyze_midi(file: UploadFile = File(...)):
        content = await file.read()
        return await _proxy(
            client,
            "POST",
            "/companion/references/analyze-midi",
            {"filename": file.filename, "content_hex": content.hex()},
        )

    @app.post("/companion/references")
    async def save_reference(payload: dict = Body(...)):
        return await _proxy(client, "POST", "/companion/references", payload)

    @app.delete("/companion/references/{fingerprint_id}")
    async def delete_reference(fingerprint_id: int):
        return await _proxy(
            client, "DELETE", f"/companion/references/{fingerprint_id}"
        )

    if static_dir.exists():
        assets = static_dir / "assets"
        if assets.exists():
            app.mount("/assets", StaticFiles(directory=assets), name="assets")

        @app.get("/", include_in_schema=False)
        async def companion_index():
            return FileResponse(static_dir / "index.html")

    return app


def _safe_json(value: dict) -> str:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True)[:4000]


async def _proxy(
    client: BrainBackendClient,
    method: str,
    path: str,
    payload: dict | None = None,
):
    try:
        return await client.request_json(method, path, payload)
    except BackendError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


def _plan_summary(value: dict) -> str:
    meta = value.get("meta", {})
    tracks = value.get("tracks", [])
    roles = " + ".join(str(track.get("role", "")).title() for track in tracks)
    summary = (
        f"{meta.get('key', '—')} · {roles or 'MIDI'} · "
        f"{meta.get('swing_pct', '—')}% swing"
    )
    if value.get("sound_type"):
        summary += f" · {value['sound_type']} gating"
    return summary

