// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'
import type { UserDetail } from '../types'

export function listUsers(): Promise<UserDetail[]> {
  return get('/users')
}

export function getUser(id: number): Promise<UserDetail> {
  return get(`/users/${id}`)
}

export function createUser(data: {
  username: string
  email: string
  password: string
  group_ids: number[]
  force_password_change?: boolean
}): Promise<{ id: number }> {
  return post('/users', data)
}

export function updateUser(
  id: number,
  data: { email: string; is_active: boolean; group_ids: number[] },
): Promise<{ message: string }> {
  return put(`/users/${id}`, data)
}

export function deleteUser(id: number): Promise<{ message: string }> {
  return del(`/users/${id}`)
}

export function resetPassword(id: number, password: string): Promise<{ message: string }> {
  return post(`/users/${id}/reset-password`, { password })
}
