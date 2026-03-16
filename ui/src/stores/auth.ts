// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import * as authApi from '../api/auth'
import type { User } from '../types'

export const useAuthStore = defineStore('auth', () => {
  const user = ref<User | null>(null)
  const token = ref<string | null>(localStorage.getItem('jwt'))

  const isAuthenticated = computed(() => !!token.value && !!user.value)
  const role = computed(() => user.value?.role ?? 'Viewer')
  const permissions = computed(() => user.value?.permissions ?? [])
  const isAdmin = computed(() => role.value === 'Admin')
  const isOperator = computed(() => role.value === 'Operator' || role.value === 'Admin')

  async function hydrate(): Promise<boolean> {
    if (!token.value) return false
    try {
      user.value = await authApi.me()
      return true
    } catch {
      clear()
      return false
    }
  }

  async function login(username: string, password: string) {
    const result = await authApi.login(username, password)
    token.value = result.token
    localStorage.setItem('jwt', result.token)
    user.value = await authApi.me()
  }

  async function logout() {
    try {
      await authApi.logout()
    } finally {
      clear()
    }
  }

  function clear() {
    token.value = null
    user.value = null
    localStorage.removeItem('jwt')
  }

  return { user, token, isAuthenticated, role, permissions, isAdmin, isOperator, hydrate, login, logout }
})
