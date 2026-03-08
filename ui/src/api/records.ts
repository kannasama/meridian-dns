import { get, post, put, del } from './client'
import type { DnsRecord, RecordCreate } from '../types'

export function listRecords(zoneId: number): Promise<DnsRecord[]> {
  return get(`/zones/${zoneId}/records`)
}

export function getRecord(zoneId: number, recordId: number): Promise<DnsRecord> {
  return get(`/zones/${zoneId}/records/${recordId}`)
}

export function createRecord(zoneId: number, data: RecordCreate): Promise<{ id: number }> {
  return post(`/zones/${zoneId}/records`, data)
}

export function updateRecord(
  zoneId: number,
  recordId: number,
  data: RecordCreate,
): Promise<{ message: string }> {
  return put(`/zones/${zoneId}/records/${recordId}`, data)
}

export function deleteRecord(zoneId: number, recordId: number): Promise<{ message: string }> {
  return del(`/zones/${zoneId}/records/${recordId}`)
}

export function createRecordsBatch(
  zoneId: number,
  records: RecordCreate[],
): Promise<{ ids: number[]; count: number }> {
  return post(`/zones/${zoneId}/records/batch`, records)
}

export interface ProviderRecord {
  name: string
  type: string
  value: string
  ttl: number
  priority: number
}

export function fetchProviderRecords(zoneId: number): Promise<ProviderRecord[]> {
  return get(`/zones/${zoneId}/provider-records`)
}

export function restoreRecord(zoneId: number, recordId: number): Promise<void> {
  return post(`/zones/${zoneId}/records/${recordId}/restore`)
}

export function batchRecords(
  zoneId: number,
  body: { updates?: Array<Record<string, unknown>>; deletes?: number[] },
): Promise<DnsRecord[]> {
  return put(`/zones/${zoneId}/records/batch`, body)
}
