import { get, post, put, del } from './client'
import type { Group, GroupDetail } from '../types'

export function listGroups(): Promise<Group[]> {
  return get('/groups')
}

export function getGroup(id: number): Promise<GroupDetail> {
  return get(`/groups/${id}`)
}

export function createGroup(data: {
  name: string
  description: string
}): Promise<{ id: number }> {
  return post('/groups', data)
}

export function updateGroup(
  id: number,
  data: { name: string; description: string },
): Promise<{ message: string }> {
  return put(`/groups/${id}`, data)
}

export function deleteGroup(id: number): Promise<{ message: string }> {
  return del(`/groups/${id}`)
}
