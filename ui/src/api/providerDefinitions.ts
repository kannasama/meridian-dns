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

export function exportProviderDefinition(id: number): void {
  // Triggers browser download via anchor click
  const a = document.createElement('a')
  a.href = `/api/v1/provider-definitions/${id}/export`
  a.click()
}
