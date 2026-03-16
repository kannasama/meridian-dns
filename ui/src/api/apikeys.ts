// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, del } from './client'
import type { ApiKeyEntry } from '../types'

export function listApiKeys(): Promise<ApiKeyEntry[]> {
  return get('/api-keys')
}

export function createApiKey(data: {
  description: string
  expires_at?: string
}): Promise<{ id: number; key: string; prefix: string }> {
  return post('/api-keys', data)
}

export function revokeApiKey(id: number): Promise<{ message: string }> {
  return del(`/api-keys/${id}`)
}
