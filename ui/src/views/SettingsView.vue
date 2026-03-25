<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import InputSwitch from 'primevue/inputswitch'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import PageHeader from '../components/shared/PageHeader.vue'
import { listSettings, updateSettings } from '../api/settings'
import { useNotificationStore } from '../stores/notification'
import type { SystemSetting } from '../types'

const notify = useNotificationStore()
const settings = ref<SystemSetting[]>([])
const editValues = ref<Record<string, string>>({})
const loading = ref(false)
const saving = ref(false)

const leftSections = [
  {
    title: 'Application',
    icon: 'pi pi-globe',
    keys: ['app.base_url'],
  },
  {
    title: 'Session & Security',
    icon: 'pi pi-shield',
    keys: [
      'session.absolute_ttl_seconds',
      'session.cleanup_interval_seconds',
      'apikey.cleanup_grace_seconds',
      'apikey.cleanup_interval_seconds',
    ],
  },
  {
    title: 'Deployment',
    icon: 'pi pi-upload',
    keys: ['deployment.retention_count'],
  },
]

const rightSections = [
  {
    title: 'Sync',
    icon: 'pi pi-sync',
    keys: ['sync.check_interval_seconds'],
  },
  {
    title: 'Audit',
    icon: 'pi pi-history',
    keys: [
      'audit.retention_days',
      'audit.purge_interval_seconds',
      'audit.stdout',
    ],
  },
  {
    title: 'System Logs',
    icon: 'pi pi-list',
    keys: ['system_log.retention_days'],
  },
]

const fullWidthSections = [
  {
    title: 'Paths & Infrastructure',
    icon: 'pi pi-folder',
    keys: ['ui.dir', 'migrations.dir', 'audit.db_url', 'http.threads'],
  },
]

const settingsByKey = computed(() => {
  const map: Record<string, SystemSetting> = {}
  for (const s of settings.value) {
    map[s.key] = s
  }
  return map
})

const hasChanges = computed(() => {
  for (const s of settings.value) {
    if (editValues.value[s.key] !== s.value) return true
  }
  return false
})

async function fetchSettings() {
  loading.value = true
  try {
    settings.value = await listSettings()
    editValues.value = {}
    for (const s of settings.value) {
      editValues.value[s.key] = s.value
    }
  } catch {
    notify.error('Failed to load settings')
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
    const changed: Record<string, string> = {}
    for (const s of settings.value) {
      const editVal = editValues.value[s.key] ?? s.value
      if (editVal !== s.value) {
        changed[s.key] = editVal
      }
    }
    if (Object.keys(changed).length === 0) return

    const result = await updateSettings(changed)
    notify.success(`Updated ${result.updated.length} setting(s)`)

    // Check if any restart-required setting was changed
    const restartNeeded = result.updated.some(key => {
      const s = settingsByKey.value[key]
      return s?.restart_required
    })
    if (restartNeeded) {
      notify.add({ severity: 'warn', summary: 'Some changes require a service restart to take effect' })
    }

    await fetchSettings()
  } catch {
    notify.error('Failed to save settings')
  } finally {
    saving.value = false
  }
}

function resetToDefaults() {
  for (const s of settings.value) {
    editValues.value[s.key] = s.default
  }
}

function isIntegerSetting(key: string) {
  return !key.endsWith('.stdout') && !key.includes('.dir') && !key.includes('.db_url') && !key.includes('base_url')
}

function isBooleanSetting(key: string) {
  return key === 'audit.stdout'
}

function isStringSetting(key: string) {
  return key.includes('.dir') || key.includes('.db_url') || key.includes('base_url')
}

onMounted(fetchSettings)
</script>

<template>
  <div class="settings-page">
    <PageHeader title="Settings" subtitle="System configuration">
      <Button
        label="Reset to Defaults"
        icon="pi pi-refresh"
        severity="secondary"
        size="small"
        :disabled="loading"
        @click="resetToDefaults"
      />
      <Button
        label="Save Changes"
        icon="pi pi-check"
        size="small"
        :disabled="!hasChanges || saving"
        :loading="saving"
        @click="save"
      />
    </PageHeader>

    <div v-if="loading" class="loading-state">Loading settings...</div>

    <div v-else class="settings-layout">
      <!-- Left column -->
      <div class="settings-column">
        <section v-for="section in leftSections" :key="section.title" class="settings-section">
          <h3 class="section-title">
            <i :class="section.icon" class="section-icon" />
            {{ section.title }}
          </h3>
          <div class="settings-grid">
            <div
              v-for="key in section.keys"
              :key="key"
              class="setting-field"
            >
              <div class="setting-header">
                <label :for="key" class="setting-label">{{ key }}</label>
                <Tag
                  v-if="settingsByKey[key]?.restart_required"
                  value="Restart required"
                  severity="warn"
                  class="restart-tag"
                />
              </div>
              <p class="setting-description">
                {{ settingsByKey[key]?.description }}
              </p>
              <div class="setting-input">
                <InputSwitch
                  v-if="isBooleanSetting(key)"
                  :modelValue="editValues[key] === 'true'"
                  @update:modelValue="editValues[key] = $event ? 'true' : 'false'"
                />
                <InputNumber
                  v-else-if="isIntegerSetting(key)"
                  :modelValue="Number(editValues[key]) || 0"
                  @update:modelValue="editValues[key] = String($event ?? 0)"
                  :id="key"
                  class="w-full"
                  :useGrouping="false"
                />
                <InputText
                  v-else-if="isStringSetting(key)"
                  v-model="editValues[key]"
                  :id="key"
                  class="w-full"
                  :placeholder="settingsByKey[key]?.default || key === 'app.base_url' ? 'https://dns.example.com' : '(empty)'"
                />
              </div>
              <small class="setting-default">
                Default: {{ settingsByKey[key]?.default || '(empty)' }}
              </small>
            </div>
          </div>
        </section>
      </div>

      <!-- Right column -->
      <div class="settings-column">
        <section v-for="section in rightSections" :key="section.title" class="settings-section">
          <h3 class="section-title">
            <i :class="section.icon" class="section-icon" />
            {{ section.title }}
          </h3>
          <div class="settings-grid">
            <div
              v-for="key in section.keys"
              :key="key"
              class="setting-field"
            >
              <div class="setting-header">
                <label :for="key" class="setting-label">{{ key }}</label>
                <Tag
                  v-if="settingsByKey[key]?.restart_required"
                  value="Restart required"
                  severity="warn"
                  class="restart-tag"
                />
              </div>
              <p class="setting-description">
                {{ settingsByKey[key]?.description }}
              </p>
              <div class="setting-input">
                <InputSwitch
                  v-if="isBooleanSetting(key)"
                  :modelValue="editValues[key] === 'true'"
                  @update:modelValue="editValues[key] = $event ? 'true' : 'false'"
                />
                <InputNumber
                  v-else-if="isIntegerSetting(key)"
                  :modelValue="Number(editValues[key]) || 0"
                  @update:modelValue="editValues[key] = String($event ?? 0)"
                  :id="key"
                  class="w-full"
                  :useGrouping="false"
                />
                <InputText
                  v-else-if="isStringSetting(key)"
                  v-model="editValues[key]"
                  :id="key"
                  class="w-full"
                  :placeholder="settingsByKey[key]?.default || '(empty)'"
                />
              </div>
              <small class="setting-default">
                Default: {{ settingsByKey[key]?.default || '(empty)' }}
              </small>
            </div>
          </div>
        </section>
      </div>

      <!-- Full-width sections -->
      <section
        v-for="section in fullWidthSections"
        :key="section.title"
        class="settings-section settings-full-width"
      >
        <h3 class="section-title">
          <i :class="section.icon" class="section-icon" />
          {{ section.title }}
        </h3>
        <div class="settings-grid settings-grid-wide">
          <div
            v-for="key in section.keys"
            :key="key"
            class="setting-field"
          >
            <div class="setting-header">
              <label :for="key" class="setting-label">{{ key }}</label>
              <Tag
                v-if="settingsByKey[key]?.restart_required"
                value="Restart required"
                severity="warn"
                class="restart-tag"
              />
            </div>
            <p class="setting-description">
              {{ settingsByKey[key]?.description }}
            </p>
            <div class="setting-input">
              <InputSwitch
                v-if="isBooleanSetting(key)"
                :modelValue="editValues[key] === 'true'"
                @update:modelValue="editValues[key] = $event ? 'true' : 'false'"
              />
              <InputNumber
                v-else-if="isIntegerSetting(key)"
                :modelValue="Number(editValues[key]) || 0"
                @update:modelValue="editValues[key] = String($event ?? 0)"
                :id="key"
                class="w-full"
                :useGrouping="false"
              />
              <InputText
                v-else-if="isStringSetting(key)"
                v-model="editValues[key]"
                :id="key"
                class="w-full"
                :placeholder="settingsByKey[key]?.default || '(empty)'"
              />
            </div>
            <small class="setting-default">
              Default: {{ settingsByKey[key]?.default || '(empty)' }}
            </small>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<style scoped>
.settings-page {
  padding: 0;
}

.loading-state {
  color: var(--p-surface-400);
  padding: 2rem;
}

/* Two-column grid layout like ProfileView */
.settings-layout {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1.5rem;
  max-width: 80rem;
}

.settings-column {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.settings-full-width {
  grid-column: 1 / -1;
}

/* Fall back to single column on narrow viewports */
@media (max-width: 860px) {
  .settings-layout {
    grid-template-columns: 1fr;
  }
}

.settings-section {
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.5rem;
  padding: 1.25rem;
}

:root:not(.app-dark) .settings-section {
  background: var(--p-surface-0);
  border-color: var(--p-surface-200);
}

.section-title {
  margin: 0 0 1rem;
  font-size: 1rem;
  font-weight: 600;
  color: var(--p-surface-100);
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

:root:not(.app-dark) .section-title {
  color: var(--p-surface-900);
}

.section-icon {
  font-size: 1rem;
}

.settings-grid {
  display: flex;
  flex-direction: column;
  gap: 1.25rem;
}

.settings-grid-wide {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr));
  gap: 1.25rem;
}

.setting-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.setting-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.setting-label {
  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  font-size: 0.8125rem;
  font-weight: 600;
  color: var(--p-surface-200);
}

:root:not(.app-dark) .setting-label {
  color: var(--p-surface-800);
}

.restart-tag {
  font-size: 0.625rem;
}

.setting-description {
  margin: 0;
  font-size: 0.75rem;
  color: var(--p-surface-400);
  line-height: 1.4;
}

:root:not(.app-dark) .setting-description {
  color: var(--p-surface-500);
}

.setting-input {
  margin-top: 0.25rem;
}

.setting-default {
  font-size: 0.6875rem;
  color: var(--p-surface-500);
}

:root:not(.app-dark) .setting-default {
  color: var(--p-surface-400);
}
</style>
