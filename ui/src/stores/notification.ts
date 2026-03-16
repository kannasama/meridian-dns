// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { defineStore } from 'pinia'
import { ref } from 'vue'

export interface ToastMessage {
  severity: 'success' | 'info' | 'warn' | 'error'
  summary: string
  detail?: string
  life?: number
}

export const useNotificationStore = defineStore('notification', () => {
  const messages = ref<ToastMessage[]>([])

  function add(msg: ToastMessage) {
    messages.value.push({ life: 4000, ...msg })
  }

  function success(summary: string, detail?: string) {
    add({ severity: 'success', summary, detail })
  }

  function error(summary: string, detail?: string) {
    add({ severity: 'error', summary, detail })
  }

  function clear() {
    messages.value = []
  }

  return { messages, add, success, error, clear }
})
