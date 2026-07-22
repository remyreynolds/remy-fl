import { Check, ChevronRight, Pause, Play, Send, SlidersHorizontal, Sparkles, ThumbsDown, ThumbsUp } from 'lucide-react'
import { useEffect, useState } from 'react'
import { usePreviewAudio } from '../audio'
import { PianoRoll } from './PianoRoll'
import type { Generation, Role, SoundType } from '../types'

export interface GenerationControls {
  swing: number
  density: number
  brightness: number
  threeTakes: boolean
}

const SUBGENRES = [
  ['Deep', 'deep house'],
  ['Tech', 'tech house'],
  ['Classic', 'classic house'],
  ['French', 'French house'],
  ['Melodic', 'progressive melodic house'],
  ['Afro', 'afro house'],
  ['Garage', 'UK garage'],
  ['Piano', 'piano house'],
] as const
const SOUND_TYPES: Array<[string, SoundType]> = [
  ['Pluck', 'pluck'],
  ['Pad', 'pad'],
  ['Keys', 'keys'],
  ['Stab', 'stab'],
  ['Sub', 'sub'],
  ['Lead', 'lead'],
]

const COLLAB_LABELS: Record<NonNullable<Generation['collab_type']>, string> = {
  fix_groove: 'Fix groove',
  reharmonize: 'Reharmonize',
  continue: 'Continue',
  bass_under: 'Bass under',
}

export function GenerateSurface({
  prompt,
  onPromptChange,
  role,
  soundType,
  onSoundTypeChange,
  controls,
  onControlsChange,
  takes,
  activeTake,
  onTakeChange,
  generating,
  stage,
  onGenerate,
  onKeep,
  onSend,
  onFeedback,
  onTweak,
}: {
  prompt: string
  onPromptChange: (value: string) => void
  role: Role
  soundType?: SoundType
  onSoundTypeChange: (value?: SoundType) => void
  controls: GenerationControls
  onControlsChange: (value: GenerationControls) => void
  takes: Generation[]
  activeTake: number
  onTakeChange: (index: number) => void
  generating: boolean
  stage: string
  onGenerate: () => void
  onKeep: () => void
  onSend: () => void
  onFeedback: (rating: -1 | 1) => void
  onTweak: (value: string) => void
}) {
  const [tweaking, setTweaking] = useState(false)
  const [tweak, setTweak] = useState('')
  const [showInputGhost, setShowInputGhost] = useState(true)
  const generation = takes[activeTake]
  const audio = usePreviewAudio(generation)
  const toggleAudio = audio.toggle
  const hasInputNotes = Boolean(generation?.input_notes?.length)
  const collabLabel = generation?.collab_type
    ? COLLAB_LABELS[generation.collab_type]
    : undefined

  useEffect(() => {
    const toggle = () => toggleAudio()
    window.addEventListener('midi-preview-toggle', toggle)
    return () => window.removeEventListener('midi-preview-toggle', toggle)
  }, [toggleAudio])

  const prependSubgenre = (subgenre: string) => {
    const clean = prompt.trim()
    onPromptChange(clean.toLowerCase().startsWith(subgenre.toLowerCase()) ? clean : `${subgenre} ${clean}`.trim())
  }

  return (
    <section className="generate-surface">
      <div className="generation-zone">
        <div className="surface-kicker">New {role}</div>
        <h1>What do you want to hear next?</h1>
        <textarea
          className="prompt-box"
          value={prompt}
          onChange={(event) => onPromptChange(event.target.value)}
          placeholder="Describe a loop — try: dark tech house bass, 8 bars, F minor"
          aria-label="Generation prompt"
          rows={4}
        />
        <div className="sound-type-chips" aria-label="Sound type">
          <span>Sound</span>
          {SOUND_TYPES.map(([label, value]) => (
            <button
              type="button"
              key={value}
              aria-pressed={soundType === value}
              className={soundType === value ? 'is-active' : ''}
              onClick={() => onSoundTypeChange(soundType === value ? undefined : value)}
            >
              {label}
            </button>
          ))}
        </div>
        <div className="subgenre-chips" aria-label="Subgenre shortcuts">
          {SUBGENRES.map(([label, value]) => (
            <button type="button" key={label} onClick={() => prependSubgenre(value)}>{label}</button>
          ))}
        </div>

        <div className="macro-controls">
          <MacroSlider
            label="Swing"
            value={controls.swing}
            min={50}
            max={66}
            suffix="%"
            onChange={(swing) => onControlsChange({ ...controls, swing })}
          />
          <MacroSlider
            label="Density"
            value={controls.density}
            min={-2}
            max={2}
            display={['Sparse', 'Lean', 'Balanced', 'Busy', 'Dense'][controls.density + 2]}
            onChange={(density) => onControlsChange({ ...controls, density })}
          />
          <MacroSlider
            label="Dark ↔ Bright"
            value={controls.brightness}
            min={-2}
            max={2}
            display={controls.brightness === 0 ? 'Neutral' : controls.brightness < 0 ? `Dark ${Math.abs(controls.brightness)}` : `Bright ${controls.brightness}`}
            onChange={(brightness) => onControlsChange({ ...controls, brightness })}
          />
        </div>

        <div className="generate-row">
          <button
            className="write-action generate-button"
            type="button"
            onClick={onGenerate}
            disabled={generating || !prompt.trim()}
          >
            {generating ? (
              <><span className="progress-pulse" /> {stage}</>
            ) : (
              <><Sparkles size={17} /> Generate <kbd>⌘↵</kbd></>
            )}
          </button>
          <label className="takes-toggle">
            <input
              type="checkbox"
              checked={controls.threeTakes}
              onChange={(event) => onControlsChange({ ...controls, threeTakes: event.target.checked })}
            />
            <span>×3 takes</span>
          </label>
        </div>

        {generation?.plan_summary && (
          <p className="plan-line"><Check size={13} /> {generation.plan_summary}{soundType && !generation.plan_summary.includes(soundType) ? ` · ${soundType} gating` : ''}</p>
        )}
      </div>

      <div className="output-zone">
        {!generation ? (
          <div className="output-empty">
            <div className="empty-rings"><span /><span /><span /></div>
            <p>Describe a loop — try: <em>dark tech house bass, 8 bars, F minor</em></p>
          </div>
        ) : (
          <>
            <div className="preview-header">
              <div>
                <span className="eyebrow">Newest take{collabLabel ? ` · ${collabLabel}` : ''}</span>
                <strong>{generation.tracks.map((track) => track.name).join(' + ')}</strong>
              </div>
              <div className="preview-header-actions">
                {hasInputNotes && (
                  <label className="ghost-toggle" title="Ghost the collaborator input notes under the result">
                    <input
                      type="checkbox"
                      checked={showInputGhost}
                      onChange={(event) => setShowInputGhost(event.target.checked)}
                      aria-label="Show input notes under output"
                    />
                    <span>input vs output</span>
                  </label>
                )}
                <button type="button" className="play-button" onClick={audio.toggle} aria-label={audio.playing ? 'Stop preview' : 'Play preview'}>
                  {audio.playing ? <Pause size={15} /> : <Play size={15} fill="currentColor" />}
                  {audio.playing ? 'Stop' : 'Preview'}
                  <kbd>Space</kbd>
                </button>
              </div>
            </div>

            {takes.length > 1 && (
              <div className="take-tabs" role="tablist" aria-label="Generation takes">
                {takes.map((take, index) => (
                  <button
                    type="button"
                    role="tab"
                    aria-selected={activeTake === index}
                    className={activeTake === index ? 'is-active' : ''}
                    onClick={() => onTakeChange(index)}
                    key={take.generation_id ?? index}
                  >
                    {String.fromCharCode(65 + index)}
                    <kbd>{index + 1}</kbd>
                  </button>
                ))}
              </div>
            )}

            <div className="preview-shell">
              <PianoRoll
                generation={generation}
                playhead={audio.playhead}
                ghostNotes={showInputGhost ? generation.input_notes ?? [] : []}
              />
              <div className="preview-meta">
                <span>{generation.meta.key}</span>
                <span>{generation.meta.bpm} BPM</span>
                <span>{generation.meta.loop_bars} bars</span>
                <span>{generation.meta.swing_pct}% swing</span>
                {collabLabel && <span className="mode-badge">{collabLabel}</span>}
              </div>
            </div>

            {generation.warnings && generation.warnings.length > 0 && (
              <div className="warning-strip">
                shipped with warnings: {generation.warnings.join(' · ')}
              </div>
            )}

            <div className="output-actions">
              <button className="write-action" type="button" onClick={onSend}><Send size={15} /> Send to FL</button>
              <button type="button" onClick={onKeep}><Check size={15} /> Keep</button>
              <button type="button" className="icon-action" onClick={() => onFeedback(1)} aria-label="Thumbs up"><ThumbsUp size={15} /></button>
              <button type="button" className="icon-action" onClick={() => onFeedback(-1)} aria-label="Thumbs down"><ThumbsDown size={15} /></button>
              <button type="button" onClick={() => setTweaking((value) => !value)}><SlidersHorizontal size={15} /> Tweak…</button>
            </div>

            {tweaking && (
              <form
                className="tweak-box"
                onSubmit={(event) => {
                  event.preventDefault()
                  if (!tweak.trim()) return
                  onTweak(tweak)
                  setTweak('')
                  setTweaking(false)
                }}
              >
                <input autoFocus value={tweak} onChange={(event) => setTweak(event.target.value)} placeholder="bar 3 busier" aria-label="Tweak prompt" />
                <button type="submit" aria-label="Apply tweak"><ChevronRight size={16} /></button>
              </form>
            )}
          </>
        )}
      </div>
    </section>
  )
}

function MacroSlider({
  label,
  value,
  min,
  max,
  suffix = '',
  display,
  onChange,
}: {
  label: string
  value: number
  min: number
  max: number
  suffix?: string
  display?: string
  onChange: (value: number) => void
}) {
  return (
    <label className="macro-slider">
      <span>{label}<span className="macro-value">{display ?? `${value}${suffix}`}</span></span>
      <input type="range" min={min} max={max} value={value} onChange={(event) => onChange(Number(event.target.value))} />
    </label>
  )
}

