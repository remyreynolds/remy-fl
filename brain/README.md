# 🧠 THE BRAIN — START HERE

Three folders. Three jobs. Nothing else.

```
brain/
├── 1-knowledge/   ← WHAT IT KNOWS   (you edit these files — this is where taste comes from)
├── 2-prompts/     ← WHAT IT'S TOLD  (the system prompts — edit rarely, carefully)
└── 3-memory/      ← WHAT IT LEARNED (auto-generated — never hand-edit)
```

## The one rule
**If you want the brain to make better music, you work in `1-knowledge/`.**
That's it. Each folder has its own README explaining exactly what goes in it
and how to add things.

## Quick answers

| I want to… | Go to |
|---|---|
| Add a reference track so generations sound like it | `1-knowledge/` → `fingerprints.json` |
| Add theory / chord / melody cheat sheets | `1-knowledge/guides/` |
| Change what a subgenre sounds like by default | `1-knowledge/` → `genre_cards.json` |
| Map a mood word ("dark", "dreamy") to musical settings | `1-knowledge/` → `mood_table.json` |
| Change how the AI is instructed | `2-prompts/` |
| See what the brain has learned about my taste | `3-memory/` → `brain_report.md` |
| Reset everything it learned | `python -m app.cli --reset-taste default` |

The pipeline code itself lives in `app/` (seven stages: perception → context →
planner → generator → critic → finalizer → memory). You should almost never
need to touch it to change musical results.
