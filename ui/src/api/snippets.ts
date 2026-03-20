// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'

export interface SnippetRecord {
  id?: number
  name: string
  type: string
  ttl: number
  value_template: string
  priority: number
  sort_order: number
}

export interface Snippet {
  id: number
  name: string
  description: string
  records: SnippetRecord[]
  created_at: string
  updated_at: string
}

export interface SnippetCreate {
  name: string
  description: string
  records: SnippetRecord[]
}

export function listSnippets(): Promise<Snippet[]> {
  return get('/snippets')
}

export function getSnippet(id: number): Promise<Snippet> {
  return get(`/snippets/${id}`)
}

export function createSnippet(data: SnippetCreate): Promise<{ id: number; message: string }> {
  return post('/snippets', data)
}

export function updateSnippet(id: number, data: SnippetCreate): Promise<{ message: string }> {
  return put(`/snippets/${id}`, data)
}

export function deleteSnippet(id: number): Promise<{ message: string }> {
  return del(`/snippets/${id}`)
}

export function applySnippetToZone(
  zoneId: number,
  snippetId: number
): Promise<{ message: string; records_applied: number }> {
  return post(`/zones/${zoneId}/snippets/${snippetId}/apply`, {})
}
