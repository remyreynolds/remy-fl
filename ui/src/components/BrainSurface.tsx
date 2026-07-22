import { Brain, Lightbulb, X } from 'lucide-react'
import ReactMarkdown from 'react-markdown'
import type { BrainState } from '../types'

export function BrainSurface({
  state,
  onDeleteTaste,
  onToggleHeuristic,
}: {
  state: BrainState
  onDeleteTaste: (id: string) => void
  onToggleHeuristic: (id: number, active: boolean) => void
}) {
  return (
    <section className="secondary-surface brain-surface">
      <header className="surface-header">
        <div><span className="eyebrow">Learning window</span><h1>Brain</h1></div>
        <p>Visible beliefs. Correctable rules. No silent personalization.</p>
      </header>
      <div className="brain-grid">
        <article className="brain-card taste-card">
          <header><Brain size={17} /><div><h2>Taste profile</h2><p>Click × to remove a wrong belief.</p></div></header>
          <div className="belief-chips">
            {state.taste.map((belief) => (
              <span className="belief-chip" style={{ opacity: 0.4 + belief.confidence * 0.6 }} key={belief.id}>
                {belief.label}
                <button type="button" onClick={() => onDeleteTaste(belief.id)} aria-label={`Delete ${belief.label}`}><X size={12} /></button>
              </span>
            ))}
          </div>
        </article>

        <article className="brain-card heuristic-card">
          <header><Lightbulb size={17} /><div><h2>Learned rules</h2><p>Highest-evidence rules are planned first.</p></div></header>
          <div className="heuristic-list">
            {state.heuristics.map((heuristic) => (
              <label key={heuristic.id}>
                <span>
                  <small>{heuristic.trigger}</small>
                  <strong>{heuristic.rule}</strong>
                  <em>learned from {heuristic.evidence} keeps</em>
                </span>
                <input
                  type="checkbox"
                  checked={heuristic.active}
                  onChange={(event) => onToggleHeuristic(heuristic.id, event.target.checked)}
                  aria-label={`Toggle ${heuristic.rule}`}
                />
              </label>
            ))}
          </div>
        </article>

        <article className="brain-card report-card">
          <header><span className="report-glyph">W</span><div><h2>Weekly report</h2><p>What the brain currently believes.</p></div></header>
          <div className="report-markdown"><ReactMarkdown>{state.report}</ReactMarkdown></div>
        </article>
      </div>
    </section>
  )
}

