// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

export interface SurfacePalette {
  0: string
  50: string
  100: string
  200: string
  300: string
  400: string
  500: string
  600: string
  700: string
  800: string
  900: string
  950: string
}

export interface ThemePreset {
  name: string
  label: string
  mode: 'dark' | 'light'
  defaultAccent: string        // Tailwind palette name (e.g., 'indigo', 'purple')
  surface: SurfacePalette
}
