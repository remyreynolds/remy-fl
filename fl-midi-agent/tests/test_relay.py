from __future__ import annotations

import asyncio
import json
import os
import threading
import time
from pathlib import Path

from fastapi.testclient import TestClient

from relay.app import create_app
from relay.backend import BackendError
from relay.bridge import FileBridge


class FakeBackend:
    def __init__(self, contract, error: str | None = None):
        self.contract = contract
        self.error = error
        self.requests = []

    async def generate(self, request):
        self.requests.append(request)
        if self.error:
            raise BackendError(self.error)
        return self.contract

    async def generate_full(self, request):
        contract = await self.generate(request)
        return {
            **contract.model_dump(),
            "description": "fixture generation",
            "trace_id": "fixture-trace",
        }

    async def health(self):
        return not bool(self.error)

    async def feedback(self, request):
        return {"ok": True, "rating": request.rating}


def test_generate_happy_path(relay_config, contract):
    fake = FakeBackend(contract)
    app = create_app(
        relay_config,
        fake,
        start_file_bridge=False,
    )
    with TestClient(app) as client:
        response = client.post(
            "/generate",
            json={
                "prompt": "deep house chords",
                "element": "Chords",
                "bars": 8,
                "session_id": "project.flp",
            },
        )
    assert response.status_code == 200
    assert response.json() == contract.model_dump()
    assert fake.requests[0].element == "Chords"


def test_generate_forwards_collaborator_context_untouched(
    relay_config,
    contract,
):
    fake = FakeBackend(contract)
    app = create_app(relay_config, fake, start_file_bridge=False)
    note = {
        "pitch": 53,
        "start_tick": 960,
        "length_ticks": 480,
        "velocity": 92,
        "selected": True,
    }
    with TestClient(app) as client:
        response = client.post(
            "/generate",
            json={
                "prompt": "fix my groove",
                "element": "Chords",
                "bars": 8,
                "mode": "Fix groove",
                "input_notes": [note],
                "selection_only": True,
                "roll_ppq": 96,
                "sound_type": "keys",
            },
        )
    assert response.status_code == 200
    forwarded = fake.requests[0]
    assert forwarded.mode == "Fix groove"
    assert forwarded.input_notes[0].model_dump() == note
    assert forwarded.selection_only is True
    assert forwarded.roll_ppq == 96
    assert forwarded.sound_type == "keys"


def test_backend_down_is_clean_502(relay_config, contract):
    fake = FakeBackend(contract, error="connection refused")
    app = create_app(relay_config, fake, start_file_bridge=False)
    with TestClient(app, raise_server_exceptions=False) as client:
        response = client.post(
            "/generate",
            json={"prompt": "bass", "element": "Bass", "bars": 4},
        )
    assert response.status_code == 502
    body = response.json()
    assert body["ok"] is False
    assert "connection refused" in body["error"]
    assert "Traceback" not in response.text


def test_health(relay_config, contract):
    app = create_app(
        relay_config,
        FakeBackend(contract),
        start_file_bridge=False,
    )
    with TestClient(app) as client:
        response = client.get("/health")
    assert response.status_code == 200
    assert response.json()["backend_reachable"] is True


def test_feedback_forwarded(relay_config, contract):
    app = create_app(
        relay_config,
        FakeBackend(contract),
        start_file_bridge=False,
    )
    with TestClient(app) as client:
        response = client.post(
            "/feedback",
            json={"session_id": "p", "rating": 1, "text": "groovy"},
        )
    assert response.status_code == 200
    assert response.json()["rating"] == 1


def test_companion_is_served_and_queue_feeds_next_fl_apply(
    relay_config,
    contract,
):
    app = create_app(
        relay_config,
        FakeBackend(contract),
        start_file_bridge=False,
    )
    with TestClient(app) as client:
        index = client.get("/")
        queued = client.post(
            "/companion/send-to-fl",
            json={"generation": contract.model_dump(), "roles": ["chords"]},
        )
        consumed = client.post(
            "/generate",
            json={
                "prompt": "receive queued chords",
                "element": "Chords",
                "bars": 8,
            },
        )
    assert index.status_code == 200
    # Companion frontend was rebranded to ComposerAI; check the served
    # index.html reflects that instead of the old product name.
    assert "ComposerAI" in index.text
    assert queued.json()["queued"] == 1
    assert consumed.status_code == 200
    assert consumed.json() == contract.model_dump()


def test_companion_generate_preserves_brain_metadata(
    relay_config,
    contract,
):
    app = create_app(
        relay_config,
        FakeBackend(contract),
        start_file_bridge=False,
    )
    with TestClient(app) as client:
        response = client.post(
            "/companion/generate",
            json={"prompt": "chords", "element": "Chords", "bars": 8},
        )
    assert response.status_code == 200
    assert response.json()["trace_id"] == "fixture-trace"
    assert "plan_summary" in response.json()


def test_file_watcher_creates_atomic_response(
    tmp_path: Path,
    relay_config,
    contract,
    logger,
):
    fake = FakeBackend(contract)
    bridge = FileBridge(
        relay_config.bridge_dir,
        fake,
        logger,
        poll_interval=0.01,
        use_watchdog=False,
    )
    bridge.start()
    try:
        request_path = relay_config.bridge_dir / "req_x.json"
        response_path = relay_config.bridge_dir / "res_x.json"
        request_path.write_text(
            json.dumps(
                {
                    "prompt": "deep house chords",
                    "element": "Chords",
                    "bars": 8,
                    "session_id": "flp",
                }
            ),
            encoding="utf-8",
        )

        parse_errors = []
        deadline = time.time() + 3
        parsed = None
        while time.time() < deadline:
            if response_path.exists():
                try:
                    parsed = json.loads(response_path.read_text(encoding="utf-8"))
                    break
                except json.JSONDecodeError as exc:
                    parse_errors.append(exc)
            time.sleep(0.001)

        assert not parse_errors, "response became visible before atomic JSON completed"
        assert parsed is not None
        assert parsed["meta"]["ppq"] == 960
        assert not request_path.exists()
        assert not (relay_config.bridge_dir / "res_x.json.tmp").exists()
    finally:
        bridge.stop()


def test_orphan_sweep_removes_old_responses(
    relay_config,
    contract,
    logger,
):
    relay_config.bridge_dir.mkdir(parents=True)
    stale = relay_config.bridge_dir / "res_old.json"
    fresh = relay_config.bridge_dir / "res_fresh.json"
    stale.write_text("{}")
    fresh.write_text("{}")
    old = time.time() - 700
    os.utime(stale, (old, old))

    bridge = FileBridge(relay_config.bridge_dir, FakeBackend(contract), logger)
    removed = bridge.sweep_orphans(max_age_seconds=600)
    assert removed == 1
    assert not stale.exists()
    assert fresh.exists()

