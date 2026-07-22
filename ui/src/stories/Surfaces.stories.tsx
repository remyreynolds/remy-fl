import type { Meta, StoryObj } from '@storybook/react-vite'
import { BrainSurface } from '../components/BrainSurface'
import { GenerateSurface } from '../components/GenerateSurface'
import { HistorySurface } from '../components/HistorySurface'
import { ReferencesSurface } from '../components/ReferencesSurface'
import { mockBrain, mockFingerprints, mockGeneration, mockHistory } from '../mockData'

const meta = {
  title: 'Companion/Surfaces',
  parameters: { layout: 'fullscreen' },
} satisfies Meta

export default meta
type Story = StoryObj<typeof meta>

const noop = () => undefined

export const Generate: Story = {
  render: () => (
    <GenerateSurface
      prompt="dark deep house chords, warm and sparse"
      onPromptChange={noop}
      role="chords"
      soundType="keys"
      onSoundTypeChange={noop}
      controls={{ swing: 56, density: 0, brightness: -1, threeTakes: true }}
      onControlsChange={noop}
      takes={[mockGeneration, { ...mockGeneration, generation_id: 52 }, { ...mockGeneration, generation_id: 53 }]}
      activeTake={0}
      onTakeChange={noop}
      generating={false}
      stage="critic ✓"
      onGenerate={noop}
      onKeep={noop}
      onSend={noop}
      onFeedback={noop}
      onTweak={noop}
    />
  ),
}

export const History: Story = {
  render: () => <HistorySurface history={mockHistory} sessionId="studio-session" query="" onQuery={noop} onReplay={noop} onBringBack={noop} />,
}

export const Brain: Story = {
  render: () => <BrainSurface state={mockBrain} onDeleteTaste={noop} onToggleHeuristic={noop} />,
}

export const References: Story = {
  render: () => (
    <ReferencesSurface
      fingerprints={mockFingerprints}
      analyzing={false}
      onAnalyze={noop}
      onSave={noop}
      onDiscard={noop}
      onDelete={noop}
    />
  ),
}

