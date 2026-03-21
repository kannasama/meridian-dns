// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'

export interface SoaPreset {
  id: number
  name: string
  mname_template: string
  rname_template: string
  refresh: number
  retry: number
  expire: number
  minimum: number
  default_ttl: number
  created_at: string
  updated_at: string
}

export type SoaPresetCreate = Omit<SoaPreset, 'id' | 'created_at' | 'updated_at'>

export function listSoaPresets(): Promise<SoaPreset[]> {
  return get('/soa-presets')
}

export function getSoaPreset(id: number): Promise<SoaPreset> {
  return get(`/soa-presets/${id}`)
}

export function createSoaPreset(data: SoaPresetCreate): Promise<{ id: number }> {
  return post('/soa-presets', data)
}

export function updateSoaPreset(id: number, data: SoaPresetCreate): Promise<{ message: string }> {
  return put(`/soa-presets/${id}`, data)
}

export function deleteSoaPreset(id: number): Promise<{ message: string }> {
  return del(`/soa-presets/${id}`)
}
