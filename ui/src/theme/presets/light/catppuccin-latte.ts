// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import type { ThemePreset } from '../types'

const preset: ThemePreset = {
  name: 'catppuccin-latte',
  label: 'Catppuccin Latte',
  mode: 'light',
  defaultAccent: 'purple',
  surface: {
    0: '#ffffff',
    50: '#eff1f5',     // base — page background
    100: '#e6e9ef',    // mantle — card surface
    200: '#dce0e8',    // crust — borders
    300: '#ccd0da',    // surface0 — dividers
    400: '#acb0be',    // surface2 — muted text
    500: '#9ca0b0',    // overlay0 — secondary text
    600: '#7c7f93',    // overlay2 — body text
    700: '#6c6f85',    // subtext0 — emphasis text
    800: '#5c5f77',    // subtext1 — heading text
    900: '#4c4f69',    // text — strong text
    950: '#4c4f69',    // text — darkest text
  },
}

export default preset
