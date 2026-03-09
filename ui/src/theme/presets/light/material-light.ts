import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'material-light',
  label: 'Material Light',
  mode: 'light',
  defaultAccent: 'cyan',
  surface: {
    0: '#ffffff',
    50: '#fafafa',     // background — page background
    100: '#f5f5f5',    // surface — card surface
    200: '#eeeeee',    // overlay — borders
    300: '#e0e0e0',    // dividers
    400: '#bdbdbd',    // muted text
    500: '#9e9e9e',    // secondary text
    600: '#757575',    // body text
    700: '#616161',    // emphasis text
    800: '#424242',    // heading text
    900: '#212121',    // text — strong text
    950: '#121212',    // darkest text
  },
}

export default preset
