// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'
import type { Zone, ZoneCreate, ZoneSyncResult } from '../types'

export function listZones(viewId?: number): Promise<Zone[]> {
  const query = viewId !== undefined ? `?view_id=${viewId}` : ''
  return get(`/zones${query}`)
}

export function getZone(id: number): Promise<Zone> {
  return get(`/zones/${id}`)
}

export function createZone(data: ZoneCreate): Promise<{ id: number }> {
  return post('/zones', data)
}

export function updateZone(
  id: number,
  data: {
    name: string
    view_id?: number | null
    deployment_retention?: number | null
    manage_soa?: boolean
    manage_ns?: boolean
    git_repo_id?: number | null
    git_branch?: string | null
  },
): Promise<{ message: string }> {
  return put(`/zones/${id}`, data)
}

export function deleteZone(id: number): Promise<{ message: string }> {
  return del(`/zones/${id}`)
}

export function syncCheckZone(id: number): Promise<ZoneSyncResult> {
  return post(`/zones/${id}/sync-check`)
}

export interface SyncCheckAllResult {
  results: ZoneSyncResult[]
  server_time: number
}

export async function syncCheckAll(): Promise<SyncCheckAllResult> {
  return post('/zones/sync-check')
}

export function cloneZone(id: number, data: { name: string; view_id: number }): Promise<Zone> {
  return post(`/zones/${id}/clone`, data)
}

export async function exportZone(id: number): Promise<Blob> {
  const token = localStorage.getItem('jwt') ?? ''
  const res = await fetch(`/api/v1/zones/${id}/export`, {
    headers: { Authorization: `Bearer ${token}` },
  })
  if (!res.ok) throw new Error(`Export failed: ${res.status}`)
  return res.blob()
}

export function updateZoneTags(id: number, tags: string[]): Promise<Zone> {
  return put(`/zones/${id}/tags`, { tags })
}
