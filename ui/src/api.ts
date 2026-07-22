import { mockBrain, mockFingerprints, mockGeneration, mockHistory, mockSession } from './mockData'
import type { BrainState, Fingerprint, Generation, HistoryItem, ReferenceAnalysis, SessionState } from './types'

const mockMode = new URLSearchParams(window.location.search).has('mock')

export class RelayError extends Error {
  status: number

  constructor(message: string, status = 0) {
    super(message)
    this.name = 'RelayError'
    this.status = status
  }
}

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  let response: Response
  try {
    response = await fetch(path, {
      ...options,
      headers: { 'Content-Type': 'application/json', ...options?.headers },
    })
  } catch {
    throw new RelayError('FL Studio bridge not responding — is the relay running and FL open?')
  }
  const body = await response.json().catch(() => ({}))
  if (!response.ok) {
    throw new RelayError(body.error ?? body.detail ?? 'Generation failed', response.status)
  }
  return body as T
}

function delay<T>(value: T, milliseconds = 220): Promise<T> {
  return new Promise((resolve) => window.setTimeout(() => resolve(value), milliseconds))
}

export const relayApi = {
  async health(): Promise<{ status: string; backend_reachable: boolean }> {
    if (mockMode) return delay({ status: 'ok', backend_reachable: true }, 80)
    return request('/health')
  },

  async generate(input: {
    prompt: string
    element: string
    bars: number
    session_id: string
    take?: number
    sound_type?: string
  }): Promise<Generation> {
    if (mockMode) {
      return delay({
        ...mockGeneration,
        generation_id: 100 + (input.take ?? 0),
        description: `${input.bars}-bar ${input.element.toLowerCase()} take`,
      }, 700)
    }
    return request('/companion/generate', {
      method: 'POST',
      body: JSON.stringify(input),
    })
  },

  async feedback(sessionId: string, rating: -1 | 1, text = ''): Promise<void> {
    if (mockMode) return delay(undefined, 100)
    await request('/feedback', {
      method: 'POST',
      body: JSON.stringify({ session_id: sessionId, rating, text }),
    })
  },

  async queueForFl(generation: Generation, roles?: string[]): Promise<{ queued: number }> {
    if (mockMode) return delay({ queued: roles?.length ?? generation.tracks.length }, 120)
    return request('/companion/send-to-fl', {
      method: 'POST',
      body: JSON.stringify({ generation, roles }),
    })
  },

  async session(sessionId: string): Promise<SessionState> {
    if (mockMode) return delay(structuredClone(mockSession), 120)
    return request(`/companion/session/${encodeURIComponent(sessionId)}`)
  },

  async saveSession(session: SessionState): Promise<void> {
    if (mockMode) return delay(undefined, 80)
    await request(`/companion/session/${encodeURIComponent(session.session_id)}`, {
      method: 'PUT',
      body: JSON.stringify(session),
    })
  },

  async history(sessionId: string, query = ''): Promise<HistoryItem[]> {
    if (mockMode) {
      const low = query.toLowerCase()
      return delay(mockHistory.filter((item) => (
        !low || `${item.prompt} ${item.plan_summary}`.toLowerCase().includes(low)
      )), 120)
    }
    return request(`/companion/history?session_id=${encodeURIComponent(sessionId)}&q=${encodeURIComponent(query)}&include_past=true`)
  },

  async brain(): Promise<BrainState> {
    if (mockMode) return delay(structuredClone(mockBrain), 120)
    return request('/companion/brain')
  },

  async deleteTaste(id: string): Promise<void> {
    if (mockMode) return delay(undefined, 80)
    await request(`/companion/brain/taste/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },

  async toggleHeuristic(id: number, active: boolean): Promise<void> {
    if (mockMode) return delay(undefined, 80)
    await request(`/companion/brain/heuristics/${id}`, {
      method: 'PATCH',
      body: JSON.stringify({ active }),
    })
  },

  async fingerprints(): Promise<Fingerprint[]> {
    if (mockMode) return delay(structuredClone(mockFingerprints), 120)
    return request('/companion/references')
  },

  async analyzeReference(reference: string, file?: File): Promise<ReferenceAnalysis> {
    if (mockMode) {
      return delay({
        fingerprint: {
          track: reference || file?.name || 'Dropped MIDI',
          subgenre: 'deep_house',
          bpm: 123,
          key: 'F minor',
          progression: ['Fm9', 'D♭maj7'],
          groove: 'late offbeat stabs with restrained bass movement',
          confidence: 0.83,
        },
      }, 500)
    }
    if (file) {
      const form = new FormData()
      form.append('file', file)
      const response = await fetch('/companion/references/analyze-midi', { method: 'POST', body: form })
      if (!response.ok) throw new RelayError('Reference analysis failed', response.status)
      return response.json()
    }
    return request('/companion/references/analyze', {
      method: 'POST',
      body: JSON.stringify({ reference }),
    })
  },

  async saveReference(fingerprint: ReferenceAnalysis['fingerprint']): Promise<void> {
    if (mockMode) return delay(undefined, 80)
    await request('/companion/references', {
      method: 'POST',
      body: JSON.stringify(fingerprint),
    })
  },

  async deleteReference(id: number): Promise<void> {
    if (mockMode) return delay(undefined, 80)
    await request(`/companion/references/${id}`, { method: 'DELETE' })
  },
}

