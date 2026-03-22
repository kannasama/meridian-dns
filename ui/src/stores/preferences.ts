// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getPreferences, savePreferences } from '../api/preferences'

export const usePreferencesStore = defineStore('preferences', () => {
  const data = ref<Record<string, unknown>>({})
  const loaded = ref(false)

  async function fetch() {
    try {
      data.value = await getPreferences()
      loaded.value = true
    } catch {
      // Preferences are optional — fail silently
      loaded.value = true
    }
  }

  async function save(key: string, value: unknown) {
    data.value[key] = value
    await savePreferences({ [key]: value })
  }

  async function saveMany(prefs: Record<string, unknown>) {
    Object.assign(data.value, prefs)
    await savePreferences(prefs)
  }

  const zoneHiddenTags = computed<string[]>(() =>
    (data.value.zone_hidden_tags as string[]) ?? []
  )

  const zoneDefaultView = computed<string>(() =>
    (data.value.zone_default_view as string) ?? 'all'
  )

  const theme = computed<string>(() =>
    (data.value.theme as string) ?? 'system'
  )

  return { data, loaded, fetch, save, saveMany, zoneHiddenTags, zoneDefaultView, theme }
})
