<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { listTags, renameTag, deleteTag } from '../api/tags'
import type { Tag } from '../types'
import { useNotification } from '../stores/notification'
import { useConfirm } from '../composables/useConfirm'

const notify = useNotification()
const confirm = useConfirm()

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

async function confirmDelete(tag: Tag) {
  const ok = await confirm.danger(
    'Delete Tag',
    `Delete tag "${tag.name}"? It will be removed from all zones.`,
  )
  if (!ok) return
  try {
    await deleteTag(tag.id)
    notify.success('Tag deleted')
    await loadTags()
  } catch {
    notify.error('Failed to delete tag')
  }
}

onMounted(loadTags)
</script>

<template>
  <div class="p-4">
    <div class="flex justify-between items-center mb-4">
      <h1 class="text-xl font-semibold">Tags</h1>
    </div>

    <DataTable :value="tags" :loading="loading" class="text-sm">
      <Column field="name" header="Name" />
      <Column field="zone_count" header="Zone Count" />
      <Column header="Created">
        <template #body="{ data }">
          {{ new Date(data.created_at * 1000).toLocaleDateString() }}
        </template>
      </Column>
      <Column header="Actions">
        <template #body="{ data }">
          <Button size="small" label="Rename" class="mr-2" @click="openRename(data)" />
          <Button size="small" severity="danger" label="Delete" @click="confirmDelete(data)" />
        </template>
      </Column>
    </DataTable>

    <Dialog v-model:visible="showRenameDialog" header="Rename Tag" modal>
      <div class="p-4 flex flex-col gap-3">
        <InputText v-model="editName" placeholder="Tag name" class="w-full" />
        <div class="flex justify-end gap-2">
          <Button label="Cancel" severity="secondary" @click="showRenameDialog = false" />
          <Button label="Save" @click="submitRename" />
        </div>
      </div>
    </Dialog>
  </div>
</template>
