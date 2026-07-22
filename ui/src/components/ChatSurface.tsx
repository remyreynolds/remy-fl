import {
  AssistantRuntimeProvider,
  useLocalRuntime,
  type ChatModelAdapter,
  type ThreadMessage,
} from '@assistant-ui/react'
import { useMemo } from 'react'
import { Thread } from '@/components/assistant-ui/thread'
import { TooltipProvider } from '@/components/ui/tooltip'
import { relayApi } from '@/api'

function messageText(message: ThreadMessage): string {
  if (!('content' in message) || !Array.isArray(message.content)) return ''
  return message.content
    .map((part) => (part.type === 'text' ? part.text : ''))
    .filter(Boolean)
    .join('\n')
    .trim()
}

const starterSuggestions = [
  { prompt: 'Make a full 8-bar tech house loop with chords, bass, melody, and drums' },
  { prompt: 'Make a groovy tech house bassline in the project key, 8 bars' },
  { prompt: 'Make warm deep house chords in the project key, 8 bars' },
  { prompt: 'Make a memorable house topline melody over the current key, 8 bars' },
]

type ChatSurfaceProps = {
  sessionId: string
  bars: number
  onGenerated?: () => void
}

export function ChatSurface({ sessionId, bars, onGenerated }: ChatSurfaceProps) {
  const chatModel = useMemo<ChatModelAdapter>(
    () => ({
      async run({ messages }) {
        const lastUser = [...messages].reverse().find((m) => m.role === 'user')
        const prompt = lastUser ? messageText(lastUser) : ''
        if (!prompt) {
          return { content: [{ type: 'text', text: 'Tell me what to generate.' }] }
        }

        try {
          const generation = await relayApi.generate({
            prompt,
            element: 'chords',
            bars,
            session_id: sessionId,
          })
          onGenerated?.()
          const lines = [
            generation.plan_summary,
            generation.description,
            generation.influence_citation,
            generation.warnings?.length ? `Notes: ${generation.warnings.join(' · ')}` : '',
            'MIDI is ready — open Generate to preview, keep, or send to FL.',
          ].filter(Boolean)
          return { content: [{ type: 'text', text: lines.join('\n\n') }] }
        } catch (error) {
          const text = error instanceof Error ? error.message : 'Generation failed'
          return {
            content: [{ type: 'text', text: `⚠ ${text}` }],
          }
        }
      },
    }),
    [bars, onGenerated, sessionId],
  )

  const runtime = useLocalRuntime(chatModel, {
    adapters: {
      suggestion: {
        async generate({ messages }) {
          if (messages.length > 0) return []
          return starterSuggestions
        },
      },
    },
  })

  return (
    <TooltipProvider>
      <AssistantRuntimeProvider runtime={runtime}>
        <section className="chat-surface h-[calc(100vh-58px)] min-h-[520px] border-l border-border">
          <Thread
            components={{
              Welcome: () => (
                <div className="aui-thread-welcome-root mb-6 flex flex-col items-center px-4 text-center">
                  <h1 className="text-2xl font-semibold tracking-tight">How can I help you today?</h1>
                  <p className="text-muted-foreground mt-2 max-w-md text-sm">
                    Describe a loop, bassline, or vibe — I'll generate MIDI for FL Studio.
                  </p>
                </div>
              ),
            }}
          />
        </section>
      </AssistantRuntimeProvider>
    </TooltipProvider>
  )
}
