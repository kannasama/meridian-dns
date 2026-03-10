import { get, put } from './client'
import type { SystemSetting } from '../types'

export function listSettings(): Promise<SystemSetting[]> {
  return get('/settings')
}

export function updateSettings(data: Record<string, string | number | boolean>): Promise<{ message: string; updated: string[] }> {
  return put('/settings', data)
}
