#!/bin/bash
# ============================================================================
# AI MIDI Gen — one-command macOS build & install
#
#   bash <(curl -fsSL https://raw.githubusercontent.com/remyreynolds/remy-fl/ai-midi-gen-plugin/install-mac.sh)
#
# What it does (all automatic):
#   1. Makes sure the compiler (Xcode Command Line Tools) is installed
#   2. Makes sure CMake is installed (via Homebrew if needed)
#   3. Clones/updates the plugin source into ~/ai-midi-gen
#   4. Builds the VST3 + AU + Standalone (first build downloads JUCE, ~5-10 min)
#   5. Copies the plugin into your plugin folders so FL Studio can find it
# ============================================================================
set -e

# NOTE: the plugin currently lives on a dedicated branch of the remy-fl repo
# (the GitHub token available to the agent can only write there). Once a
# dedicated ai-midi-gen repo exists, change REPO and drop BRANCH.
REPO="https://github.com/remyreynolds/remy-fl.git"
BRANCH="ai-midi-gen-plugin"
DIR="$HOME/ai-midi-gen"

say()  { printf "\n\033[1;36m==> %s\033[0m\n" "$*"; }
fail() { printf "\n\033[1;31mERROR: %s\033[0m\n" "$*"; exit 1; }

# ---------------------------------------------------------------- 1. compiler
if ! xcode-select -p >/dev/null 2>&1; then
  say "Installing Xcode Command Line Tools (a dialog will pop up — click Install)…"
  xcode-select --install || true
  fail "Re-run this script after the Command Line Tools finish installing."
fi
say "Compiler: OK ($(clang --version | head -1))"

# ------------------------------------------------------------------ 2. cmake
if ! command -v cmake >/dev/null 2>&1; then
  if ! command -v brew >/dev/null 2>&1; then
    say "Installing Homebrew (needed once, to get CMake)…"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Apple Silicon brew lives in /opt/homebrew, Intel in /usr/local
    eval "$(/opt/homebrew/bin/brew shellenv 2>/dev/null || /usr/local/bin/brew shellenv)"
  fi
  say "Installing CMake…"
  brew install cmake
fi
say "CMake: OK ($(cmake --version | head -1))"

# ----------------------------------------------------------------- 3. source
if [ -d "$DIR/.git" ]; then
  say "Updating source in $DIR…"
  git -C "$DIR" pull --ff-only 2>/dev/null \
    || say "Could not update from GitHub — building the local copy as-is."
elif [ -d "$DIR/src" ]; then
  say "Using existing source in $DIR (no git remote)…"
else
  say "Cloning source into $DIR…"
  git clone -b "$BRANCH" --single-branch "$REPO" "$DIR"
fi

# ------------------------------------------------------------------ 4. build
cd "$DIR"
say "Configuring (first run downloads JUCE — a few minutes)…"
cmake -B build -DCMAKE_BUILD_TYPE=Release

say "Building VST3 + AU + Standalone (first build ~5-10 min)…"
CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build build --config Release -j "$CORES"

# ---------------------------------------------------------------- 5. install
# COPY_PLUGIN_AFTER_BUILD already copies to the user plugin folders; verify.
VST3_USER="$HOME/Library/Audio/Plug-Ins/VST3/AI MIDI Gen.vst3"
AU_USER="$HOME/Library/Audio/Plug-Ins/Components/AI MIDI Gen.component"
ART="build/AIMidiGen_artefacts/Release"

if [ ! -d "$VST3_USER" ] && [ -d "$ART/VST3/AI MIDI Gen.vst3" ]; then
  mkdir -p "$(dirname "$VST3_USER")"
  cp -R "$ART/VST3/AI MIDI Gen.vst3" "$VST3_USER"
fi
if [ ! -d "$AU_USER" ] && [ -d "$ART/AU/AI MIDI Gen.component" ]; then
  mkdir -p "$(dirname "$AU_USER")"
  cp -R "$ART/AU/AI MIDI Gen.component" "$AU_USER"
fi

[ -d "$VST3_USER" ] || fail "Build finished but the VST3 wasn't found — send me the output above."

say "DONE! 🎛  AI MIDI Gen is installed."
cat << 'NEXT'

  Open it in FL Studio:
    1. FL Studio → Options → Manage plugins → click "Find plugins"
    2. Wait for the scan — "AI MIDI Gen" appears under Installed
    3. Add it to a channel, pick a Style (Tech House, Afro House, UK Garage…)
       and hit Generate All
    4. Drag any part straight into the Playlist / Piano roll

  Optional — enable the Claude AI chat:
    Paste your Anthropic API key into the "Claude API key" field in the plugin.
    Without a key it still generates using the built-in style engine.

  Standalone app (test without FL Studio):
    ~/ai-midi-gen/build/AIMidiGen_artefacts/Release/Standalone/AI MIDI Gen.app

NEXT
