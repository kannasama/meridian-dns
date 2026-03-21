// ui/src/api/providerDefinitions.ts
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'

export interface ProviderDefinition {
  id: number
  name: string
  type_slug: string
  version: string
  definition: Record<string, unknown>
  source_url: string
  active_instance_count: number
  imported_at: number
  updated_at: number
}

export interface ProviderDefinitionCreate {
  name: string
  type_slug: string
  version: string
  definition: Record<string, unknown>
  source_url?: string
}

export interface ProviderDefinitionUpdate {
  name: string
  version: string
  definition: Record<string, unknown>
  source_url?: string
}

export function listProviderDefinitions(): Promise<ProviderDefinition[]> {
  return get('/provider-definitions')
}

export function getProviderDefinition(id: number): Promise<ProviderDefinition> {
  return get(`/provider-definitions/${id}`)
}

export function createProviderDefinition(
  data: ProviderDefinitionCreate
): Promise<{ id: number; updated: boolean }> {
  return post('/provider-definitions', data)
}

export function updateProviderDefinition(
  id: number,
  data: ProviderDefinitionUpdate
): Promise<{ message: string }> {
  return put(`/provider-definitions/${id}`, data)
}

export function deleteProviderDefinition(id: number): Promise<{ message: string }> {
  return del(`/provider-definitions/${id}`)
}

export async function exportProviderDefinition(id: number, typeSlug: string): Promise<void> {
  const data = await get(`/provider-definitions/${id}/export`)
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${typeSlug}.json`
  a.click()
  URL.revokeObjectURL(url)
}
