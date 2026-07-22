"""File-exchange fallback for FL Studio's stripped Python runtime."""
from __future__ import annotations

import asyncio
import json
import logging
import os
import threading
import time
from pathlib import Path
from typing import Callable

from .backend import BackendError, BrainBackendClient
from .models import GenerateRequest

try:
    from watchdog.events import FileSystemEventHandler
    from watchdog.observers import Observer
except ImportError:  # pragma: no cover - dependency is normal, polling remains valid
    FileSystemEventHandler = object  # type: ignore[assignment,misc]
    Observer = None  # type: ignore[assignment,misc]


class FileBridge:
    def __init__(
        self,
        bridge_dir: Path,
        backend: BrainBackendClient,
        logger: logging.Logger,
        poll_interval: float = 0.25,
        use_watchdog: bool = True,
    ):
        self.bridge_dir = bridge_dir
        self.backend = backend
        self.logger = logger
        self.poll_interval = poll_interval
        self.use_watchdog = use_watchdog
        self._stop = threading.Event()
        self._poll_thread: threading.Thread | None = None
        self._observer = None
        self._in_flight: set[Path] = set()
        self._guard = threading.Lock()

    def start(self) -> None:
        self.bridge_dir.mkdir(parents=True, exist_ok=True)
        self.sweep_orphans()
        self._stop.clear()
        if self.use_watchdog and Observer is not None:
            handler = _BridgeEventHandler(self)
            self._observer = Observer()
            self._observer.schedule(handler, str(self.bridge_dir), recursive=False)
            self._observer.start()
            self.logger.info("file bridge watcher=watchdog dir=%s", self.bridge_dir)
        else:
            self.logger.info("file bridge watcher=poll dir=%s", self.bridge_dir)
        # Keep polling even with watchdog: catches startup races and dropped events.
        self._poll_thread = threading.Thread(
            target=self._poll_loop,
            name="midiagent-file-bridge",
            daemon=True,
        )
        self._poll_thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._observer is not None:
            self._observer.stop()
            self._observer.join(timeout=2)
            self._observer = None
        if self._poll_thread is not None:
            self._poll_thread.join(timeout=2)
            self._poll_thread = None

    def submit(self, path: Path) -> None:
        if not _is_request(path):
            return
        with self._guard:
            if path in self._in_flight:
                return
            self._in_flight.add(path)
        threading.Thread(
            target=self._process_guarded,
            args=(path,),
            name=f"midiagent-{path.stem}",
            daemon=True,
        ).start()

    def _process_guarded(self, path: Path) -> None:
        try:
            self.process_request_file(path)
        finally:
            with self._guard:
                self._in_flight.discard(path)

    def process_request_file(self, path: Path) -> None:
        if not path.exists():
            return
        request_id = path.stem.removeprefix("req_")
        response_path = self.bridge_dir / f"res_{request_id}.json"
        started = time.monotonic()
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
            request = GenerateRequest.model_validate(payload)
            contract = asyncio.run(self.backend.generate(request))
            response = contract.model_dump()
            ok = True
        except (OSError, ValueError, BackendError) as exc:
            response = {"ok": False, "error": str(exc)}
            ok = False

        _atomic_json_write(response_path, response)
        try:
            path.unlink(missing_ok=True)
        except OSError:
            self.logger.warning("could not delete request file=%s", path)
        latency_ms = int((time.monotonic() - started) * 1000)
        self.logger.info(
            "file_generate id=%s ok=%s latency_ms=%d request=%s response=%s",
            request_id,
            ok,
            latency_ms,
            _safe_json(payload if "payload" in locals() else {}),
            _safe_json(response),
        )

    def sweep_orphans(self, max_age_seconds: float = 600.0) -> int:
        self.bridge_dir.mkdir(parents=True, exist_ok=True)
        now = time.time()
        removed = 0
        for path in self.bridge_dir.glob("res_*.json"):
            try:
                if now - path.stat().st_mtime > max_age_seconds:
                    path.unlink()
                    removed += 1
            except OSError:
                continue
        if removed:
            self.logger.info("swept orphan responses count=%d", removed)
        return removed

    def _poll_loop(self) -> None:
        last_sweep = 0.0
        while not self._stop.wait(self.poll_interval):
            for path in self.bridge_dir.glob("req_*.json"):
                self.submit(path)
            now = time.monotonic()
            if now - last_sweep > 60:
                self.sweep_orphans()
                last_sweep = now


class _BridgeEventHandler(FileSystemEventHandler):  # type: ignore[misc]
    def __init__(self, bridge: FileBridge):
        self.bridge = bridge

    def on_created(self, event) -> None:  # noqa: ANN001
        if not event.is_directory:
            self.bridge.submit(Path(event.src_path))

    def on_moved(self, event) -> None:  # noqa: ANN001
        if not event.is_directory:
            self.bridge.submit(Path(event.dest_path))


def _is_request(path: Path) -> bool:
    return path.name.startswith("req_") and path.suffix == ".json"


def _atomic_json_write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    text = json.dumps(value, separators=(",", ":"))
    with tmp.open("w", encoding="utf-8") as handle:
        handle.write(text)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)


def _safe_json(value: dict) -> str:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True)[:4000]

