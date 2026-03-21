// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'
import type { Variable, VariableCreate, VariableUpdate } from '../types'

export function listVariables(scope?: string, zoneId?: number): Promise<Variable[]> {
  const params = new URLSearchParams()
  if (scope) params.set('scope', scope)
  if (zoneId !== undefined) params.set('zone_id', String(zoneId))
  const query = params.toString() ? `?${params.toString()}` : ''
  return get(`/variables${query}`)
}

export function getVariable(id: number): Promise<Variable> {
  return get(`/variables/${id}`)
}

export function createVariable(data: VariableCreate): Promise<{ id: number }> {
  return post('/variables', data)
}

export function updateVariable(id: number, data: VariableUpdate): Promise<{ message: string }> {
  return put(`/variables/${id}`, data)
}

export function deleteVariable(id: number): Promise<{ message: string }> {
  return del(`/variables/${id}`)
}
