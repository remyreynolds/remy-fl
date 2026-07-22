from __future__ import annotations

from app.analysis import analyze_notes


def _note(
    pitch: int,
    start: int,
    length: int = 720,
    velocity: int = 96,
) -> dict:
    return {
        "pitch": pitch,
        "start_tick": start,
        "length_ticks": length,
        "velocity": velocity,
    }


def test_known_f_minor_seventh_fixture_detects_key():
    notes = []
    for bar in range(4):
        notes.extend(
            _note(pitch, bar * 3840, 1920, 88 + index * 4)
            for index, pitch in enumerate((53, 56, 60, 63))
        )
    profile = analyze_notes(notes)
    assert profile.detected_key == "F minor"
    assert profile.key_confidence > 0.7
    assert profile.bar_span == [0, 4]
    assert profile.chord_slices[0].guess == "Fm7"
    assert profile.is_monophonic is False


def test_straight_grid_estimates_fifty_percent_swing():
    notes = [_note(60, index * 240, 120) for index in range(16)]
    profile = analyze_notes(notes)
    assert profile.swing_estimate_pct == 50


def test_shuffled_offbeat_sixteenths_estimate_fifty_six_percent():
    notes = [
        _note(60, index * 240 + (40 if index % 2 else 0), 120)
        for index in range(16)
    ]
    profile = analyze_notes(notes)
    assert 55.5 <= profile.swing_estimate_pct <= 56.5

