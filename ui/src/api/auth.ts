// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put } from './client'
import type { User } from '../types'

export function login(username: string, password: string): Promise<{ token: string }> {
  return post('/auth/local/login', { username, password })
}

export function logout(): Promise<{ message: string }> {
  return post('/auth/local/logout')
}

export function me(): Promise<User> {
  return get('/auth/me')
}

export function updateProfile(data: { email: string; display_name?: string }): Promise<{ message: string }> {
  return put('/auth/profile', data)
}

export function changePassword(data: {
  current_password: string
  new_password: string
}): Promise<{ message: string }> {
  return post('/auth/change-password', data)
}

export function listEnabledIdps(): Promise<{ id: number; name: string; type: string }[]> {
  return get('/auth/identity-providers')
}
