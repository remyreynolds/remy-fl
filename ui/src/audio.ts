import { useCallback, useEffect, useRef, useState } from 'react'
import type { Generation, MidiNote, Role } from './types'

type AudioNodeWithStop = OscillatorNode | AudioBufferSourceNode

export function usePreviewAudio(generation?: Generation) {
  const [playing, setPlaying] = useState(false)
  const [playhead, setPlayhead] = useState(0)
  const contextRef = useRef<AudioContext | null>(null)
  const nodesRef = useRef<AudioNodeWithStop[]>([])
  const intervalRef = useRef<number | null>(null)
  const frameRef = useRef<number | null>(null)
  const startedAtRef = useRef(0)

  const stop = useCallback(() => {
    nodesRef.current.forEach((node) => {
      try { node.stop() } catch { /* already stopped */ }
    })
    nodesRef.current = []
    if (intervalRef.current !== null) window.clearInterval(intervalRef.current)
    if (frameRef.current !== null) cancelAnimationFrame(frameRef.current)
    intervalRef.current = null
    frameRef.current = null
    setPlaying(false)
    setPlayhead(0)
  }, [])

  const play = useCallback(async () => {
    if (!generation) return
    stop()
    const AudioContextClass = window.AudioContext
    if (!AudioContextClass) return
    const context = contextRef.current ?? new AudioContextClass()
    contextRef.current = context
    await context.resume()
    const duration = generation.meta.loop_bars * 4 * (60 / generation.meta.bpm)
    const startAt = context.currentTime + 0.04
    startedAtRef.current = startAt
    scheduleGeneration(context, generation, startAt, nodesRef.current)
    intervalRef.current = window.setInterval(() => {
      const nextStart = context.currentTime + 0.04
      startedAtRef.current = nextStart
      scheduleGeneration(context, generation, nextStart, nodesRef.current)
    }, duration * 1000)
    const update = () => {
      const elapsed = Math.max(0, context.currentTime - startedAtRef.current)
      setPlayhead((elapsed % duration) / duration)
      frameRef.current = requestAnimationFrame(update)
    }
    frameRef.current = requestAnimationFrame(update)
    setPlaying(true)
  }, [generation, stop])

  const toggle = useCallback(() => {
    if (playing) stop()
    else void play()
  }, [play, playing, stop])

  useEffect(() => stop, [generation, stop])

  return { playing, playhead, toggle, stop }
}

function scheduleGeneration(
  context: AudioContext,
  generation: Generation,
  startsAt: number,
  nodes: AudioNodeWithStop[],
) {
  const secondsPerTick = (60 / generation.meta.bpm) / generation.meta.ppq
  for (const track of generation.tracks) {
    for (const note of track.notes) {
      scheduleNote(context, track.role, note, startsAt, secondsPerTick, nodes)
    }
  }
}

function scheduleNote(
  context: AudioContext,
  role: Role,
  note: MidiNote,
  loopStart: number,
  secondsPerTick: number,
  nodes: AudioNodeWithStop[],
) {
  const start = loopStart + note.start_tick * secondsPerTick
  const duration = Math.max(0.025, note.length_ticks * secondsPerTick)
  const gain = context.createGain()
  const volume = (note.velocity / 127) * (role === 'chords' ? 0.06 : 0.1)
  gain.gain.setValueAtTime(0.0001, start)
  gain.gain.exponentialRampToValueAtTime(Math.max(0.001, volume), start + 0.008)
  gain.gain.exponentialRampToValueAtTime(0.0001, start + duration)
  gain.connect(context.destination)

  if (role === 'perc') {
    const source = context.createBufferSource()
    const sampleLength = Math.max(1, Math.floor(context.sampleRate * duration))
    const buffer = context.createBuffer(1, sampleLength, context.sampleRate)
    const channel = buffer.getChannelData(0)
    for (let index = 0; index < channel.length; index += 1) {
      channel[index] = (Math.random() * 2 - 1) * (1 - index / channel.length)
    }
    source.buffer = buffer
    const filter = context.createBiquadFilter()
    filter.type = note.pitch <= 38 ? 'lowpass' : 'highpass'
    filter.frequency.value = note.pitch <= 38 ? 180 : 5500
    source.connect(filter).connect(gain)
    source.start(start)
    source.stop(start + duration)
    nodes.push(source)
    return
  }

  const oscillator = context.createOscillator()
  oscillator.type = role === 'bass' ? 'sawtooth' : role === 'melody' || role === 'arp' ? 'triangle' : 'sine'
  oscillator.frequency.setValueAtTime(440 * 2 ** ((note.pitch - 69) / 12), start)
  const filter = context.createBiquadFilter()
  filter.type = 'lowpass'
  filter.frequency.value = role === 'bass' ? 520 : 2600
  oscillator.connect(filter).connect(gain)
  oscillator.start(start)
  oscillator.stop(start + duration)
  nodes.push(oscillator)
}

