<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import InputText from 'primevue/inputtext'
import Password from 'primevue/password'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Dialog from 'primevue/dialog'
import Select from 'primevue/select'
import PageHeader from '../components/shared/PageHeader.vue'
import { useAuthStore } from '../stores/auth'
import { useNotificationStore } from '../stores/notification'
import { useThemeStore, type AccentColor } from '../stores/theme'
import { useConfirmAction } from '../composables/useConfirm'
import { me, updateProfile, changePassword } from '../api/auth'
import { listApiKeys, createApiKey, revokeApiKey } from '../api/apikeys'
import { darkPresets, lightPresets } from '../theme/presets'
import type { ApiKeyEntry } from '../types'

const auth = useAuthStore()
const theme = useThemeStore()

const fontFamilyOptions = [
  { label: 'System Default', value: 'system' },
  { label: 'Inter', value: 'inter' },
  { label: 'Roboto', value: 'Roboto' },
  { label: 'Source Sans 3', value: 'Source Sans 3' },
]

const fontSizeOptions = ['12', '13', '14', '15', '16'].map(s => ({ label: `${s}px`, value: s }))
const gridFontSizeOptions = ['11', '12', '13', '14', '15'].map(s => ({ label: `${s}px`, value: s }))

const darkPresetOptions = computed(() => darkPresets.map(p => ({ label: p.label, value: p.name })))
const lightPresetOptions = computed(() => lightPresets.map(p => ({ label: p.label, value: p.name })))

const colorRows: { name: AccentColor; bg: string }[][] = [
  [
    { name: 'noir', bg: '#71717a' },
    { name: 'emerald', bg: '#10b981' },
    { name: 'green', bg: '#22c55e' },
    { name: 'lime', bg: '#84cc16' },
    { name: 'orange', bg: '#f97316' },
    { name: 'amber', bg: '#f59e0b' },
    { name: 'yellow', bg: '#eab308' },
    { name: 'cyan', bg: '#06b6d4' },
  ],
  [
    { name: 'sky', bg: '#0ea5e9' },
    { name: 'blue', bg: '#3b82f6' },
    { name: 'indigo', bg: '#6366f1' },
    { name: 'violet', bg: '#8b5cf6' },
    { name: 'purple', bg: '#a855f7' },
    { name: 'fuchsia', bg: '#d946ef' },
    { name: 'pink', bg: '#ec4899' },
    { name: 'rose', bg: '#f43f5e' },
  ],
]
const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const email = ref('')
const currentPassword = ref('')
const newPassword = ref('')
const confirmNewPassword = ref('')

const apiKeys = ref<ApiKeyEntry[]>([])
const createKeyVisible = ref(false)
const newKeyLabel = ref('')
const createdKeyValue = ref<string | null>(null)

onMounted(async () => {
  try {
    const user = await me()
    email.value = user.email ?? ''
  } catch { /* ignore */ }
  await fetchApiKeys()
})

async function fetchApiKeys() {
  try {
    apiKeys.value = await listApiKeys()
  } catch { /* ignore */ }
}

async function handleProfileSave() {
  try {
    await updateProfile({ email: email.value })
    notify.success('Profile updated')
  } catch (e: any) {
    notify.error(e.message || 'Failed to update profile')
  }
}

async function handlePasswordChange() {
  if (newPassword.value !== confirmNewPassword.value) {
    notify.error('Passwords do not match')
    return
  }
  try {
    await changePassword({
      current_password: currentPassword.value,
      new_password: newPassword.value,
    })
    notify.success('Password changed successfully')
    currentPassword.value = ''
    newPassword.value = ''
    confirmNewPassword.value = ''
    await auth.hydrate()
  } catch (e: any) {
    notify.error(e.message || 'Failed to change password')
  }
}

function openCreateKey() {
  newKeyLabel.value = ''
  createdKeyValue.value = null
  createKeyVisible.value = true
}

async function handleCreateKey() {
  try {
    const result = await createApiKey({ description: newKeyLabel.value })
    createdKeyValue.value = result.key
    notify.success('API key created')
    await fetchApiKeys()
  } catch (e: any) {
    notify.error(e.message || 'Failed to create API key')
  }
}

function handleRevokeKey(key: ApiKeyEntry) {
  confirmDelete(`Revoke API key "${key.description}"?`, async () => {
    try {
      await revokeApiKey(key.id)
      notify.success('API key revoked')
      await fetchApiKeys()
    } catch (e: any) {
      notify.error(e.message || 'Failed to revoke API key')
    }
  })
}

function copyKey() {
  if (createdKeyValue.value) {
    navigator.clipboard.writeText(createdKeyValue.value)
    notify.success('Copied to clipboard')
  }
}
</script>

<template>
  <div>
    <PageHeader title="Profile" subtitle="Manage your account" />

    <div class="profile-layout">
      <!-- Left column: Account & Password -->
      <div class="profile-column-left">
        <section class="profile-section">
          <h3 class="section-title"><i class="pi pi-user section-icon" /> Profile</h3>
          <form @submit.prevent="handleProfileSave" class="section-form">
            <div class="field">
              <label>Username</label>
              <InputText :modelValue="auth.user?.username" disabled class="w-full" />
            </div>
            <div class="field">
              <label>Email</label>
              <InputText v-model="email" class="w-full" />
            </div>
            <div class="field">
              <label>Role</label>
              <InputText :modelValue="auth.role" disabled class="w-full" />
            </div>
            <Button type="submit" label="Save Profile" class="align-self-start" />
          </form>
        </section>

        <section class="profile-section">
          <h3 class="section-title"><i class="pi pi-lock section-icon" /> Change Password</h3>
          <form @submit.prevent="handlePasswordChange" class="section-form">
            <div class="field">
              <label>Current Password</label>
              <Password v-model="currentPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
            </div>
            <div class="field">
              <label>New Password</label>
              <Password v-model="newPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
            </div>
            <div class="field">
              <label>Confirm New Password</label>
              <Password v-model="confirmNewPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
            </div>
            <Button type="submit" label="Change Password" class="align-self-start" />
          </form>
        </section>
      </div>

      <!-- Right column: Appearance -->
      <div class="profile-column-right">
        <section class="profile-section">
          <h3 class="section-title"><i class="pi pi-palette section-icon" /> Appearance</h3>

          <div class="appearance-grid">
            <div class="field">
              <label>Dark Theme</label>
              <Select
                :modelValue="theme.darkTheme"
                @update:modelValue="theme.setDarkTheme($event)"
                :options="darkPresetOptions"
                optionLabel="label"
                optionValue="value"
                class="w-full"
              />
            </div>
            <div class="field">
              <label>Light Theme</label>
              <Select
                :modelValue="theme.lightTheme"
                @update:modelValue="theme.setLightTheme($event)"
                :options="lightPresetOptions"
                optionLabel="label"
                optionValue="value"
                class="w-full"
              />
            </div>
          </div>

          <div class="field mt-1">
            <label>Accent Color</label>
            <div class="color-grid">
              <div v-for="(row, ri) in colorRows" :key="ri" class="color-row">
                <button
                  v-for="c in row"
                  :key="c.name"
                  class="color-swatch"
                  :class="{ active: theme.accent === c.name }"
                  :style="{ backgroundColor: c.bg }"
                  :title="c.name"
                  @click="theme.setAccent(c.name)"
                >
                  <i v-if="theme.accent === c.name" class="pi pi-check swatch-check" />
                </button>
              </div>
            </div>
          </div>

          <div class="appearance-grid mt-1">
            <div class="field">
              <label>Font Family</label>
              <Select
                :modelValue="theme.fontFamily"
                @update:modelValue="theme.setFontFamily($event)"
                :options="fontFamilyOptions"
                optionLabel="label"
                optionValue="value"
                class="w-full"
              />
            </div>
            <div class="field">
              <label>Font Size</label>
              <Select
                :modelValue="theme.fontSize"
                @update:modelValue="theme.setFontSize($event)"
                :options="fontSizeOptions"
                optionLabel="label"
                optionValue="value"
                class="w-full"
              />
            </div>
            <div class="field">
              <label>Table Font Size</label>
              <Select
                :modelValue="theme.gridFontSize"
                @update:modelValue="theme.setGridFontSize($event)"
                :options="gridFontSizeOptions"
                optionLabel="label"
                optionValue="value"
                class="w-full"
              />
            </div>
          </div>
        </section>
      </div>

      <!-- Full-width: API Keys -->
      <section class="profile-section profile-full-width">
        <div class="flex align-items-center justify-content-between mb-3">
          <h3 class="section-title" style="margin: 0"><i class="pi pi-key section-icon" /> API Keys</h3>
          <Button label="Create Key" icon="pi pi-plus" size="small" @click="openCreateKey" />
        </div>
        <DataTable :value="apiKeys" size="small" stripedRows>
          <Column field="description" header="Label" />
          <Column field="prefix" header="Prefix">
            <template #body="{ data }">
              <span class="font-mono">{{ data.prefix }}...</span>
            </template>
          </Column>
          <Column field="created_at" header="Created" />
          <Column field="expires_at" header="Expires">
            <template #body="{ data }">
              {{ data.expires_at || 'Never' }}
            </template>
          </Column>
          <Column header="" style="width: 3rem">
            <template #body="{ data }">
              <Button icon="pi pi-trash" text rounded size="small" severity="danger" v-tooltip.top="'Revoke'" @click="handleRevokeKey(data)" />
            </template>
          </Column>
        </DataTable>
      </section>
    </div>

    <Dialog v-model:visible="createKeyVisible" header="Create API Key" :style="{ width: '28rem' }" modal>
      <div v-if="!createdKeyValue" class="drawer-form">
        <div class="field">
          <label>Label</label>
          <InputText v-model="newKeyLabel" class="w-full" placeholder="e.g. CI/CD pipeline" />
        </div>
        <Button label="Create" class="w-full" @click="handleCreateKey" />
      </div>
      <div v-else class="drawer-form">
        <div class="key-display">
          <p class="text-sm mb-2"><strong>Your API key (shown once):</strong></p>
          <div class="key-value">
            <code class="font-mono text-sm">{{ createdKeyValue }}</code>
            <Button icon="pi pi-copy" text size="small" @click="copyKey" v-tooltip.top="'Copy'" />
          </div>
          <small class="text-surface-400">Save this key now. It cannot be retrieved later.</small>
        </div>
        <Button label="Done" class="w-full" @click="createKeyVisible = false" />
      </div>
    </Dialog>
  </div>
</template>

<style scoped>
/* Two-column grid layout: left (account) + right (appearance), API keys full-width below */
.profile-layout {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1.5rem;
  max-width: 72rem;
}
.profile-column-left {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}
.profile-column-right {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}
.profile-full-width {
  grid-column: 1 / -1;
}

/* Fall back to single column on narrow viewports */
@media (max-width: 860px) {
  .profile-layout {
    grid-template-columns: 1fr;
  }
}

.profile-section { background: var(--p-surface-900); border: 1px solid var(--p-surface-700); border-radius: 0.5rem; padding: 1.25rem; }
:root:not(.app-dark) .profile-section { background: var(--p-surface-50); border-color: var(--p-surface-200); }
.section-title { font-size: 1rem; font-weight: 600; margin: 0 0 1rem; color: var(--p-surface-200); }
:root:not(.app-dark) .section-title { color: var(--p-surface-700); }
.section-form { display: flex; flex-direction: column; gap: 1rem; }
.field { display: flex; flex-direction: column; gap: 0.375rem; }
.field label { font-size: 0.875rem; font-weight: 500; }
.w-full { width: 100%; }
.align-self-start { align-self: flex-start; }
.flex { display: flex; }
.align-items-center { align-items: center; }
.justify-content-between { justify-content: space-between; }
.mb-2 { margin-bottom: 0.5rem; }
.mb-3 { margin-bottom: 0.75rem; }
.text-sm { font-size: 0.875rem; }
.text-surface-400 { color: var(--p-surface-400); }
.drawer-form { display: flex; flex-direction: column; gap: 1rem; }
.key-display { background: var(--p-surface-800); border-radius: 0.375rem; padding: 1rem; }
:root:not(.app-dark) .key-display { background: var(--p-surface-100); }
.key-value { display: flex; align-items: center; gap: 0.5rem; word-break: break-all; }
.section-icon { margin-right: 0.5rem; font-size: 1rem; }
.appearance-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(12rem, 1fr)); gap: 1rem; }
.mt-1 { margin-top: 1rem; }
.color-grid { display: flex; flex-direction: column; gap: 0.5rem; }
.color-row { display: flex; gap: 0.5rem; }
.color-swatch {
  width: 1.5rem;
  height: 1.5rem;
  border-radius: 50%;
  border: 2px solid transparent;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  transition: transform 0.15s;
}
.color-swatch:hover { transform: scale(1.2); }
.color-swatch.active { border-color: var(--p-surface-0); box-shadow: 0 0 0 1px var(--p-surface-500); }
.swatch-check { font-size: 0.625rem; color: white; text-shadow: 0 1px 2px rgba(0, 0, 0, 0.5); }
</style>
