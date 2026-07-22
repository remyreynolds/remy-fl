import { History as HistoryIcon, Play, RotateCcw, Search } from 'lucide-react'
import { PianoRoll } from './PianoRoll'
import type { HistoryItem } from '../types'

export function HistorySurface({
  history,
  sessionId,
  query,
  onQuery,
  onReplay,
  onBringBack,
}: {
  history: HistoryItem[]
  sessionId: string
  query: string
  onQuery: (value: string) => void
  onReplay: (item: HistoryItem) => void
  onBringBack: (item: HistoryItem) => void
}) {
  const current = history.filter((item) => !item.session_id || item.session_id === sessionId)
  const past = history.filter((item) => item.session_id && item.session_id !== sessionId)
  const pastGroups = past.reduce<Record<string, HistoryItem[]>>((groups, item) => {
    const key = item.session_id!
    groups[key] = [...(groups[key] ?? []), item]
    return groups
  }, {})

  return (
    <section className="secondary-surface history-surface">
      <header className="surface-header">
        <div><span className="eyebrow">Tape machine</span><h1>History</h1></div>
        <label className="search-box">
          <Search size={15} />
          <input value={query} onChange={(event) => onQuery(event.target.value)} placeholder="Find a bassline, prompt, plan…" aria-label="Search history" />
        </label>
      </header>
      {!history.length ? (
        <div className="surface-empty"><HistoryIcon size={24} /><p>Everything you generate lands here. Nothing is ever lost.</p></div>
      ) : (
        <div className="history-list">
          {current.map((item) => <HistoryRow item={item} onReplay={onReplay} onBringBack={onBringBack} key={item.id} />)}
          {Object.entries(pastGroups).map(([pastSession, items]) => (
            <details className="past-sessions" key={pastSession}>
              <summary>{pastSession.replaceAll('-', ' ')} · {items?.length ?? 0} generations</summary>
              {items?.map((item) => <HistoryRow item={item} onReplay={onReplay} onBringBack={onBringBack} key={item.id} />)}
            </details>
          ))}
          {!past.length && <details className="past-sessions"><summary>Past sessions</summary><p>Older projects appear here when this session is closed.</p></details>}
        </div>
      )}
    </section>
  )
}

const COLLAB_LABELS: Record<NonNullable<HistoryItem['generation']['collab_type']>, string> = {
  fix_groove: 'Fix groove',
  reharmonize: 'Reharmonize',
  continue: 'Continue',
  bass_under: 'Bass under',
}

function HistoryRow({ item, onReplay, onBringBack }: { item: HistoryItem; onReplay: (item: HistoryItem) => void; onBringBack: (item: HistoryItem) => void }) {
  const modeLabel = item.generation.collab_type
    ? COLLAB_LABELS[item.generation.collab_type]
    : undefined
  return (
    <article className="history-row">
      <div className="history-time">
        <time>{new Date(item.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}</time>
        <span>{item.element}</span>
        {modeLabel && <span className="mode-badge">{modeLabel}</span>}
      </div>
      <div className="history-summary"><strong>{item.prompt}</strong><p>{item.plan_summary}</p></div>
      <div className="history-thumbnail">
        <PianoRoll
          compact
          generation={item.generation}
          ghostNotes={item.generation.input_notes ?? []}
        />
      </div>
      <span className={`history-badge ${item.kept ? 'is-kept' : item.rejected ? 'is-rejected' : ''}`}>
        {item.kept ? 'kept' : item.rejected ? 'rejected' : 'unrated'}
      </span>
      <div className="history-actions">
        <button type="button" aria-label="Replay" onClick={() => onReplay(item)}><Play size={14} /></button>
        <button type="button" onClick={() => onBringBack(item)}><RotateCcw size={14} /> Bring back</button>
      </div>
    </article>
  )
}

