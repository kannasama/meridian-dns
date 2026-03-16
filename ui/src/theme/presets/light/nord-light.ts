// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'nord-light',
  label: 'Nord Light',
  mode: 'light',
  defaultAccent: 'sky',
  surface: {
    0: '#ffffff',
    50: '#eceff4',     // nord6 — page background
    100: '#e5e9f0',    // nord5 — card surface
    200: '#d8dee9',    // nord4 — borders
    300: '#c0c6d3',    // interpolated — dividers
    400: '#8690a2',    // interpolated — muted text
    500: '#6d7a90',    // interpolated — secondary text
    600: '#4c566a',    // nord3 — body text
    700: '#434c5e',    // nord2 — emphasis text
    800: '#3b4252',    // nord1 — heading text
    900: '#2e3440',    // nord0 — strong text
    950: '#242933',    // darkened nord0 — darkest text
  },
}

export default preset
