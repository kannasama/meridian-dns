// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'rose-pine-dawn',
  label: 'Rose Pine Dawn',
  mode: 'light',
  defaultAccent: 'rose',
  surface: {
    0: '#ffffff',
    50: '#faf4ed',     // base — page background
    100: '#fffaf3',    // surface — card surface
    200: '#f2e9e1',    // overlay — borders
    300: '#dfdad9',    // interpolated — dividers
    400: '#9893a5',    // muted — muted text
    500: '#797593',    // subtle — secondary text
    600: '#6e6a86',    // interpolated — body text
    700: '#575279',    // text — emphasis text
    800: '#4a4568',    // darkened text — heading text
    900: '#3d3857',    // darkened text — strong text
    950: '#2e2946',    // darkened text — darkest text
  },
}

export default preset
