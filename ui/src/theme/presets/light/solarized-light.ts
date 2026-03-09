import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'solarized-light',
  label: 'Solarized Light',
  mode: 'light',
  defaultAccent: 'blue',
  surface: {
    0: '#ffffff',
    50: '#fdf6e3',     // base3 — page background
    100: '#eee8d5',    // base2 — card surface
    200: '#ddd6c1',    // interpolated — borders
    300: '#c9c1ab',    // interpolated — dividers
    400: '#93a1a1',    // base1 — muted text
    500: '#839496',    // base0 — secondary text
    600: '#657b83',    // base00 — body text
    700: '#586e75',    // base01 — emphasis text
    800: '#073642',    // base02 — heading text
    900: '#002b36',    // base03 — strong text
    950: '#001f27',    // darkened base03 — darkest text
  },
}

export default preset
