"""Load the Groovewright brain prompt from disk — never inline as a string literal."""
from __future__ import annotations

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROMPT_PATH = REPO_ROOT / "brain" / "2-prompts" / "house-midi-agent-master-prompt.md"


class PromptLoadError(FileNotFoundError):
    pass


def load_brain_prompt(path: Path | None = None) -> str:
    """Load the master system prompt verbatim. Fail loudly if missing/empty."""
    prompt_path = Path(path) if path is not None else DEFAULT_PROMPT_PATH
    if not prompt_path.exists():
        raise PromptLoadError(
            f"Brain prompt missing: {prompt_path}. "
            "Expected brain/2-prompts/house-midi-agent-master-prompt.md"
        )
    text = prompt_path.read_text(encoding="utf-8").strip()
    if not text:
        raise PromptLoadError(f"Brain prompt file is empty: {prompt_path}")
    if "Groovewright" not in text and "MASTER SYSTEM PROMPT" not in text:
        raise PromptLoadError(
            f"Brain prompt does not look like the house agent master file: {prompt_path}"
        )
    return text


def prompt_path() -> Path:
    return DEFAULT_PROMPT_PATH
