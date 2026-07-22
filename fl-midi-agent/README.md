# FL Studio MIDI Agent

This integration contains three runtime components:

1. `relay/`: a Python 3.11 FastAPI daemon that proxies the existing MIDI brain.
2. `flscript/`: a stdlib-only FL Studio 21.2+ Piano Roll script.
3. `../ui/`: the React companion app, built into and served by the relay.

The script writes generated notes into the piano roll from which it was launched.
That piano roll's channel—Serum, another synth, or a drum instrument—is the
sound target. It does not select channels or change FL Studio's tempo.

## Install

The brain backend must already be running and expose `/generate`, `/feedback`,
and optionally `/health`.

```bash
cd fl-midi-agent
pipx install .
midiagent-relay --install-script
midiagent-relay
```

For a venv instead:

```bash
python3.11 -m venv .venv
.venv/bin/pip install .
.venv/bin/midiagent-relay --install-script
.venv/bin/midiagent-relay
```

The installer copies the script to:

- macOS: `~/Documents/Image-Line/FL Studio/Settings/Piano roll scripts/MIDI Agent/`
- Windows: `%USERPROFILE%\Documents\Image-Line\FL Studio\Settings\Piano roll scripts\MIDI Agent\`

Restart FL Studio after first installation. The command appears under Piano
Roll → burger menu → Tools → Scripts → MIDI Agent.

## Configure

The first relay run creates `~/.midiagent/config.toml`:

```toml
backend_url = "http://127.0.0.1:8000"
api_key = ""
host = "127.0.0.1"
port = 8765
backend_timeout_seconds = 90.0
bridge_dir = "/Users/you/Documents/MIDIAgent/bridge"
```

`MIDIAGENT_BACKEND_URL` and `MIDIAGENT_API_KEY` override the corresponding
file values. Relay logs are written to `~/.midiagent/logs/relay.log`.

Check the bridge:

```bash
curl http://127.0.0.1:8765/health
```

Expected shape:

```json
{"status":"ok","backend_reachable":true,"transport_hint":"http; file fallback available"}
```

Open the companion at `http://127.0.0.1:8765`. Its **Send to FL** action queues
notes by role. Because FL piano-roll scripts only run when invoked, open the
target piano roll and run MIDI Agent Apply with the matching Element to consume
the queue. A browser cannot call FL's sandboxed `flpianoroll` API directly.

## Transport behavior

The FL script uses `urllib` first, then a raw HTTP/1.1 socket when `urllib` is
unavailable, then file exchange. The selected capability is cached in
`transport.json` beside the installed script and recorded in `midi-agent.log`.

HTTP uses a 2-second TCP connect timeout and a 60-second response timeout. File
mode writes requests under `~/Documents/MIDIAgent/bridge/`, polls every 250 ms,
and times out after 90 seconds. Relay responses are written atomically.

To force transport re-detection after changing the FL environment, delete
`transport.json` from the installed `MIDI Agent` script folder.

## Manual acceptance (FL Studio 21.2+)

These steps require an interactive FL Studio installation and must be checked
manually:

1. Start the brain and `midiagent-relay`; verify `/health` reports
   `backend_reachable: true`. Capture `01-relay-health.png`.
2. Open FL Studio, load Serum on a channel, and open that channel's piano roll.
   Capture `02-serum-piano-roll.png`.
3. Run Tools → Scripts → MIDI Agent with `tech house rolling bass, 8 bars,
   F minor, 126bpm`, Element `Bass`, Mode `Replace`, Bars `8`.
4. Confirm notes appear within 30 seconds and play through Serum on spacebar.
   Capture `03-generated-bass.png`.
5. Run `melody` in Add mode on the same piano roll and confirm the bass is
   preserved. Then run Melody on a second channel to verify the intended
   per-channel workflow. Capture `04-add-and-second-channel.png`.
6. Stop the relay and run the script again. Confirm FL displays:
   `MIDI Agent relay isn't running. Start it with: midiagent-relay`.
   Capture `05-relay-offline-message.png`.

Store acceptance screenshots in `docs/screenshots/`. They are intentionally not
fabricated by the automated test suite.

## Troubleshooting

| Problem | Cause and fix |
|---|---|
| MIDI Agent is absent from Tools → Scripts | FL Studio must be 21.2+; rerun `midiagent-relay --install-script`, confirm the exact Piano roll scripts folder, then restart FL. |
| Generation times out | Check `~/.midiagent/logs/relay.log`; the brain/LLM may exceed 60 seconds over HTTP or 90 seconds in file mode. |
| Port 8765 is already in use | Stop the conflicting process, or change `port` in config and rerun `midiagent-relay --install-script`; the installer synchronizes the script's port. |
| File-mode fallback is active | This is expected when HTTP modules are unavailable. Check `midi-agent.log`, ensure the relay watches `~/Documents/MIDIAgent/bridge/`, or delete `transport.json` to re-detect. |
| Notes appear on the wrong sound | Launch the script from the target channel's piano roll. The script deliberately has no channel-selection logic. |
| Percussion pitches sound wrong | Perc uses GM pitches such as 36/38/42/46. Point that piano roll at a compatible drum channel or remap the instrument. |
| Relay health is OK but backend is unreachable | Start the existing brain service or correct `backend_url` in `~/.midiagent/config.toml`. |

## Development

```bash
python3.11 -m venv .venv
.venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest -c pyproject.toml tests
```

The tests mock both the brain backend and `flpianoroll`; FL Studio is not
required for automated verification.

