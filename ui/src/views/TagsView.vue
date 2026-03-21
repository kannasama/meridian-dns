<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { listTags, renameTag, deleteTag } from '../api/tags'
import type { Tag } from '../types'
import { useNotificationStore } from '../stores/notification'
import { useConfirmAction } from '../composables/useConfirm'

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const tags = ref<Tag[]>([])
const loading = ref(false)
const editingTag = ref<Tag | null>(null)
const editName = ref('')
const showRenameDialog = ref(false)

async function loadTags() {
  loading.value = true
  try {
    tags.value = await listTags()
  } catch {
    notify.error('Failed to load tags')
  } finally {
    loading.value = false
  }
}

function openRename(tag: Tag) {
  editingTag.value = tag
  editName.value = tag.name
  showRenameDialog.value = true
}

async function submitRename() {
  if (!editingTag.value) return
  try {
    await renameTag(editingTag.value.id, editName.value)
    notify.success('Tag renamed')
    showRenameDialog.value = false
    await loadTags()
  } catch {
    notify.error('Failed to rename tag')
  }
}

function handleDelete(tag: Tag) {
  confirmDelete(`Delete tag "${tag.name}"? It will be removed from all zones.`, async () => {
    try {
      await deleteTag(tag.id)
      notify.success('Tag deleted')
      await loadTags()
    } catch {
      notify.error('Failed to delete tag')
    }
  })
}

onMounted(loadTags)
</script>

<template>
  <div class="p-4">
    <div class="flex justify-between items-center mb-4">
      <h1 class="text-xl font-semibold">Tags</h1>
    </div>

    <DataTable :value="tags" :loading="loading" class="text-sm" size="small" stripedRows>
      <Column field="name" header="Name" sortable />
      <Column field="zone_count" header="Zone Count" sortable />
      <Column header="Created">
        <template #body="{ data }">
          {{ new Date(data.created_at * 1000).toLocaleDateString() }}
        </template>
      </Column>
      <Column header="Actions" style="width: 10rem; text-align: right">
        <template #body="{ data }">
          <div class="action-buttons">
            <Button size="small" label="Rename" class="mr-2" @click="openRename(data)" />
            <Button size="small" severity="danger" label="Delete" @click="handleDelete(data)" />
          </div>
        </template>
      </Column>
    </DataTable>

    <Dialog v-model:visible="showRenameDialog" header="Rename Tag" modal>
      <div class="dialog-body">
        <InputText v-model="editName" placeholder="Tag name" class="w-full" />
        <div class="flex justify-end gap-2">
          <Button label="Cancel" severity="secondary" @click="showRenameDialog = false" />
          <Button label="Save" @click="submitRename" />
        </div>
      </div>
    </Dialog>
  </div>
</template>

<style scoped>
.p-4 {
  padding: 1rem;
}

.flex {
  display: flex;
}

.justify-between {
  justify-content: space-between;
}

.justify-end {
  justify-content: flex-end;
}

.items-center {
  align-items: center;
}

.mb-4 {
  margin-bottom: 1rem;
}

.text-xl {
  font-size: 1.25rem;
}

.font-semibold {
  font-weight: 600;
}

.text-sm {
  font-size: 0.875rem;
}

.action-buttons {
  display: flex;
  justify-content: flex-end;
  gap: 0.25rem;
}

.mr-2 {
  margin-right: 0.5rem;
}

.w-full {
  width: 100%;
}

.dialog-body {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding: 0.5rem 0;
  min-width: 20rem;
}

.gap-2 {
  gap: 0.5rem;
}
</style>
