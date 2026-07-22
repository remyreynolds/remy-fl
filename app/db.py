"""Backward-compatible name for the cognitive memory system."""
from app.memory import CognitiveMemory, DEFAULT_DB


class StyleLibrary(CognitiveMemory):
    """Compatibility alias retained for existing app and tests."""

