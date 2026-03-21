// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'

interface RecordDiff {
  action: string
  name: string
  type: string
  ttl: number
  source_value: string
  provider_value: string
  priority?: number
}

export interface ZoneTemplate {
  id: number
  name: string
  description: string
  soa_preset_id: number | null
  snippet_ids: number[]
  created_at: string
  updated_at: string
}

export type ZoneTemplateCreate = {
  name: string
  description: string
  soa_preset_id: number | null
  snippet_ids: number[]
}

export interface ComplianceCheckResult {
  zone_id: number
  zone_name: string
  has_drift: boolean
  diffs: RecordDiff[]
}

export function listTemplates(): Promise<ZoneTemplate[]> {
  return get('/templates')
}

export function getTemplate(id: number): Promise<ZoneTemplate> {
  return get(`/templates/${id}`)
}

export function createTemplate(data: ZoneTemplateCreate): Promise<{ id: number }> {
  return post('/templates', data)
}

export function updateTemplate(id: number, data: ZoneTemplateCreate): Promise<{ message: string }> {
  return put(`/templates/${id}`, data)
}

export function deleteTemplate(id: number): Promise<{ message: string }> {
  return del(`/templates/${id}`)
}

export function pushTemplate(
  zoneId: number,
  templateId: number,
  link: boolean
): Promise<ComplianceCheckResult | { message: string; records_applied: number }> {
  return post(`/zones/${zoneId}/template/push`, { template_id: templateId, link })
}

export function checkTemplateCompliance(zoneId: number): Promise<ComplianceCheckResult> {
  return get(`/zones/${zoneId}/template/check`)
}

export function applyComplianceRecords(
  zoneId: number,
  records: { name: string; type: string }[]
): Promise<{ message: string }> {
  return post(`/zones/${zoneId}/template/apply`, { records })
}

export function unlinkTemplate(zoneId: number): Promise<{ message: string }> {
  return del(`/zones/${zoneId}/template`)
}
