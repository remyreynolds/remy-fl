# MIDI Agent Companion

The companion is a React + Vite studio instrument served by `midiagent-relay`.
It has four surfaces only: Generate, History, Brain, and References.

## Run

```bash
cd ui
npm install
npm test
npm run build
cd ../fl-midi-agent
midiagent-relay
```

Open `http://127.0.0.1:8765`. For UI work without a brain or relay:

```bash
npm run dev
# open http://127.0.0.1:5173/?mock=1
```

`npm run storybook` opens the four mocked surface states. The production build is
written to `fl-midi-agent/relay/static` and packaged with the relay.

## FL handoff

FL Studio piano-roll scripts are not persistent processes. A browser cannot
invoke `flpianoroll` directly. **Send to FL** therefore queues each selected
role in the relay. Open the target channel's piano roll and run **Tools →
Scripts → MIDI Agent → Apply** with the matching Element. The relay returns the
queued notes before making a new brain request. For a stack, repeat this once
per target channel.

## Keyboard map

- `Space` — play/stop the WebAudio loop
- `⌘↵` / `Ctrl+↵` — generate
- `1`, `2`, `3` — select take A/B/C
- `?` — shortcut overlay
- `Esc` — close overlays

## Deliberate cut list

These are product decisions, not omissions:

- **Chat interface.** Prompt + Tweak covers loop iteration without scroll
  archaeology.
- **In-app piano roll editing.** FL Studio remains the MIDI editor; preview is
  read-only.
- **Settings page.** The relay owns backend URL, API key, model routing, and
  port in `config.toml`; the UI exposes only connection status.
- **Fingerprint editor / knowledge-base CMS.** References are read, saved, or
  deleted only.
- **Arrangement / song view.** This tool makes loops; FL makes songs.
- **Per-note parameter controls.** Swing, Density, and Dark↔Bright are the only
  macros; all other changes are prompt words.
- **Accounts, auth, cloud sync, sharing, and community presets.** This is a
  localhost single-user instrument.
- **Mobile layout.** Below 1100px the app asks to be opened on the studio
  machine.
- **Onboarding tours and pervasive tooltips.** Surface empty states are the
  onboarding.
- **Metrics dashboards.** Operational detail stays in `/debug/trace/{id}`;
  Brain shows musical learning only.

## Verification

```bash
npm test
npm run build
npm run build-storybook
```

The component suite covers Session Strip locks and slot states, tick-accurate
preview geometry, global shortcuts, relay-down toasts, and the 1100px layout
contract.

# React + TypeScript + Vite

This template provides a minimal setup to get React working in Vite with HMR and some Oxlint rules.

Currently, two official plugins are available:

- [@vitejs/plugin-react](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react) uses [Oxc](https://oxc.rs)
- [@vitejs/plugin-react-swc](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react-swc) uses [SWC](https://swc.rs/)

## React Compiler

The React Compiler is not enabled on this template because of its impact on dev & build performances. To add it, see [this documentation](https://react.dev/learn/react-compiler/installation).

## Expanding the Oxlint configuration

If you are developing a production application, we recommend enabling type-aware lint rules by installing `oxlint-tsgolint` and editing `.oxlintrc.json`:

```json
{
  "$schema": "./node_modules/oxlint/configuration_schema.json",
  "plugins": ["react", "typescript", "oxc"],
  "options": {
    "typeAware": true
  },
  "rules": {
    "react/rules-of-hooks": "error",
    "react/only-export-components": ["warn", { "allowConstantExport": true }]
  }
}
```

See the [Oxlint rules documentation](https://oxc.rs/docs/guide/usage/linter/rules) for the full list of rules and categories.
