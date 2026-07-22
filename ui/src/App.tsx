import '@fontsource/dm-sans/400.css'
import '@fontsource/dm-sans/500.css'
import '@fontsource/dm-sans/600.css'
import '@fontsource/ibm-plex-mono/400.css'
import '@fontsource/ibm-plex-mono/500.css'
import '@fontsource/instrument-serif/400.css'
import { Brain, CircleHelp, Disc3, History, Library, Radio, Sparkles } from 'lucide-react'
import { useCallback, useEffect, useState } from 'react'
import type { ReactNode } from 'react'
import { relayApi, RelayError } from './api'
import { BrainSurface } from './components/BrainSurface'
import { GenerateSurface, type GenerationControls } from './components/GenerateSurface'
import { HistorySurface } from './components/HistorySurface'
import { type ToastState, ShortcutOverlay, Toast } from './components/Overlays'
import { ReferencesSurface } from './components/ReferencesSurface'
import { SessionStrip } from './components/SessionStrip'
import { emptySession, mockBrain, mockFingerprints } from './mockData'
import type { BrainState, Fingerprint, Generation, HistoryItem, ReferenceAnalysis, Role, SessionState, SoundType, Surface } from './types'
import './App.css'

function App() {
  const [surface, setSurface] = useState<Surface>('generate')
  const [connected, setConnected] = useState(false)
  const [session, setSession] = useState<SessionState>(structuredClone(emptySession))
  const [selectedRole, setSelectedRole] = useState<Role>('chords')
  const [soundTypes, setSoundTypes] = useState<Partial<Record<Role, SoundType>>>(loadSoundTypes)
  const [prompt, setPrompt] = useState('')
  const [controls, setControls] = useState<GenerationControls>({ swing: 56, density: 0, brightness: 0, threeTakes: false })
  const [takes, setTakes] = useState<Generation[]>([])
  const [activeTake, setActiveTake] = useState(0)
  const [generating, setGenerating] = useState(false)
  const [stage, setStage] = useState('planning')
  const [history, setHistory] = useState<HistoryItem[]>([])
  const [historyQuery, setHistoryQuery] = useState('')
  const [brain, setBrain] = useState<BrainState>(mockBrain)
  const [fingerprints, setFingerprints] = useState<Fingerprint[]>(mockFingerprints)
  const [analysis, setAnalysis] = useState<ReferenceAnalysis>()
  const [analyzing, setAnalyzing] = useState(false)
  const [toast, setToast] = useState<ToastState>()
  const [showShortcuts, setShowShortcuts] = useState(false)

  const showError = useCallback((error: unknown, retry?: () => void) => {
    const message = error instanceof RelayError
      ? error.message
      : 'FL Studio bridge not responding — is the relay running and FL open?'
    setToast({ message, kind: 'error', retry })
  }, [])

  const connect = useCallback(async () => {
    try {
      const health = await relayApi.health()
      setConnected(health.status === 'ok')
      const loaded = await relayApi.session(session.session_id)
      setSession(loaded)
    } catch (error) {
      setConnected(false)
      showError(error, () => void connect())
    }
  }, [session.session_id, showError])

  useEffect(() => { void connect() }, [connect])

  const loadHistory = useCallback(async (query = historyQuery) => {
    try { setHistory(await relayApi.history(session.session_id, query)) }
    catch (error) { showError(error, () => void loadHistory(query)) }
  }, [historyQuery, session.session_id, showError])

  useEffect(() => {
    if (surface === 'history') void loadHistory()
    if (surface === 'brain') relayApi.brain().then(setBrain).catch(showError)
    if (surface === 'references') relayApi.fingerprints().then(setFingerprints).catch(showError)
  }, [loadHistory, showError, surface])

  const buildPrompt = useCallback((base: string, take: number) => {
    const densityWords = ['very sparse', 'sparse', 'balanced', 'busy', 'dense']
    const brightness = controls.brightness < 0 ? `dark ${Math.abs(controls.brightness)}` : controls.brightness > 0 ? `bright ${controls.brightness}` : 'neutral'
    const locked = session.slots.filter((slot) => slot.locked).map((slot) => slot.role)
    return [
      base,
      `${selectedRole}, ${session.bars} bars, ${session.key}, ${session.bpm}bpm`,
      `${controls.swing}% swing, ${densityWords[controls.density + 2]}, ${brightness}`,
      locked.length ? `keep locked context: ${locked.join(', ')}` : '',
      controls.threeTakes ? `variation take ${String.fromCharCode(65 + take)}` : '',
    ].filter(Boolean).join('. ')
  }, [controls, selectedRole, session])

  const runGenerate = useCallback(async (override?: string) => {
    const source = (override ?? prompt).trim()
    if (!source || generating) return
    setGenerating(true)
    setStage('planning')
    const stages = ['planning', `${selectedRole} · generating`, 'critic · listening']
    let stageIndex = 0
    const timer = window.setInterval(() => {
      stageIndex = Math.min(stageIndex + 1, stages.length - 1)
      setStage(stages[stageIndex])
    }, 550)
    try {
      const count = controls.threeTakes ? 3 : 1
      const generated = await Promise.all(Array.from({ length: count }, (_, take) => relayApi.generate({
        prompt: buildPrompt(source, take),
        element: selectedRole[0].toUpperCase() + selectedRole.slice(1),
        bars: session.bars,
        session_id: session.session_id,
        take,
        sound_type: soundTypes[selectedRole],
      })))
      setStage('critic ✓')
      setTakes(generated)
      setActiveTake(0)
      setPrompt(source)
      void loadHistory('')
    } catch (error) {
      showError(error, () => void runGenerate(source))
    } finally {
      window.clearInterval(timer)
      window.setTimeout(() => setGenerating(false), 260)
    }
  }, [buildPrompt, controls.threeTakes, generating, loadHistory, prompt, selectedRole, session.bars, session.session_id, showError, soundTypes])

  const keepActive = useCallback(() => {
    const generation = takes[activeTake]
    if (!generation) return
    const next = {
      ...session,
      slots: session.slots.map((slot) => (
        slot.role === selectedRole ? { ...slot, locked: true, generation } : slot
      )),
    }
    setSession(next)
    void relayApi.saveSession(next)
    setToast({ message: `${selectedRole} kept and locked into this session.`, kind: 'success' })
  }, [activeTake, selectedRole, session, takes])

  const sendGeneration = useCallback(async (generation?: Generation, roles?: string[]) => {
    const target = generation ?? takes[activeTake]
    if (!target) return
    try {
      const result = await relayApi.queueForFl(target, roles)
      setToast({
        message: `${result.queued} element${result.queued === 1 ? '' : 's'} queued — run MIDI Agent Apply in the target FL piano roll.`,
        kind: 'success',
      })
    } catch (error) {
      showError(error, () => void sendGeneration(target, roles))
    }
  }, [activeTake, showError, takes])

  const sendAll = useCallback(() => {
    const ready = session.slots.filter((slot) => slot.generation && !slot.locked)
    if (!ready.length) return
    const base = ready[0].generation!
    void sendGeneration(
      { ...base, tracks: ready.flatMap((slot) => slot.generation!.tracks) },
      ready.map((slot) => slot.role),
    )
  }, [sendGeneration, session.slots])

  const handleHistoryQuery = (value: string) => {
    setHistoryQuery(value)
    void loadHistory(value)
  }

  useEffect(() => {
    const handle = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement
      const typing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName)
      if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
        event.preventDefault()
        void runGenerate()
      } else if (!typing && event.code === 'Space' && surface === 'generate') {
        event.preventDefault()
        window.dispatchEvent(new Event('midi-preview-toggle'))
      } else if (!typing && ['1', '2', '3'].includes(event.key) && surface === 'generate') {
        const index = Number(event.key) - 1
        if (index < takes.length) setActiveTake(index)
      } else if (!typing && event.key === '?') {
        setShowShortcuts(true)
      } else if (event.key === 'Escape') {
        setShowShortcuts(false)
      }
    }
    window.addEventListener('keydown', handle)
    return () => window.removeEventListener('keydown', handle)
  }, [runGenerate, surface, takes.length])

  return (
    <div className="studio-app">
      <div className="small-screen-note">
        <Disc3 size={28} />
        <h1>Open on your studio machine</h1>
        <p>MIDI Agent is designed for a desktop workspace at least 1100px wide.</p>
      </div>
      <header className="app-header">
        <button className="brand" type="button" onClick={() => setSurface('generate')}><Radio size={17} /><span>MIDI</span><em>AGENT</em></button>
        <nav aria-label="Main surfaces">
          <NavButton surface="generate" current={surface} onClick={setSurface} icon={<Sparkles size={15} />} label="Generate" />
          <NavButton surface="history" current={surface} onClick={setSurface} icon={<History size={15} />} label="History" />
          <NavButton surface="brain" current={surface} onClick={setSurface} icon={<Brain size={15} />} label="Brain" />
          <NavButton surface="references" current={surface} onClick={setSurface} icon={<Library size={15} />} label="References" />
        </nav>
        <div className="header-status">
          <span className={connected ? 'status-dot is-connected' : 'status-dot'} />
          relay {connected ? 'connected' : 'offline'}
          <button type="button" onClick={() => setShowShortcuts(true)} aria-label="Keyboard shortcuts"><CircleHelp size={15} /></button>
        </div>
      </header>

      <div className="app-body">
        <SessionStrip
          session={session}
          onChange={(next) => { setSession(next); void relayApi.saveSession(next) }}
          onToggleLock={(role) => {
            const next = { ...session, slots: session.slots.map((slot) => slot.role === role ? { ...slot, locked: !slot.locked } : slot) }
            setSession(next)
            void relayApi.saveSession(next)
          }}
          onGenerateRole={(role) => { setSelectedRole(role); setSurface('generate') }}
          onSendAll={sendAll}
        />
        <main>
          {surface === 'generate' && (
            <GenerateSurface
              prompt={prompt}
              onPromptChange={setPrompt}
              role={selectedRole}
              soundType={soundTypes[selectedRole]}
              onSoundTypeChange={(soundType) => {
                setSoundTypes((current) => {
                  const next = { ...current, [selectedRole]: soundType }
                  if (!soundType) delete next[selectedRole]
                  sessionStorage.setItem('midi-agent.sound-types', JSON.stringify(next))
                  return next
                })
              }}
              controls={controls}
              onControlsChange={setControls}
              takes={takes}
              activeTake={activeTake}
              onTakeChange={setActiveTake}
              generating={generating}
              stage={stage}
              onGenerate={() => void runGenerate()}
              onKeep={keepActive}
              onSend={() => void sendGeneration()}
              onFeedback={(rating) => {
                void relayApi.feedback(session.session_id, rating).then(() => setToast({ message: rating > 0 ? 'Kept signal learned.' : 'Rejection noted for the next plan.', kind: 'success' })).catch(showError)
              }}
              onTweak={(value) => void runGenerate(value)}
            />
          )}
          {surface === 'history' && (
            <HistorySurface
              history={history}
              sessionId={session.session_id}
              query={historyQuery}
              onQuery={handleHistoryQuery}
              onReplay={(item) => { setTakes([item.generation]); setActiveTake(0); setSurface('generate'); window.setTimeout(() => window.dispatchEvent(new Event('midi-preview-toggle')), 0) }}
              onBringBack={(item) => {
                setSession((current) => ({ ...current, slots: current.slots.map((slot) => slot.role === item.element ? { ...slot, generation: item.generation, locked: true } : slot) }))
                setToast({ message: `${item.element} restored and locked.`, kind: 'success' })
              }}
            />
          )}
          {surface === 'brain' && (
            <BrainSurface
              state={brain}
              onDeleteTaste={(id) => {
                setBrain((current) => ({ ...current, taste: current.taste.filter((belief) => belief.id !== id) }))
                void relayApi.deleteTaste(id).catch(showError)
              }}
              onToggleHeuristic={(id, active) => {
                setBrain((current) => ({ ...current, heuristics: current.heuristics.map((item) => item.id === id ? { ...item, active } : item) }))
                void relayApi.toggleHeuristic(id, active).catch(showError)
              }}
            />
          )}
          {surface === 'references' && (
            <ReferencesSurface
              fingerprints={fingerprints}
              analysis={analysis}
              analyzing={analyzing}
              onAnalyze={(reference, file) => {
                setAnalyzing(true)
                relayApi.analyzeReference(reference, file).then(setAnalysis).catch(showError).finally(() => setAnalyzing(false))
              }}
              onSave={() => {
                if (!analysis) return
                relayApi.saveReference(analysis.fingerprint).then(() => relayApi.fingerprints()).then(setFingerprints).catch(showError)
                setAnalysis(undefined)
              }}
              onDiscard={() => setAnalysis(undefined)}
              onDelete={(id) => {
                setFingerprints((current) => current.filter((item) => item.id !== id))
                void relayApi.deleteReference(id).catch(showError)
              }}
            />
          )}
        </main>
      </div>
      {toast && <Toast toast={toast} onClose={() => setToast(undefined)} />}
      {showShortcuts && <ShortcutOverlay onClose={() => setShowShortcuts(false)} />}
    </div>
  )
}

function NavButton({ surface, current, onClick, icon, label }: { surface: Surface; current: Surface; onClick: (surface: Surface) => void; icon: ReactNode; label: string }) {
  return <button type="button" className={surface === current ? 'is-active' : ''} onClick={() => onClick(surface)}>{icon}{label}</button>
}

function loadSoundTypes(): Partial<Record<Role, SoundType>> {
  try {
    return JSON.parse(sessionStorage.getItem('midi-agent.sound-types') ?? '{}')
  } catch {
    return {}
  }
}

export default App
