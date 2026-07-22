from __future__ import annotations

import json
import runpy
import sys
import types
from pathlib import Path

import pytest

from flscript import midi_agent_core as core


class MockNote:
    def __init__(
        self,
        number=60,
        time=0,
        length=24,
        velocity=0.75,
        selected=False,
    ):
        self.number = number
        self.time = time
        self.length = length
        self.velocity = velocity
        self.selected = selected


class MockScore:
    def __init__(self, ppq=96, existing=0):
        self.PPQ = ppq
        self.notes = [MockNote() for _ in range(existing)]
        self.deleted = []

    @property
    def noteCount(self):
        return len(self.notes)

    def deleteNote(self, index):
        self.deleted.append(index)
        del self.notes[index]

    def addNote(self, note):
        self.notes.append(note)

    def getNote(self, index):
        return self.notes[index]


def test_tick_conversion_at_ppq_96():
    assert core.backend_tick_to_fl(960, 96) == 96
    assert core.backend_tick_to_fl(480, 96) == 48
    assert core.backend_tick_to_fl(-10, 96) == 0


def test_velocity_conversion():
    assert core.velocity_to_fl(92) == pytest.approx(0.724, abs=0.001)
    assert core.velocity_to_fl(127) == 1.0
    assert core.velocity_to_fl(1) >= 0.01
    assert core.velocity_to_backend(92 / 127.0) == 92


def test_roll_round_trip_at_ppq_96_preserves_grid():
    score = MockScore(ppq=96)
    score.notes = [
        MockNote(number=60, time=0, length=48, velocity=92 / 127.0),
        MockNote(number=64, time=48, length=24, velocity=100 / 127.0),
    ]
    input_notes, selected = core.read_roll(score)
    assert selected == []
    assert input_notes[1]["start_tick"] == 480
    assert input_notes[1]["length_ticks"] == 240

    core.write_track(
        score,
        MockNote,
        {"notes": input_notes},
        "Replace",
    )
    assert [(note.time, note.length) for note in score.notes] == [(0, 48), (48, 24)]


def test_replace_deletes_in_reverse_and_writes_notes():
    score = MockScore(existing=4)
    track = {
        "notes": [
            {
                "pitch": 53,
                "start_tick": 960,
                "length_ticks": 480,
                "velocity": 92,
            }
        ]
    }
    written = core.write_track(score, MockNote, track, "Replace")
    assert score.deleted == [3, 2, 1, 0]
    assert written == 1
    note = score.notes[0]
    assert note.number == 53
    assert note.time == 96
    assert note.length == 48
    assert note.velocity == pytest.approx(0.724, abs=0.001)
    assert note.pan == 0.5


def test_add_preserves_existing_notes():
    score = MockScore(existing=2)
    track = {
        "notes": [
            {
                "pitch": 48,
                "start_tick": 0,
                "length_ticks": 240,
                "velocity": 100,
            }
        ]
    }
    core.write_track(score, MockNote, track, "Add")
    assert score.deleted == []
    assert len(score.notes) == 3


def test_selected_collaborator_replace_preserves_eight_of_twelve_notes():
    score = MockScore(ppq=96)
    score.notes = [
        MockNote(
            number=48 + index,
            time=index * 24,
            selected=index in {1, 4, 7, 10},
        )
        for index in range(12)
    ]
    untouched = [
        note for index, note in enumerate(score.notes) if index not in {1, 4, 7, 10}
    ]
    input_notes, selected = core.read_roll(score)
    sent = [input_notes[index] for index in selected]
    core.write_track(
        score,
        MockNote,
        {"notes": sent},
        "Add",
        replace_indices=selected,
    )
    assert selected == [1, 4, 7, 10]
    assert len(score.notes) == 12
    assert all(note in score.notes for note in untouched)
    assert score.deleted == [10, 7, 4, 1]


def test_rounding_clamps_negative_and_deduplicates_collapsed_starts():
    notes = [
        {
            "pitch": 60,
            "start_tick": -4,
            "length_ticks": 1,
            "velocity": 100,
        },
        {
            "pitch": 60,
            "start_tick": 1,
            "length_ticks": 1,
            "velocity": 100,
        },
        {
            "pitch": 64,
            "start_tick": 1,
            "length_ticks": 1,
            "velocity": 100,
        },
    ]
    converted = core._converted_notes(notes, 96)
    starts_by_pitch = [(item["pitch"], item["time"]) for item in converted]
    assert all(item["time"] >= 0 for item in converted)
    assert len(starts_by_pitch) == len(set(starts_by_pitch))
    assert len(converted) == 2
    assert all(item["length"] >= 1 for item in converted)


def test_transport_fallback_and_cache(tmp_path: Path):
    cache = tmp_path / "transport.json"
    fake_urllib = object()
    fake_socket = object()

    assert core.choose_transport(str(cache), fake_urllib, fake_socket) == "urllib"
    assert json.loads(cache.read_text())["transport"] == "urllib"
    assert core.choose_transport(str(cache), None, fake_socket) == "socket"
    assert json.loads(cache.read_text())["transport"] == "socket"
    assert core.choose_transport(str(cache), None, None) == "file"
    assert json.loads(cache.read_text())["transport"] == "file"
    assert core.choose_transport(str(cache), fake_urllib, fake_socket) == "file"


def test_pyscript_loads_with_flpianoroll_mock(monkeypatch):
    module = types.ModuleType("flpianoroll")

    class ScriptDialog:
        def __init__(self, title, description):
            self.title = title
            self.description = description
            self.inputs = []

        def AddInputText(self, *args):
            self.inputs.append(args)

        def AddInputCombo(self, *args):
            self.inputs.append(args)

        def AddInputKnobInt(self, *args):
            self.inputs.append(args)

        def AddInputCheckbox(self, *args):
            self.inputs.append(args)

    module.ScriptDialog = ScriptDialog
    module.Note = MockNote
    module.score = MockScore()
    module.Utils = types.SimpleNamespace(ShowMessage=lambda _: None)
    monkeypatch.setitem(sys.modules, "flpianoroll", module)

    script = (
        Path(__file__).resolve().parents[1]
        / "flscript"
        / "MIDI Agent.pyscript"
    )
    values = runpy.run_path(str(script))
    dialog = values["createDialog"]()
    assert dialog.title == "MIDI Agent"
    assert "THIS piano roll/channel" in dialog.description
    assert len(dialog.inputs) == 6

