<script setup lang="ts">
import { onMounted, ref } from 'vue'
import InputText from 'primevue/inputtext'
import Password from 'primevue/password'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Dialog from 'primevue/dialog'
import PageHeader from '../components/shared/PageHeader.vue'
import { useAuthStore } from '../stores/auth'
import { useNotificationStore } from '../stores/notification'
import { useConfirmAction } from '../composables/useConfirm'
import { me, updateProfile, changePassword } from '../api/auth'
import { listApiKeys, createApiKey, revokeApiKey } from '../api/apikeys'
import type { ApiKeyEntry } from '../types'

const auth = useAuthStore()
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
    email.value = (user as any).email ?? ''
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

    <div class="profile-sections">
      <section class="profile-section">
        <h3 class="section-title">Profile</h3>
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
        <h3 class="section-title">Change Password</h3>
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

      <section class="profile-section">
        <div class="flex align-items-center justify-content-between mb-3">
          <h3 class="section-title" style="margin: 0">API Keys</h3>
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
.profile-sections { display: flex; flex-direction: column; gap: 2rem; max-width: 36rem; }
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
</style>
