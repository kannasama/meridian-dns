<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Textarea from 'primevue/textarea'
import Select from 'primevue/select'
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import * as snippetApi from '../api/snippets'
import type { Snippet, SnippetRecord, SnippetCreate } from '../api/snippets'

const { isAdmin, isOperator } = useRole()
const canEdit = computed(() => isAdmin.value || isOperator.value)

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const snippets = ref<Snippet[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)

const form = ref<SnippetCreate>({ name: '', description: '', records: [] })

const recordTypes = ['A', 'AAAA', 'CNAME', 'MX', 'TXT', 'SRV', 'NS', 'PTR', 'CAA']

async function fetchSnippets() {
  loading.value = true
  try {
    snippets.value = await snippetApi.listSnippets()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load snippets')
  } finally {
    loading.value = false
  }
}

function openCreate() {
  editingId.value = null
  form.value = { name: '', description: '', records: [] }
  dialogVisible.value = true
}

async function openEdit(snippet: Snippet) {
  editingId.value = snippet.id
  try {
    const full = await snippetApi.getSnippet(snippet.id)
    form.value = { name: full.name, description: full.description, records: [...full.records] }
    dialogVisible.value = true
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load snippet')
  }
}

function addRecord() {
  form.value.records.push({
    name: '',
    type: 'A',
    ttl: 300,
    value_template: '',
    priority: 0,
    sort_order: form.value.records.length,
  })
}

function removeRecord(idx: number) {
  form.value.records.splice(idx, 1)
  form.value.records.forEach((r: SnippetRecord, i: number) => {
    r.sort_order = i
  })
}

async function save() {
  saving.value = true
  try {
    if (editingId.value === null) {
      await snippetApi.createSnippet(form.value)
      notify.success('Snippet created')
    } else {
      await snippetApi.updateSnippet(editingId.value, form.value)
      notify.success('Snippet updated')
    }
    dialogVisible.value = false
    await fetchSnippets()
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : 'Save failed'
    notify.error(msg)
  } finally {
    saving.value = false
  }
}

function handleDelete(snippet: Snippet) {
  confirmDelete(`Delete snippet "${snippet.name}"?`, async () => {
    try {
      await snippetApi.deleteSnippet(snippet.id)
      notify.success('Snippet deleted')
      await fetchSnippets()
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : 'Delete failed'
      notify.error(msg)
    }
  })
}

onMounted(fetchSnippets)
</script>

<template>
  <div>
    <PageHeader title="Snippets" subtitle="Reusable DNS record templates">
      <Button
        v-if="canEdit"
        label="New Snippet"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="snippets.length === 0"
      icon="pi pi-copy"
      message="No snippets yet. Create your first snippet."
    >
      <Button
        v-if="canEdit"
        label="New Snippet"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </EmptyState>

    <DataTable
      v-else
      :value="snippets"
      size="small"
      paginator
      :rows="25"
      :rowsPerPageOptions="[25, 50, 100]"
      sortField="name"
      :sortOrder="1"
      stripedRows
    >
      <Column field="name" header="Name" sortable>
        <template #body="{ data }">
          <span class="font-mono">{{ data.name }}</span>
        </template>
      </Column>
      <Column field="description" header="Description">
        <template #body="{ data }">
          <span class="text-surface-400">{{ data.description || '—' }}</span>
        </template>
      </Column>
      <Column header="Actions" style="width: 6rem; text-align: right" v-if="canEdit">
        <template #body="{ data }">
          <div class="action-buttons">
            <Button
              icon="pi pi-pencil"
              text
              rounded
              size="small"
              aria-label="Edit"
              v-tooltip.top="'Edit'"
              @click="openEdit(data)"
            />
            <Button
              icon="pi pi-trash"
              text
              rounded
              size="small"
              severity="danger"
              aria-label="Delete"
              v-tooltip.top="'Delete'"
              @click="handleDelete(data)"
            />
          </div>
        </template>
      </Column>
    </DataTable>

    <Dialog
      v-model:visible="dialogVisible"
      :header="editingId !== null ? 'Edit Snippet' : 'New Snippet'"
      modal
      class="w-40rem"
    >
      <form @submit.prevent="save" class="dialog-form">
        <div class="field">
          <label for="snip-name">Name</label>
          <InputText id="snip-name" v-model="form.name" class="w-full" required />
        </div>
        <div class="field">
          <label for="snip-description">Description</label>
          <Textarea id="snip-description" v-model="form.description" class="w-full" rows="2" />
        </div>

        <div class="records-section">
          <div class="records-header">
            <span class="records-title">Records</span>
            <Button
              label="Add Record"
              icon="pi pi-plus"
              size="small"
              text
              type="button"
              @click="addRecord"
            />
          </div>

          <div v-if="form.records.length === 0" class="records-empty">
            No records. Click "Add Record" to add one.
          </div>

          <div v-else class="records-list">
            <div
              v-for="(record, idx) in form.records"
              :key="idx"
              class="record-row"
            >
              <div class="record-row-header">
                <span class="record-index">Record {{ idx + 1 }}</span>
                <Button
                  icon="pi pi-times"
                  text
                  rounded
                  size="small"
                  severity="danger"
                  type="button"
                  aria-label="Remove record"
                  @click="removeRecord(idx)"
                />
              </div>
              <div class="record-fields">
                <div class="field">
                  <label>Name</label>
                  <InputText v-model="record.name" class="w-full" placeholder="@ or subdomain" />
                </div>
                <div class="field-row">
                  <div class="field">
                    <label>Type</label>
                    <Select
                      v-model="record.type"
                      :options="recordTypes"
                      class="w-full"
                    />
                  </div>
                  <div class="field">
                    <label>TTL</label>
                    <InputNumber v-model="record.ttl" :min="1" class="w-full" />
                  </div>
                  <div class="field">
                    <label>Priority</label>
                    <InputNumber v-model="record.priority" :min="0" class="w-full" />
                  </div>
                </div>
                <div class="field">
                  <label>Value Template</label>
                  <InputText
                    v-model="record.value_template"
                    class="w-full font-mono"
                    placeholder="e.g. {{ip}} or 1.2.3.4"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>

        <Button
          type="submit"
          :label="editingId !== null ? 'Save' : 'Create'"
          :loading="saving"
          class="w-full"
        />
      </form>
    </Dialog>
  </div>
</template>

<style scoped>
.skeleton-table {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.mb-2 {
  margin-bottom: 0.5rem;
}

.action-buttons {
  display: flex;
  justify-content: flex-end;
  gap: 0.25rem;
}

.dialog-form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 0.375rem;
}

.field label {
  font-size: 0.875rem;
  font-weight: 500;
}

.field-row {
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 0.5rem;
}

.w-full {
  width: 100%;
}

.records-section {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.records-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.records-title {
  font-size: 0.875rem;
  font-weight: 600;
}

.records-empty {
  font-size: 0.8125rem;
  color: var(--p-surface-400);
  padding: 0.5rem 0;
}

.records-list {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.record-row {
  border: 1px solid var(--p-surface-200);
  border-radius: 4px;
  padding: 0.75rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.record-row-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.record-index {
  font-size: 0.75rem;
  font-weight: 600;
  color: var(--p-surface-500);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.record-fields {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.font-mono {
  font-family: monospace;
}
</style>
