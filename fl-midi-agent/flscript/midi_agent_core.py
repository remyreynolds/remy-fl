"""Stdlib-only transport and note-writing helpers for FL Studio."""
from __future__ import annotations

try:
    import json
except ImportError:  # pragma: no cover - FL always needs JSON to use the bridge
    json = None
try:
    import os
except ImportError:  # pragma: no cover
    os = None
try:
    import time
except ImportError:  # pragma: no cover
    time = None
try:
    import uuid
except ImportError:  # pragma: no cover
    uuid = None
try:
    import urllib.error as urllib_error
    import urllib.request as urllib_request
except ImportError:  # pragma: no cover - explicitly supported by fallback tests
    urllib_error = None
    urllib_request = None
try:
    import http.client as http_client
except ImportError:  # pragma: no cover
    http_client = None
try:
    import socket
except ImportError:  # pragma: no cover
    socket = None


RELAY_HOST = "127.0.0.1"
RELAY_PORT = 8765
RELAY_PATH = "/generate"
BACKEND_PPQ = 960
ELEMENT_ROLES = {
    "Chords": "chords",
    "Bass": "bass",
    "Melody": "melody",
    "Arp": "arp",
    "Perc": "perc",
}


class RelayUnavailable(Exception):
    pass


class RelayError(Exception):
    pass


class FileBridgeTimeout(Exception):
    pass


def choose_transport(
    cache_path,
    urllib_module=urllib_request,
    socket_module=socket,
):
    """Choose once by stdlib capability and cache urllib/socket/file."""
    available = {
        "urllib": urllib_module is not None and http_client is not None,
        "socket": socket_module is not None,
        "file": True,
    }
    cached = _read_cached_transport(cache_path)
    if cached in available and available[cached]:
        return cached
    if available["urllib"]:
        selected = "urllib"
    elif available["socket"]:
        selected = "socket"
    else:
        selected = "file"
    _write_cached_transport(cache_path, selected)
    _log_transport(cache_path, selected)
    return selected


def send_generate(payload, script_dir, bridge_dir=None):
    cache_path = os.path.join(script_dir, "transport.json")
    mode = choose_transport(cache_path)
    body = json.dumps(payload).encode("utf-8")
    if mode == "urllib":
        return _urllib_post(body)
    if mode == "socket":
        return _socket_post(body)
    return _file_exchange(payload, bridge_dir=bridge_dir)


def _urllib_post(body):
    if urllib_request is None or http_client is None:
        raise RelayUnavailable("urllib transport is unavailable")

    class SplitTimeoutConnection(http_client.HTTPConnection):
        def connect(self):
            original_timeout = self.timeout
            self.timeout = 2.0
            try:
                http_client.HTTPConnection.connect(self)
            finally:
                self.timeout = original_timeout
            if self.sock is not None:
                self.sock.settimeout(60.0)

    class SplitTimeoutHandler(urllib_request.HTTPHandler):
        def http_open(self, request):
            return self.do_open(SplitTimeoutConnection, request, timeout=60.0)

    request = urllib_request.Request(
        "http://%s:%d%s" % (RELAY_HOST, RELAY_PORT, RELAY_PATH),
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        opener = urllib_request.build_opener(SplitTimeoutHandler())
        response = opener.open(request)
        try:
            raw = response.read()
        finally:
            response.close()
    except Exception as exc:
        if urllib_error is not None and isinstance(exc, urllib_error.HTTPError):
            try:
                raw = exc.read()
            except Exception:
                raise RelayUnavailable(str(exc))
            return _decode_response(raw)
        raise RelayUnavailable(str(exc))
    return _decode_response(raw)


def _socket_post(body):
    if socket is None:
        raise RelayUnavailable("socket transport is unavailable")
    request = (
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        % (RELAY_PATH, RELAY_HOST, RELAY_PORT, len(body))
    ).encode("ascii") + body
    client = None
    try:
        client = socket.create_connection((RELAY_HOST, RELAY_PORT), timeout=2.0)
        client.settimeout(60.0)
        client.sendall(request)
        chunks = []
        while True:
            chunk = client.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
    except Exception as exc:
        raise RelayUnavailable(str(exc))
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:
                pass
    raw = b"".join(chunks)
    marker = raw.find(b"\r\n\r\n")
    if marker < 0:
        raise RelayError("Relay returned malformed HTTP")
    return _decode_response(raw[marker + 4 :])


def _file_exchange(payload, bridge_dir=None, timeout_seconds=90.0):
    if bridge_dir is None:
        bridge_dir = os.path.expanduser("~/Documents/MIDIAgent/bridge")
    os.makedirs(bridge_dir, exist_ok=True)
    request_id = (
        uuid.uuid4().hex
        if uuid is not None
        else "%d" % int(time.time() * 1000000)
    )
    request_path = os.path.join(bridge_dir, "req_%s.json" % request_id)
    request_tmp = request_path + ".tmp"
    response_path = os.path.join(bridge_dir, "res_%s.json" % request_id)
    with open(request_tmp, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
        handle.flush()
    os.replace(request_tmp, request_path)

    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if os.path.exists(response_path):
            try:
                with open(response_path, "r", encoding="utf-8") as handle:
                    result = json.load(handle)
            finally:
                try:
                    os.remove(response_path)
                except OSError:
                    pass
            return _validate_response(result)
        time.sleep(0.25)

    try:
        os.remove(request_path)
    except OSError:
        pass
    raise FileBridgeTimeout("MIDI Agent timed out after 90 seconds.")


def _decode_response(raw):
    try:
        value = json.loads(raw.decode("utf-8"))
    except Exception:
        raise RelayError("Relay returned invalid JSON")
    return _validate_response(value)


def _validate_response(value):
    if not isinstance(value, dict):
        raise RelayError("Relay returned an invalid response")
    if value.get("ok") is False:
        raise RelayError(str(value.get("error") or "MIDI Agent generation failed"))
    if "meta" not in value or "tracks" not in value:
        raise RelayError(str(value.get("error") or "Relay response has no MIDI tracks"))
    return value


def find_track(contract, element):
    role = ELEMENT_ROLES.get(element, str(element).lower())
    for track in contract.get("tracks", []):
        if str(track.get("role", "")).lower() == role:
            return track
    tracks = contract.get("tracks", [])
    if len(tracks) == 1:
        return tracks[0]
    raise RelayError("No %s track was returned by the brain." % element)


def backend_tick_to_fl(value, fl_ppq):
    return max(0, int(round(float(value) * float(fl_ppq) / BACKEND_PPQ)))


def fl_tick_to_backend(value, fl_ppq):
    return max(0, int(round(float(value) * BACKEND_PPQ / float(fl_ppq))))


def velocity_to_fl(value):
    return max(0.01, min(1.0, float(value) / 127.0))


def velocity_to_backend(value):
    return max(1, min(127, int(round(float(value) * 127.0))))


def read_roll(score):
    """Return standardized 960-PPQ notes and their selected FL indices."""
    notes = []
    selected_indices = []
    for index in range(int(score.noteCount)):
        source = score.getNote(index)
        selected = bool(getattr(source, "selected", False))
        if selected:
            selected_indices.append(index)
        notes.append(
            {
                "pitch": max(0, min(127, int(source.number))),
                "start_tick": fl_tick_to_backend(source.time, int(score.PPQ)),
                "length_ticks": max(
                    1, fl_tick_to_backend(source.length, int(score.PPQ))
                ),
                "velocity": velocity_to_backend(source.velocity),
                "selected": selected,
            }
        )
    return notes, selected_indices


def write_track(score, note_factory, track, mode, replace_indices=None):
    normalized_mode = str(mode).lower()
    if replace_indices is not None:
        for index in sorted(set(int(i) for i in replace_indices), reverse=True):
            if 0 <= index < int(score.noteCount):
                score.deleteNote(index)
    elif normalized_mode in ("replace", "generate", "fix groove", "reharmonize"):
        for index in range(int(score.noteCount) - 1, -1, -1):
            score.deleteNote(index)

    converted = _converted_notes(track.get("notes", []), int(score.PPQ))
    for item in converted:
        note = note_factory()
        note.number = item["pitch"]
        note.time = item["time"]
        note.length = item["length"]
        note.velocity = item["velocity"]
        note.pan = 0.5
        score.addNote(note)
    return len(converted)


def _converted_notes(notes, fl_ppq):
    result = []
    seen = set()
    ordered = sorted(
        notes,
        key=lambda item: (
            max(0, int(item.get("start_tick", 0))),
            int(item.get("pitch", 0)),
        ),
    )
    for source in ordered:
        pitch = max(0, min(127, int(source.get("pitch", 0))))
        start = backend_tick_to_fl(source.get("start_tick", 0), fl_ppq)
        dedupe_key = (pitch, start)
        if dedupe_key in seen:
            continue
        seen.add(dedupe_key)
        result.append(
            {
                "pitch": pitch,
                "time": start,
                "length": max(
                    1,
                    backend_tick_to_fl(source.get("length_ticks", 1), fl_ppq),
                ),
                "velocity": velocity_to_fl(source.get("velocity", 1)),
            }
        )
    return result


def _read_cached_transport(cache_path):
    try:
        with open(cache_path, "r", encoding="utf-8") as handle:
            value = json.load(handle)
        return value.get("transport")
    except Exception:
        return None


def _write_cached_transport(cache_path, selected):
    try:
        with open(cache_path, "w", encoding="utf-8") as handle:
            json.dump({"transport": selected}, handle)
    except Exception:
        pass


def _log_transport(cache_path, selected):
    try:
        log_path = os.path.join(os.path.dirname(cache_path), "midi-agent.log")
        with open(log_path, "a", encoding="utf-8") as handle:
            handle.write("transport=%s\n" % selected)
    except Exception:
        pass

