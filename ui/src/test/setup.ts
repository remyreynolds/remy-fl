import '@testing-library/jest-dom/vitest'
import { cleanup } from '@testing-library/react'
import { afterEach, vi } from 'vitest'

afterEach(cleanup)

Object.defineProperty(HTMLCanvasElement.prototype, 'getContext', {
  value: vi.fn(() => ({
    scale: vi.fn(),
    fillRect: vi.fn(),
    beginPath: vi.fn(),
    moveTo: vi.fn(),
    lineTo: vi.fn(),
    stroke: vi.fn(),
    set fillStyle(_: string) {},
    set strokeStyle(_: string) {},
    set lineWidth(_: number) {},
  })),
})

Object.defineProperty(HTMLCanvasElement.prototype, 'getBoundingClientRect', {
  value: () => ({
    width: 640,
    height: 280,
    top: 0,
    left: 0,
    right: 640,
    bottom: 280,
    x: 0,
    y: 0,
    toJSON: () => ({}),
  }),
})

Object.defineProperty(window, 'matchMedia', {
  value: vi.fn(() => ({
    matches: false,
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  })),
})

