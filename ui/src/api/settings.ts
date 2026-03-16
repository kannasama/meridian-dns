// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, put } from './client'
import type { SystemSetting } from '../types'

export function listSettings(): Promise<SystemSetting[]> {
  return get('/settings')
}

export function updateSettings(data: Record<string, string | number | boolean>): Promise<{ message: string; updated: string[] }> {
  return put('/settings', data)
}
