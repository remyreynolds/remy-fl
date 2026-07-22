import { Lock, Unlock, Upload, WandSparkles } from 'lucide-react'
import { PianoRoll } from './PianoRoll'
import type { Role, SessionState } from '../types'

const KEYS = ['C minor', 'C# minor', 'D minor', 'E♭ minor', 'E minor', 'F minor', 'F# minor', 'G minor', 'A♭ minor', 'A minor', 'B♭ minor', 'B minor', 'C major', 'F major', 'G major']

export function SessionStrip({
  session,
  onChange,
  onToggleLock,
  onGenerateRole,
  onSendAll,
}: {
  session: SessionState
  onChange: (next: SessionState) => void
  onToggleLock: (role: Role) => void
  onGenerateRole: (role: Role) => void
  onSendAll: () => void
}) {
  const queued = session.slots.filter((slot) => slot.generation && !slot.locked).length

  return (
    <aside className="session-strip" aria-label="Session strip">
      <div className="session-heading">
        <span className="eyebrow">Session</span>
        <span className="session-id">{session.session_id.replaceAll('-', ' ')}</span>
      </div>

      <div className="session-values">
        <label>
          <span>Key</span>
          <select
            aria-label="Session key"
            value={session.key}
            onChange={(event) => onChange({ ...session, key: event.target.value })}
          >
            {KEYS.map((key) => <option key={key}>{key}</option>)}
          </select>
        </label>
        <label>
          <span>BPM</span>
          <input
            aria-label="Session BPM"
            type="number"
            min={80}
            max={160}
            value={session.bpm}
            onChange={(event) => onChange({ ...session, bpm: Number(event.target.value) })}
          />
        </label>
        <label>
          <span>Bars</span>
          <select
            aria-label="Session bars"
            value={session.bars}
            onChange={(event) => onChange({ ...session, bars: Number(event.target.value) })}
          >
            {[2, 4, 8, 16].map((bars) => <option key={bars}>{bars}</option>)}
          </select>
        </label>
      </div>

      <div className="slot-list">
        {session.slots.map((slot) => (
          <div
            className={`session-slot ${slot.generation ? 'is-filled' : 'is-empty'} ${slot.locked ? 'is-locked' : ''}`}
            key={slot.role}
            data-testid={`slot-${slot.role}`}
          >
            <button
              className="slot-main"
              type="button"
              onClick={() => !slot.generation && onGenerateRole(slot.role)}
              aria-label={slot.generation ? `${slot.label} filled` : `Generate ${slot.label}`}
            >
              <span className="slot-label">{slot.label}</span>
              {slot.generation ? (
                <PianoRoll generation={slot.generation} compact />
              ) : (
                <span className="slot-empty"><WandSparkles size={12} /> generate</span>
              )}
            </button>
            <button
              className="lock-button"
              type="button"
              disabled={!slot.generation}
              onClick={() => onToggleLock(slot.role)}
              aria-label={`${slot.locked ? 'Unlock' : 'Lock'} ${slot.label}`}
              aria-pressed={slot.locked}
            >
              {slot.locked ? <Lock size={13} /> : <Unlock size={13} />}
            </button>
          </div>
        ))}
      </div>

      <button
        className="send-all"
        type="button"
        disabled={queued === 0}
        onClick={onSendAll}
      >
        <Upload size={15} />
        Send all to FL
        {queued > 0 && <span>{queued}</span>}
      </button>
    </aside>
  )
}

