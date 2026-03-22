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
import Textarea from 'primevue/textarea'
import Select from 'primevue/select'
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import * as templateApi from '../api/templates'
import type { ZoneTemplate, ZoneTemplateCreate } from '../api/templates'
import * as soaPresetApi from '../api/soaPresets'
import type { SoaPreset } from '../api/soaPresets'
import * as snippetApi from '../api/snippets'
import type { Snippet } from '../api/snippets'

const { isAdmin, isOperator } = useRole()
const canEdit = computed(() => isAdmin.value || isOperator.value)

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const templates = ref<ZoneTemplate[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)

const soaPresets = ref<SoaPreset[]>([])
const allSnippets = ref<Snippet[]>([])
const selectedSnippets = ref<Snippet[]>([])

const form = ref<ZoneTemplateCreate>({
  name: '',
  description: '',
  soa_preset_id: null,
  snippet_ids: [],
})

async function fetchTemplates() {
  loading.value = true
  try {
    templates.value = await templateApi.listTemplates()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load templates')
  } finally {
    loading.value = false
  }
}

function soaPresetName(id: number | null): string {
  if (id === null) return '—'
  return soaPresets.value.find(p => p.id === id)?.name ?? '—'
}

function openCreate() {
  editingId.value = null
  form.value = { name: '', description: '', soa_preset_id: null, snippet_ids: [] }
  selectedSnippets.value = []
  dialogVisible.value = true
}

async function openEdit(template: ZoneTemplate) {
  editingId.value = template.id
  try {
    const full = await templateApi.getTemplate(template.id)
    form.value = {
      name: full.name,
      description: full.description,
      soa_preset_id: full.soa_preset_id,
      snippet_ids: full.snippet_ids,
    }
    selectedSnippets.value = full.snippet_ids
      .map(id => allSnippets.value.find(s => s.id === id))
      .filter((s): s is Snippet => s !== undefined)
    dialogVisible.value = true
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load template')
  }
}

function availableSnippets(): Snippet[] {
  const selectedIds = new Set(selectedSnippets.value.map(s => s.id))
  return allSnippets.value.filter(s => !selectedIds.has(s.id))
}

function addSnippet(snippet: Snippet) {
  selectedSnippets.value = [...selectedSnippets.value, snippet]
}

function removeSnippet(idx: number) {
  selectedSnippets.value = selectedSnippets.value.filter((_, i) => i !== idx)
}

function moveSnippetUp(idx: number) {
  if (idx === 0) return
  const arr = [...selectedSnippets.value]
  const tmp = arr[idx - 1]!
  arr[idx - 1] = arr[idx]!
  arr[idx] = tmp
  selectedSnippets.value = arr
}

function moveSnippetDown(idx: number) {
  if (idx === selectedSnippets.value.length - 1) return
  const arr = [...selectedSnippets.value]
  const tmp = arr[idx]!
  arr[idx] = arr[idx + 1]!
  arr[idx + 1] = tmp
  selectedSnippets.value = arr
}

async function save() {
  saving.value = true
  try {
    form.value.snippet_ids = selectedSnippets.value.map(s => s.id)
    if (editingId.value === null) {
      await templateApi.createTemplate(form.value)
      notify.success('Template created')
    } else {
      await templateApi.updateTemplate(editingId.value, form.value)
      notify.success('Template updated')
    }
    dialogVisible.value = false
    await fetchTemplates()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Save failed')
  } finally {
    saving.value = false
  }
}

function handleDelete(template: ZoneTemplate) {
  confirmDelete(`Delete template "${template.name}"?`, async () => {
    try {
      await templateApi.deleteTemplate(template.id)
      notify.success('Template deleted')
      await fetchTemplates()
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : 'Delete failed'
      notify.error(msg)
    }
  })
}

onMounted(async () => {
  await Promise.all([
    fetchTemplates(),
    soaPresetApi.listSoaPresets().then(r => { soaPresets.value = r }),
    snippetApi.listSnippets().then(r => { allSnippets.value = r }),
  ])
})
</script>

<template>
  <div>
    <PageHeader title="Templates" subtitle="Reusable zone configuration templates">
      <Button
        v-if="canEdit"
        label="New Template"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="templates.length === 0"
      icon="pi pi-file"
      message="No templates configured"
    >
      <Button
        v-if="canEdit"
        label="New Template"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </EmptyState>

    <DataTable
      v-else
      :value="templates"
      size="small"
      paginator
      :rows="25"
      :rowsPerPageOptions="[25, 50, 100]"
      sortField="name"
      :sortOrder="1"
      stripedRows
    >
      <Column field="name" header="Name" sortable />
      <Column field="description" header="Description">
        <template #body="{ data }">
          <span class="description-cell text-surface-400">{{ data.description || '—' }}</span>
        </template>
      </Column>
      <Column header="SOA Preset">
        <template #body="{ data }">
          {{ soaPresetName(data.soa_preset_id) }}
        </template>
      </Column>
      <Column header="Snippets" style="text-align: right">
        <template #body="{ data }">
          {{ data.snippet_ids?.length ?? 0 }}
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
      :header="editingId !== null ? 'Edit Template' : 'New Template'"
      modal
      class="w-40rem"
    >
      <form @submit.prevent="save" class="dialog-form">
        <div class="field">
          <label for="tmpl-name">Name</label>
          <InputText id="tmpl-name" v-model="form.name" class="w-full" required />
        </div>
        <div class="field">
          <label for="tmpl-description">Description</label>
          <Textarea id="tmpl-description" v-model="form.description" class="w-full" rows="2" />
        </div>
        <div class="field">
          <label for="tmpl-soa-preset">SOA Preset</label>
          <Select
            id="tmpl-soa-preset"
            v-model="form.soa_preset_id"
            :options="soaPresets"
            optionLabel="name"
            optionValue="id"
            placeholder="None"
            :showClear="true"
            class="w-full"
          />
        </div>

        <div class="snippets-section">
          <div class="snippets-header">
            <span class="snippets-title">Snippets</span>
          </div>

          <div class="snippet-picker">
            <div class="snippet-panel">
              <div class="snippet-panel-label">Available</div>
              <div class="snippet-panel-list">
                <div
                  v-if="availableSnippets().length === 0"
                  class="snippet-empty"
                >
                  No snippets available
                </div>
                <div
                  v-for="snippet in availableSnippets()"
                  :key="snippet.id"
                  class="snippet-item"
                >
                  <span class="snippet-name">{{ snippet.name }}</span>
                  <Button
                    icon="pi pi-arrow-right"
                    text
                    rounded
                    size="small"
                    type="button"
                    aria-label="Add snippet"
                    v-tooltip.top="'Add'"
                    @click="addSnippet(snippet)"
                  />
                </div>
              </div>
            </div>

            <div class="snippet-panel">
              <div class="snippet-panel-label">Selected (ordered)</div>
              <div class="snippet-panel-list">
                <div
                  v-if="selectedSnippets.length === 0"
                  class="snippet-empty"
                >
                  No snippets selected
                </div>
                <div
                  v-for="(snippet, idx) in selectedSnippets"
                  :key="snippet.id"
                  class="snippet-item"
                >
                  <div class="snippet-order-buttons">
                    <Button
                      icon="pi pi-chevron-up"
                      text
                      rounded
                      size="small"
                      type="button"
                      :disabled="idx === 0"
                      aria-label="Move up"
                      @click="moveSnippetUp(idx)"
                    />
                    <Button
                      icon="pi pi-chevron-down"
                      text
                      rounded
                      size="small"
                      type="button"
                      :disabled="idx === selectedSnippets.length - 1"
                      aria-label="Move down"
                      @click="moveSnippetDown(idx)"
                    />
                  </div>
                  <span class="snippet-name snippet-name-selected">{{ snippet.name }}</span>
                  <Button
                    icon="pi pi-times"
                    text
                    rounded
                    size="small"
                    severity="danger"
                    type="button"
                    aria-label="Remove snippet"
                    v-tooltip.top="'Remove'"
                    @click="removeSnippet(idx)"
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

.w-full {
  width: 100%;
}

.description-cell {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 30ch;
  display: inline-block;
}

.snippets-section {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.snippets-header {
  display: flex;
  align-items: center;
}

.snippets-title {
  font-size: 0.875rem;
  font-weight: 600;
}

.snippet-picker {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.5rem;
}

.snippet-panel {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  border: 1px solid var(--p-surface-200);
  border-radius: 4px;
  overflow: hidden;
}

.snippet-panel-label {
  font-size: 0.75rem;
  font-weight: 600;
  color: var(--p-surface-500);
  text-transform: uppercase;
  letter-spacing: 0.05em;
  padding: 0.375rem 0.5rem;
  background: var(--p-surface-100);
  border-bottom: 1px solid var(--p-surface-200);
}

.snippet-panel-list {
  display: flex;
  flex-direction: column;
  min-height: 6rem;
  max-height: 16rem;
  overflow-y: auto;
  padding: 0.25rem;
  gap: 0.125rem;
}

.snippet-empty {
  font-size: 0.8125rem;
  color: var(--p-surface-400);
  padding: 0.5rem;
  text-align: center;
  margin: auto 0;
}

.snippet-item {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.125rem 0.25rem;
  border-radius: 3px;
}

.snippet-item:hover {
  background: var(--p-surface-100);
}

.snippet-name {
  flex: 1;
  font-size: 0.8125rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.snippet-name-selected {
  min-width: 0;
}

.snippet-order-buttons {
  display: flex;
  flex-direction: column;
  gap: 0;
}
</style>
