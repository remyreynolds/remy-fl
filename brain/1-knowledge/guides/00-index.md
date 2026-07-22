# Music guides index (shared brain corpus)

Generator (Python brain) and chatbot (plugin KnowledgeBase) **pull from this
same folder** before writing MIDI. The chatbot may also use Claude’s trained
knowledge on top when local docs don’t cover a gap.

```
guides/
├── *.md                 curated cheat sheets (token-efficient)
├── full/*.md            FULL text extracted from your PDFs
├── pdfs/*.pdf           original PDF binaries
└── 00-index.md          this file
```

## Full PDFs included
- `pdfs/EDM-Chord-Progressions-Cheat-Sheet.pdf`
- `pdfs/5-Steps-Melody.pdf`
- `pdfs/How-to-Make-Electronic-Music.pdf`
- `pdfs/Music-Theory-TLDR.pdf`
- `pdfs/Music-Production-Software-Guide.pdf`

Matching searchable text lives under `full/` so retrieval works without a PDF
parser at runtime.

## How retrieval works
1. Context Assembler / KnowledgeBase score docs by element + prompt keywords
2. Top excerpts are injected into the planner/generator or chat/MIDI prompt
3. Chatbot: local excerpts first, then Claude knowledge allowed
4. Generator: same local excerpts (plan.theory_guide_excerpts)
