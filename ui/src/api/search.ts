// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get } from './client'
import type { SearchResult } from '../types'

export interface SearchParams {
  q: string
  type?: string
  zone_id?: number
  view_id?: number
}

export function searchRecords(params: SearchParams): Promise<SearchResult[]> {
  const query = new URLSearchParams()
  query.set('q', params.q)
  if (params.type) query.set('type', params.type)
  if (params.zone_id !== undefined) query.set('zone_id', String(params.zone_id))
  if (params.view_id !== undefined) query.set('view_id', String(params.view_id))
  return get(`/search/records?${query.toString()}`)
}
