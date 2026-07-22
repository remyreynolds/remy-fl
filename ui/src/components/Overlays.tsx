import { Command, X } from 'lucide-react'

export interface ToastState {
  message: string
  kind?: 'error' | 'success' | 'info'
  retry?: () => void
}

export function Toast({ toast, onClose }: { toast: ToastState; onClose: () => void }) {
  return (
    <div className={`toast toast--${toast.kind ?? 'info'}`} role="status">
      <span>{toast.message}</span>
      {toast.retry && <button type="button" onClick={toast.retry}>Retry</button>}
      <button type="button" onClick={onClose} aria-label="Close notification"><X size={13} /></button>
    </div>
  )
}

export function ShortcutOverlay({ onClose }: { onClose: () => void }) {
  return (
    <div className="shortcut-backdrop" onMouseDown={onClose}>
      <div className="shortcut-overlay" role="dialog" aria-label="Keyboard shortcuts" onMouseDown={(event) => event.stopPropagation()}>
        <header><Command size={17} /><h2>Keyboard map</h2><button type="button" onClick={onClose}><X size={14} /></button></header>
        <dl>
          <div><dt><kbd>Space</kbd></dt><dd>Play / stop preview</dd></div>
          <div><dt><kbd>⌘</kbd><kbd>↵</kbd></dt><dd>Generate</dd></div>
          <div><dt><kbd>1</kbd> <kbd>2</kbd> <kbd>3</kbd></dt><dd>Select take</dd></div>
          <div><dt><kbd>?</kbd></dt><dd>Show this map</dd></div>
          <div><dt><kbd>Esc</kbd></dt><dd>Close overlays</dd></div>
        </dl>
      </div>
    </div>
  )
}

