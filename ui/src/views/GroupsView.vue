<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Drawer from 'primevue/drawer'
import InputText from 'primevue/inputtext'
import Textarea from 'primevue/textarea'
import Select from 'primevue/select'
import Tag from 'primevue/tag'
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useNotificationStore } from '../stores/notification'
import * as groupApi from '../api/groups'
import * as roleApi from '../api/roles'
import type { Group, GroupDetail, Role } from '../types'

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const groups = ref<Group[]>([])
const loading = ref(true)

const drawerVisible = ref(false)
const editingId = ref<number | null>(null)
const expandedRows = ref({})

const roles = ref<Role[]>([])

const form = ref({
  name: '',
  description: '',
  role_id: null as number | null,
})

const groupDetails = ref<Map<number, GroupDetail>>(new Map())

async function fetchGroups() {
  loading.value = true
  try {
    groups.value = await groupApi.listGroups()
  } finally {
    loading.value = false
  }
}

async function fetchRoles() {
  try {
    roles.value = await roleApi.listRoles()
  } catch { /* ignore */ }
}

function openCreate() {
  editingId.value = null
  form.value = { name: '', description: '', role_id: null }
  drawerVisible.value = true
}

function openEdit(group: Group) {
  editingId.value = group.id
  form.value = { name: group.name, description: group.description, role_id: group.role_id }
  drawerVisible.value = true
}

async function handleSubmit() {
  try {
    const payload = {
      name: form.value.name,
      description: form.value.description,
      role_id: form.value.role_id!,
    }
    if (editingId.value !== null) {
      await groupApi.updateGroup(editingId.value, payload)
      notify.success('Group updated')
    } else {
      await groupApi.createGroup(payload)
      notify.success('Group created')
    }
    drawerVisible.value = false
    await fetchGroups()
  } catch (e: any) {
    notify.error(e.message || 'Failed to save group')
  }
}

function handleDelete(group: Group) {
  confirmDelete(`Delete group "${group.name}"?`, async () => {
    try {
      await groupApi.deleteGroup(group.id)
      notify.success('Group deleted')
      await fetchGroups()
    } catch (e: any) {
      notify.error(e.message || 'Failed to delete group')
    }
  })
}

async function onRowExpand(event: any) {
  const id = event.data.id
  if (!groupDetails.value.has(id)) {
    try {
      const detail = await groupApi.getGroup(id)
      groupDetails.value.set(id, detail)
    } catch { /* ignore */ }
  }
}

onMounted(() => {
  fetchGroups()
  fetchRoles()
})
</script>

<template>
  <div>
    <PageHeader title="Groups" subtitle="Group management">
      <Button label="Add Group" icon="pi pi-plus" @click="openCreate" />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="groups.length === 0"
      icon="pi pi-users"
      message="No groups yet. Create your first group."
    >
      <Button label="Add Group" icon="pi pi-plus" @click="openCreate" />
    </EmptyState>

    <DataTable
      v-else
      :value="groups"
      v-model:expandedRows="expandedRows"
      dataKey="id"
      size="small"
      paginator
      :rows="25"
      sortField="name"
      :sortOrder="1"
      stripedRows
      @rowExpand="onRowExpand"
    >
      <Column expander style="width: 3rem" />
      <Column field="name" header="Name" sortable>
        <template #body="{ data }">
          <span class="font-mono">{{ data.name }}</span>
        </template>
      </Column>
      <Column field="description" header="Description" />
      <Column field="role_name" header="Role" sortable style="width: 10rem">
        <template #body="{ data }">
          <Tag :value="data.role_name" severity="info" />
        </template>
      </Column>
      <Column field="member_count" header="Members" sortable style="width: 6rem" />
      <Column header="Actions" style="width: 6rem; text-align: right">
        <template #body="{ data }">
          <div class="action-buttons">
            <Button icon="pi pi-pencil" text rounded size="small" v-tooltip.top="'Edit'" @click="openEdit(data)" />
            <Button icon="pi pi-trash" text rounded size="small" severity="danger" v-tooltip.top="'Delete'" @click="handleDelete(data)" />
          </div>
        </template>
      </Column>
      <template #expansion="{ data }">
        <div class="expansion-content">
          <h4 class="expansion-title">Members</h4>
          <div v-if="groupDetails.get(data.id)?.members?.length" class="members-list">
            <div v-for="m in groupDetails.get(data.id)!.members" :key="m.user_id" class="member-row">
              <Tag :value="m.username" severity="secondary" />
            </div>
          </div>
          <span v-else class="text-surface-400 text-sm">No members</span>
        </div>
      </template>
    </DataTable>

    <Drawer v-model:visible="drawerVisible" :header="editingId ? 'Edit Group' : 'Add Group'" position="right" class="w-25rem">
      <form @submit.prevent="handleSubmit" class="drawer-form">
        <div class="field">
          <label>Name</label>
          <InputText v-model="form.name" class="w-full" />
        </div>
        <div class="field">
          <label>Description</label>
          <Textarea v-model="form.description" class="w-full" rows="3" />
        </div>
        <div class="field">
          <label>Role</label>
          <Select v-model="form.role_id" :options="roles" optionLabel="name" optionValue="id" class="w-full" placeholder="Select a role" />
        </div>
        <Button type="submit" :label="editingId ? 'Save' : 'Create'" class="w-full" />
      </form>
    </Drawer>
  </div>
</template>

<style scoped>
.skeleton-table { display: flex; flex-direction: column; gap: 0.5rem; }
.mb-2 { margin-bottom: 0.5rem; }
.ml-1 { margin-left: 0.25rem; }
.text-surface-400 { color: var(--p-surface-400); }
.text-sm { font-size: 0.875rem; }
.action-buttons { display: flex; justify-content: flex-end; gap: 0.25rem; }
.drawer-form { display: flex; flex-direction: column; gap: 1rem; }
.field { display: flex; flex-direction: column; gap: 0.375rem; }
.field label { font-size: 0.875rem; font-weight: 500; }
.w-full { width: 100%; }
.w-25rem { width: 25rem; }
.expansion-content { padding: 0.75rem 1rem; }
.expansion-title { font-size: 0.875rem; font-weight: 600; margin: 0 0 0.5rem; }
.members-list { display: flex; flex-direction: column; gap: 0.375rem; }
.member-row { display: flex; align-items: center; gap: 0.25rem; }
</style>
