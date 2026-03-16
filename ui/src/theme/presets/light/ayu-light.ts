// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'ayu-light',
  label: 'Ayu Light',
  mode: 'light',
  defaultAccent: 'orange',
  surface: {
    0: '#ffffff',
    50: '#fafafa',     // background — page background
    100: '#f3f4f5',    // panel — card surface
    200: '#e7e8e9',    // line — borders
    300: '#d8d9da',    // interpolated — dividers
    400: '#abb0b6',    // comment — muted text
    500: '#8a9199',    // interpolated — secondary text
    600: '#6b7178',    // interpolated — body text
    700: '#5c6166',    // foreground — emphasis text
    800: '#4b5057',    // darkened — heading text
    900: '#3a3f44',    // darkened — strong text
    950: '#2a2e33',    // darkened — darkest text
  },
}

export default preset
