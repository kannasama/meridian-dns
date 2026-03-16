import { post } from './client'

export interface RestoreSummary {
  entity_type: string
  created: number
  updated: number
  skipped: number
}

export interface RestoreResult {
  applied: boolean
  summaries: RestoreSummary[]
  credential_warnings: string[]
}

export async function downloadBackup(): Promise<void> {
  const token = localStorage.getItem('jwt')
  const url = '/api/v1/backup/export'
  const resp = await fetch(url, {
    headers: token ? { Authorization: `Bearer ${token}` } : {},
  })
  if (!resp.ok) {
    const body = await resp.json().catch(() => ({ message: resp.statusText }))
    throw new Error(body.message || 'Export failed')
  }
  const blob = await resp.blob()
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = `meridian-backup-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(a.href)
}

export function pushToRepo(): Promise<{ message: string }> {
  return post<{ message: string }>('/backup/push-to-repo')
}

export function restoreFromFile(json: string, apply = false): Promise<RestoreResult> {
  return post<RestoreResult>(`/backup/restore${apply ? '?apply=true' : ''}`, JSON.parse(json))
}

export function restoreFromRepo(apply = false): Promise<RestoreResult> {
  return post<RestoreResult>(`/backup/restore-from-repo${apply ? '?apply=true' : ''}`)
}

export async function downloadZoneExport(zoneId: number): Promise<void> {
  const token = localStorage.getItem('jwt')
  const resp = await fetch(`/api/v1/zones/${zoneId}/export`, {
    headers: token ? { Authorization: `Bearer ${token}` } : {},
  })
  if (!resp.ok) {
    const body = await resp.json().catch(() => ({ message: resp.statusText }))
    throw new Error(body.message || 'Zone export failed')
  }
  const blob = await resp.blob()
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  const disposition = resp.headers.get('Content-Disposition') || ''
  const match = disposition.match(/filename="?([^"]+)"?/)
  a.download = match?.[1] ?? `zone-${zoneId}-export.json`
  a.click()
  URL.revokeObjectURL(a.href)
}

export function importZone(zoneId: number, json: string, apply = false): Promise<RestoreResult> {
  return post<RestoreResult>(
    `/zones/${zoneId}/import${apply ? '?apply=true' : ''}`,
    JSON.parse(json),
  )
}
