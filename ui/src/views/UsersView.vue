<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Drawer from 'primevue/drawer'
import Dialog from 'primevue/dialog'
import InputText from 'primevue/inputtext'
import Password from 'primevue/password'
import MultiSelect from 'primevue/multiselect'
import ToggleSwitch from 'primevue/toggleswitch'
import Tag from 'primevue/tag'
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useNotificationStore } from '../stores/notification'
import * as userApi from '../api/users'
import * as groupApi from '../api/groups'
import type { UserDetail, Group } from '../types'

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const users = ref<UserDetail[]>([])
const groups = ref<Group[]>([])
const loading = ref(true)

const drawerVisible = ref(false)
const editingId = ref<number | null>(null)

const createForm = ref({
  username: '',
  email: '',
  password: '',
  group_ids: [] as number[],
  force_password_change: true,
})

const editForm = ref({
  email: '',
  is_active: true,
  group_ids: [] as number[],
})

const resetDialogVisible = ref(false)
const resetUserId = ref<number | null>(null)
const resetPassword = ref('')

async function fetchAll() {
  loading.value = true
  try {
    const [u, g] = await Promise.all([userApi.listUsers(), groupApi.listGroups()])
    users.value = u
    groups.value = g
  } finally {
    loading.value = false
  }
}

function openCreate() {
  editingId.value = null
  createForm.value = { username: '', email: '', password: '', group_ids: [], force_password_change: true }
  drawerVisible.value = true
}

function openEdit(user: UserDetail) {
  editingId.value = user.id
  editForm.value = {
    email: user.email,
    is_active: user.is_active,
    group_ids: user.groups.map(g => g.id),
  }
  drawerVisible.value = true
}

async function handleSubmit() {
  try {
    if (editingId.value !== null) {
      await userApi.updateUser(editingId.value, editForm.value)
      notify.success('User updated')
    } else {
      await userApi.createUser(createForm.value)
      notify.success('User created')
    }
    drawerVisible.value = false
    await fetchAll()
  } catch (e: any) {
    notify.error(e.message || 'Failed to save user')
  }
}

function handleDelete(user: UserDetail) {
  confirmDelete(`Deactivate user "${user.username}"?`, async () => {
    try {
      await userApi.deleteUser(user.id)
      notify.success('User deactivated')
      await fetchAll()
    } catch (e: any) {
      notify.error(e.message || 'Failed to deactivate user')
    }
  })
}

function openResetDialog(user: UserDetail) {
  resetUserId.value = user.id
  resetPassword.value = ''
  resetDialogVisible.value = true
}

async function handleResetPassword() {
  if (!resetUserId.value) return
  try {
    await userApi.resetPassword(resetUserId.value, resetPassword.value)
    notify.success('Password reset successfully')
    resetDialogVisible.value = false
  } catch (e: any) {
    notify.error(e.message || 'Failed to reset password')
  }
}

onMounted(fetchAll)
</script>

<template>
  <div>
    <PageHeader title="Users" subtitle="User management">
      <Button label="Add User" icon="pi pi-plus" @click="openCreate" />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="users.length === 0"
      icon="pi pi-users"
      message="No users found."
    >
      <Button label="Add User" icon="pi pi-plus" @click="openCreate" />
    </EmptyState>

    <DataTable
      v-else
      :value="users"
      size="small"
      paginator
      :rows="25"
      sortField="username"
      :sortOrder="1"
      stripedRows
    >
      <Column field="username" header="Username" sortable>
        <template #body="{ data }">
          <span class="font-mono">{{ data.username }}</span>
        </template>
      </Column>
      <Column field="display_name" header="Display Name" sortable />
      <Column field="email" header="Email" sortable />
      <Column field="auth_method" header="Auth" sortable>
        <template #body="{ data }">
          <Tag :value="data.auth_method" severity="secondary" />
        </template>
      </Column>
      <Column field="is_active" header="Active" sortable>
        <template #body="{ data }">
          <Tag :value="data.is_active ? 'Active' : 'Inactive'"
            :severity="data.is_active ? 'success' : 'danger'" />
        </template>
      </Column>
      <Column header="Groups">
        <template #body="{ data }">
          <Tag v-for="g in data.groups" :key="g.id" :value="g.name" severity="info" class="mr-1" />
        </template>
      </Column>
      <Column header="Actions" style="width: 8rem; text-align: right">
        <template #body="{ data }">
          <div class="action-buttons">
            <Button icon="pi pi-pencil" text rounded size="small" v-tooltip.top="'Edit'" @click="openEdit(data)" />
            <Button icon="pi pi-key" text rounded size="small" v-tooltip.top="'Reset Password'" @click="openResetDialog(data)" />
            <Button icon="pi pi-trash" text rounded size="small" severity="danger" v-tooltip.top="'Deactivate'" @click="handleDelete(data)" />
          </div>
        </template>
      </Column>
    </DataTable>

    <Drawer v-model:visible="drawerVisible" :header="editingId ? 'Edit User' : 'Add User'" position="right" class="w-25rem">
      <form @submit.prevent="handleSubmit" class="drawer-form">
        <template v-if="editingId === null">
          <div class="field">
            <label>Username</label>
            <InputText v-model="createForm.username" class="w-full" />
          </div>
          <div class="field">
            <label>Email</label>
            <InputText v-model="createForm.email" class="w-full" />
          </div>
          <div class="field">
            <label>Password</label>
            <Password v-model="createForm.password" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
          </div>
          <div class="field">
            <label>Groups</label>
            <MultiSelect v-model="createForm.group_ids" :options="groups" optionLabel="name" optionValue="id" class="w-full" placeholder="Select groups" />
          </div>
          <div class="field flex-row">
            <ToggleSwitch v-model="createForm.force_password_change" />
            <label>Force password change on login</label>
          </div>
        </template>
        <template v-else>
          <div class="field">
            <label>Email</label>
            <InputText v-model="editForm.email" class="w-full" />
          </div>
          <div class="field flex-row">
            <ToggleSwitch v-model="editForm.is_active" />
            <label>Active</label>
          </div>
          <div class="field">
            <label>Groups</label>
            <MultiSelect v-model="editForm.group_ids" :options="groups" optionLabel="name" optionValue="id" class="w-full" placeholder="Select groups" />
          </div>
        </template>
        <Button type="submit" :label="editingId ? 'Save' : 'Create'" class="w-full" />
      </form>
    </Drawer>

    <Dialog v-model:visible="resetDialogVisible" header="Reset Password" :style="{ width: '24rem' }" modal>
      <form @submit.prevent="handleResetPassword" class="drawer-form">
        <div class="field">
          <label>New Password</label>
          <Password v-model="resetPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
        </div>
        <small class="text-surface-400">User will be required to change password on next login.</small>
        <Button type="submit" label="Reset Password" class="w-full mt-3" />
      </form>
    </Dialog>
  </div>
</template>

<style scoped>
.skeleton-table { display: flex; flex-direction: column; gap: 0.5rem; }
.mb-2 { margin-bottom: 0.5rem; }
.mr-1 { margin-right: 0.25rem; }
.mt-3 { margin-top: 0.75rem; }
.text-surface-400 { color: var(--p-surface-400); }
.action-buttons { display: flex; justify-content: flex-end; gap: 0.25rem; }
.drawer-form { display: flex; flex-direction: column; gap: 1rem; }
.field { display: flex; flex-direction: column; gap: 0.375rem; }
.field label { font-size: 0.875rem; font-weight: 500; }
.field.flex-row { flex-direction: row; align-items: center; gap: 0.5rem; }
.w-full { width: 100%; }
.w-25rem { width: 25rem; }
</style>
