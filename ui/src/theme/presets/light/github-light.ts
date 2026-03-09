import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'github-light',
  label: 'GitHub Light',
  mode: 'light',
  defaultAccent: 'blue',
  surface: {
    0: '#ffffff',
    50: '#f6f8fa',     // canvas subtle — page background
    100: '#eef1f5',    // interpolated — card surface
    200: '#d0d7de',    // border default — borders
    300: '#c0c7ce',    // interpolated — dividers
    400: '#8b949e',    // muted — muted text
    500: '#656d76',    // muted foreground — secondary text
    600: '#484f58',    // interpolated — body text
    700: '#31373d',    // interpolated — emphasis text
    800: '#24292f',    // foreground — heading text
    900: '#1f2328',    // foreground default — strong text
    950: '#171b20',    // darkened — darkest text
  },
}

export default preset
