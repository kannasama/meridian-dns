import { get, post, del } from './client'
import type { ApiKeyEntry } from '../types'

export function listApiKeys(): Promise<ApiKeyEntry[]> {
  return get('/api-keys')
}

export function createApiKey(data: {
  description: string
  expires_at?: string
}): Promise<{ id: number; key: string; prefix: string }> {
  return post('/api-keys', data)
}

export function revokeApiKey(id: number): Promise<{ message: string }> {
  return del(`/api-keys/${id}`)
}
