// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get } from './client'

export interface CustomThemePreset {
  name: string
  label: string
  mode: 'dark' | 'light'
  defaultAccent: string
  surface: Record<string, string>
}

export function listCustomThemes(): Promise<CustomThemePreset[]> {
  return get('/themes')
}
