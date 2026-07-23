import { readFileSync } from 'node:fs'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mockBrain, mockFingerprints, mockGeneration, mockHistory, mockSession } from './mockData'

const apiMocks = vi.hoisted(() => ({
  health: vi.fn(),
  session: vi.fn(),
  generate: vi.fn(),
  feedback: vi.fn(),
  queueForFl: vi.fn(),
  saveSession: vi.fn(),
  history: vi.fn(),
  brain: vi.fn(),
  deleteTaste: vi.fn(),
  toggleHeuristic: vi.fn(),
  fingerprints: vi.fn(),
  analyzeReference: vi.fn(),
  saveReference: vi.fn(),
  deleteReference: vi.fn(),
}))

vi.mock('./api', () => ({
  relayApi: apiMocks,
  RelayError: class RelayError extends Error {},
}))

import App from './App'

beforeEach(() => {
  vi.clearAllMocks()
  sessionStorage.clear()
  apiMocks.health.mockResolvedValue({ status: 'ok', backend_reachable: true })
  apiMocks.session.mockResolvedValue(structuredClone(mockSession))
  apiMocks.generate.mockImplementation(({ take }: { take?: number }) => Promise.resolve({ ...mockGeneration, generation_id: 100 + (take ?? 0) }))
  apiMocks.feedback.mockResolvedValue(undefined)
  apiMocks.queueForFl.mockResolvedValue({ queued: 1 })
  apiMocks.saveSession.mockResolvedValue(undefined)
  apiMocks.history.mockResolvedValue(mockHistory)
  apiMocks.brain.mockResolvedValue(mockBrain)
  apiMocks.fingerprints.mockResolvedValue(mockFingerprints)
})

describe('global keyboard map', () => {
  it('supports command-enter, take numbers, and space preview', async () => {
    render(<App />)
    await screen.findByText('connected')
    await userEvent.type(screen.getByLabelText('Generation prompt'), 'dark deep house chords')
    await userEvent.click(screen.getByText('×3 takes'))
    fireEvent.keyDown(window, { key: 'Enter', metaKey: true })
    await waitFor(() => expect(apiMocks.generate).toHaveBeenCalledTimes(3))
    await screen.findByRole('tab', { name: /B/ })
    fireEvent.keyDown(window, { key: '2' })
    expect(screen.getByRole('tab', { name: /B/ })).toHaveAttribute('aria-selected', 'true')

    const dispatch = vi.spyOn(window, 'dispatchEvent')
    fireEvent.keyDown(window, { code: 'Space', key: ' ' })
    expect(dispatch).toHaveBeenCalledWith(expect.objectContaining({ type: 'midi-preview-toggle' }))
  })
})

describe('relay failures', () => {
  it('shows the exact toast and does not crash', async () => {
    apiMocks.health.mockRejectedValue(new Error('offline'))
    render(<App />)
    expect(await screen.findByRole('status')).toHaveTextContent(
      'FL Studio bridge not responding — is the relay running and FL open?',
    )
    expect(screen.getByRole('button', { name: 'Retry' })).toBeInTheDocument()
  })
})

describe('desktop layout contract', () => {
  it('switches to the studio-machine note below 1100px', () => {
    const css = readFileSync(`${process.cwd()}/src/App.css`, 'utf8')
    expect(css).toContain('@media (max-width: 1099px)')
    expect(css).toContain('.small-screen-note')
  })
})

describe('sound-type chips', () => {
  it('persists the selected chip for its element across reloads', async () => {
    const first = render(<App />)
    const chip = screen.getByRole('button', { name: 'Pluck' })
    await userEvent.click(chip)
    expect(chip).toHaveAttribute('aria-pressed', 'true')
    first.unmount()

    render(<App />)
    expect(screen.getByRole('button', { name: 'Pluck' })).toHaveAttribute(
      'aria-pressed',
      'true',
    )
  })
})

