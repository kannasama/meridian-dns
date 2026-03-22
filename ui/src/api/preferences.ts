// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, put } from './client'

export function getPreferences(): Promise<Record<string, unknown>> {
  return get('/preferences')
}

export function savePreferences(prefs: Record<string, unknown>): Promise<{ message: string }> {
  return put('/preferences', prefs)
}
