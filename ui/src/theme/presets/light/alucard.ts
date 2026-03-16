// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'alucard',
  label: 'Alucard',
  mode: 'light',
  defaultAccent: 'purple',
  surface: {
    0: '#ffffff',
    50: '#f8f8f2',     // Dracula foreground as bg — page background
    100: '#f0f0e8',    // slightly darker — card surface
    200: '#e0e0d4',    // borders
    300: '#d0d0c4',    // dividers
    400: '#a0a094',    // muted text
    500: '#808074',    // secondary text
    600: '#5a5a50',    // body text
    700: '#44444a',    // emphasis text
    800: '#2e2e34',    // heading text
    900: '#21222c',    // Dracula background — strong text
    950: '#191a21',    // darkened — darkest text
  },
}

export default preset
