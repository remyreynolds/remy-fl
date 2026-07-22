import type { Preview } from '@storybook/react-vite'
import '../src/index.css'
import '../src/App.css'
import '@fontsource/dm-sans/400.css'
import '@fontsource/ibm-plex-mono/400.css'
import '@fontsource/instrument-serif/400.css'

const preview: Preview = {
  parameters: {
    layout: 'fullscreen',
    backgrounds: { default: 'studio', values: [{ name: 'studio', value: '#0b0c0a' }] },
  },
}

export default preview

