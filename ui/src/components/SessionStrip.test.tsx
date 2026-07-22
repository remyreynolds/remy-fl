import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { useState } from 'react'
import { describe, expect, it, vi } from 'vitest'
import { mockSession } from '../mockData'
import type { Role, SessionState } from '../types'
import { SessionStrip } from './SessionStrip'

function Harness() {
  const [session, setSession] = useState<SessionState>(structuredClone(mockSession))
  const toggle = (role: Role) => setSession((current) => ({
    ...current,
    slots: current.slots.map((slot) => slot.role === role ? { ...slot, locked: !slot.locked } : slot),
  }))
  return <SessionStrip session={session} onChange={setSession} onToggleLock={toggle} onGenerateRole={vi.fn()} onSendAll={vi.fn()} />
}

describe('SessionStrip', () => {
  it('shows filled and empty slot states', () => {
    render(<Harness />)
    expect(screen.getByTestId('slot-chords')).toHaveClass('is-filled', 'is-locked')
    expect(screen.getByTestId('slot-melody')).toHaveClass('is-empty')
    expect(screen.getByRole('button', { name: 'Generate Melody' })).toBeEnabled()
  })

  it('toggles lock state without changing the slot generation', async () => {
    render(<Harness />)
    await userEvent.click(screen.getByRole('button', { name: 'Unlock Chords' }))
    expect(screen.getByTestId('slot-chords')).not.toHaveClass('is-locked')
    expect(screen.getByTestId('slot-chords')).toHaveClass('is-filled')
    expect(screen.getByRole('button', { name: 'Lock Chords' })).toHaveAttribute('aria-pressed', 'false')
  })
})

