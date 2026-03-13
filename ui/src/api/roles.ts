import { get, post, put, del } from './client'
import type { Role, RoleCreate, PermissionCategory } from '../types'

export function listRoles(): Promise<Role[]> {
  return get('/roles')
}

export function getRole(id: number): Promise<Role> {
  return get(`/roles/${id}`)
}

export function createRole(data: RoleCreate): Promise<{ id: number }> {
  return post('/roles', data)
}

export function updateRole(
  id: number,
  data: { name: string; description: string },
): Promise<{ message: string }> {
  return put(`/roles/${id}`, data)
}

export function deleteRole(id: number): Promise<{ message: string }> {
  return del(`/roles/${id}`)
}

export function getRolePermissions(id: number): Promise<string[]> {
  return get(`/roles/${id}/permissions`)
}

export function setRolePermissions(
  id: number,
  permissions: string[],
): Promise<{ message: string }> {
  return put(`/roles/${id}/permissions`, { permissions })
}

export function listPermissionCategories(): Promise<PermissionCategory[]> {
  return get('/permissions')
}
