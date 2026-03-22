import { get } from './client'
import type { SystemLog, SystemLogQuery } from '../types'

export function querySystemLogs(query: SystemLogQuery = {}): Promise<SystemLog[]> {
  const params = new URLSearchParams()
  if (query.category) params.set('category', query.category)
  if (query.severity) params.set('severity', query.severity)
  if (query.zone_id) params.set('zone_id', String(query.zone_id))
  if (query.provider_id) params.set('provider_id', String(query.provider_id))
  if (query.from) params.set('from', String(query.from))
  if (query.to) params.set('to', String(query.to))
  if (query.limit) params.set('limit', String(query.limit))
  const qs = params.toString() ? `?${params.toString()}` : ''
  return get(`/system-logs${qs}`)
}
