import { FileMusic, Save, Search, Trash2, X } from 'lucide-react'
import { useRef, useState } from 'react'
import type { Fingerprint, ReferenceAnalysis } from '../types'

export function ReferencesSurface({
  fingerprints,
  analysis,
  analyzing,
  onAnalyze,
  onSave,
  onDiscard,
  onDelete,
}: {
  fingerprints: Fingerprint[]
  analysis?: ReferenceAnalysis
  analyzing: boolean
  onAnalyze: (reference: string, file?: File) => void
  onSave: () => void
  onDiscard: () => void
  onDelete: (id: number) => void
}) {
  const [reference, setReference] = useState('')
  const [sort, setSort] = useState<'subgenre' | 'kept_rate'>('kept_rate')
  const inputRef = useRef<HTMLInputElement>(null)
  const sorted = [...fingerprints].sort((left, right) => (
    sort === 'kept_rate'
      ? right.kept_rate - left.kept_rate
      : left.subgenre.localeCompare(right.subgenre)
  ))

  return (
    <section className="secondary-surface references-surface">
      <header className="surface-header">
        <div><span className="eyebrow">Feed the brain</span><h1>References</h1></div>
        <p>Extract style, never copy riffs.</p>
      </header>

      <div
        className="reference-input"
        onDragOver={(event) => event.preventDefault()}
        onDrop={(event) => {
          event.preventDefault()
          const file = event.dataTransfer.files[0]
          if (file) onAnalyze('', file)
        }}
      >
        <Search size={17} />
        <input value={reference} onChange={(event) => setReference(event.target.value)} placeholder="artist – track" aria-label="Reference track" />
        <button type="button" disabled={analyzing || !reference.trim()} onClick={() => onAnalyze(reference)}>
          {analyzing ? 'Analyzing…' : 'Analyze'}
        </button>
        <span>or</span>
        <button type="button" className="drop-midi" onClick={() => inputRef.current?.click()}><FileMusic size={14} /> drop .mid</button>
        <input ref={inputRef} hidden type="file" accept=".mid,.midi" onChange={(event) => {
          const file = event.target.files?.[0]
          if (file) onAnalyze('', file)
        }} />
      </div>

      {analysis && (
        <article className="analysis-card">
          <button type="button" className="analysis-close" onClick={onDiscard} aria-label="Discard analysis"><X size={14} /></button>
          <div><span className="eyebrow">Extracted fingerprint</span><h2>{analysis.fingerprint.track}</h2></div>
          <dl>
            <div><dt>Key</dt><dd>{analysis.fingerprint.key}</dd></div>
            <div><dt>BPM</dt><dd>{analysis.fingerprint.bpm}</dd></div>
            <div><dt>Style</dt><dd>{analysis.fingerprint.subgenre.replace('_', ' ')}</dd></div>
            <div><dt>Confidence</dt><dd>{Math.round((analysis.fingerprint.confidence ?? 0) * 100)}%</dd></div>
          </dl>
          <p><strong>{analysis.fingerprint.progression.join(' → ')}</strong> · {analysis.fingerprint.groove}</p>
          <div><button className="write-action" type="button" onClick={onSave}><Save size={14} /> Save to library</button><button type="button" onClick={onDiscard}>Discard</button></div>
        </article>
      )}

      <div className="library-header">
        <div><span className="eyebrow">Semantic memory</span><h2>Fingerprint library</h2></div>
        <label>Sort
          <select value={sort} onChange={(event) => setSort(event.target.value as typeof sort)}>
            <option value="kept_rate">Kept-rate</option>
            <option value="subgenre">Subgenre</option>
          </select>
        </label>
      </div>
      <div className="fingerprint-table">
        <div className="fingerprint-row table-labels"><span>Reference</span><span>Style</span><span>Key / BPM</span><span>Groove</span><span>Kept</span><span /></div>
        {sorted.map((fingerprint) => (
          <div className="fingerprint-row" key={fingerprint.id}>
            <strong>{fingerprint.track}</strong>
            <span>{fingerprint.subgenre.replace('_', ' ')}</span>
            <code>{fingerprint.key} · {fingerprint.bpm}</code>
            <span>{fingerprint.groove}</span>
            <code>{Math.round(fingerprint.kept_rate * 100)}%</code>
            <button type="button" onClick={() => onDelete(fingerprint.id)} aria-label={`Delete ${fingerprint.track}`}><Trash2 size={13} /></button>
          </div>
        ))}
      </div>
    </section>
  )
}

