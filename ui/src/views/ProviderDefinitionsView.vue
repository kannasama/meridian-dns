<!-- ui/src/views/ProviderDefinitionsView.vue -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Drawer from 'primevue/drawer'
import InputText from 'primevue/inputtext'
import Textarea from 'primevue/textarea'
import Skeleton from 'primevue/skeleton'
import Tag from 'primevue/tag'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import * as pdrApi from '../api/providerDefinitions'
import type {
  ProviderDefinition,
  ProviderDefinitionCreate,
  ProviderDefinitionUpdate,
} from '../api/providerDefinitions'

const { isAdmin } = useRole()
const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const definitions = ref<ProviderDefinition[]>([])
const loading = ref(false)
const drawerVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)

interface FormState {
  name: string
  type_slug: string
  version: string
  source_url: string
  definitionJson: string
}

const form = ref<FormState>({
  name: '', type_slug: '', version: '1.0.0', source_url: '', definitionJson: '{}',
})
const jsonError = ref('')

async function fetchDefinitions() {
  loading.value = true
  try {
    definitions.value = await pdrApi.listProviderDefinitions()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load provider definitions')
  } finally {
    loading.value = false
  }
}

function openCreate() {
  editingId.value = null
  form.value = { name: '', type_slug: '', version: '1.0.0', source_url: '', definitionJson: '{}' }
  jsonError.value = ''
  drawerVisible.value = true
}

async function openEdit(def: ProviderDefinition) {
  editingId.value = def.id
  form.value = {
    name: def.name,
    type_slug: def.type_slug,
    version: def.version,
    source_url: def.source_url,
    definitionJson: JSON.stringify(def.definition, null, 2),
  }
  jsonError.value = ''
  drawerVisible.value = true
}

function validateJson(): Record<string, unknown> | null {
  try {
    jsonError.value = ''
    return JSON.parse(form.value.definitionJson) as Record<string, unknown>
  } catch {
    jsonError.value = 'Invalid JSON'
    return null
  }
}

async function saveDefinition() {
  const jDefinition = validateJson()
  if (!jDefinition) return

  saving.value = true
  try {
    if (editingId.value === null) {
      const payload: ProviderDefinitionCreate = {
        name: form.value.name,
        type_slug: form.value.type_slug,
        version: form.value.version,
        definition: jDefinition,
        source_url: form.value.source_url || undefined,
      }
      const result = await pdrApi.createProviderDefinition(payload)
      notify.success(result.updated ? 'Definition updated (existing type_slug)' : 'Definition created')
    } else {
      const payload: ProviderDefinitionUpdate = {
        name: form.value.name,
        version: form.value.version,
        definition: jDefinition,
        source_url: form.value.source_url || undefined,
      }
      await pdrApi.updateProviderDefinition(editingId.value, payload)
      notify.success('Definition updated')
    }
    drawerVisible.value = false
    await fetchDefinitions()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Save failed')
  } finally {
    saving.value = false
  }
}

async function deleteDefinition(def: ProviderDefinition) {
  await confirmDelete(def.name, async () => {
    await pdrApi.deleteProviderDefinition(def.id)
    notify.success('Definition deleted')
    await fetchDefinitions()
  })
}

function exportDefinition(def: ProviderDefinition) {
  pdrApi.exportProviderDefinition(def.id)
}

onMounted(fetchDefinitions)
</script>

<template>
  <div class="provider-definitions-view">
    <PageHeader title="Provider Definitions">
      <Button
        v-if="isAdmin"
        label="Upload Definition"
        icon="pi pi-upload"
        @click="openCreate"
      />
    </PageHeader>

    <div v-if="loading" class="loading-skeletons">
      <Skeleton v-for="n in 3" :key="n" height="3rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="definitions.length === 0"
      icon="pi pi-plug"
      message="No provider definitions. Upload a JSON definition to enable generic REST or subprocess providers."
    />

    <DataTable
      v-else
      :value="definitions"
      striped-rows
      class="definitions-table"
    >
      <Column field="name" header="Name" sortable />
      <Column field="type_slug" header="Type Slug" sortable>
        <template #body="{ data }">
          <code class="type-slug">{{ data.type_slug }}</code>
        </template>
      </Column>
      <Column field="version" header="Version" />
      <Column field="active_instance_count" header="Active Instances">
        <template #body="{ data }">
          <Tag
            :value="String(data.active_instance_count)"
            :severity="data.active_instance_count > 0 ? 'info' : 'secondary'"
          />
        </template>
      </Column>
      <Column field="source_url" header="Source">
        <template #body="{ data }">
          <a v-if="data.source_url" :href="data.source_url" target="_blank" rel="noopener">
            Link
          </a>
          <span v-else class="muted">—</span>
        </template>
      </Column>
      <Column header="Actions" style="width: 10rem">
        <template #body="{ data }">
          <div class="row-actions">
            <Button
              icon="pi pi-download"
              size="small"
              text
              title="Export JSON"
              @click="exportDefinition(data)"
            />
            <Button
              v-if="isAdmin"
              icon="pi pi-pencil"
              size="small"
              text
              @click="openEdit(data)"
            />
            <Button
              v-if="isAdmin"
              icon="pi pi-trash"
              size="small"
              text
              severity="danger"
              :disabled="data.active_instance_count > 0"
              :title="data.active_instance_count > 0 ? 'Remove all provider instances first' : 'Delete'"
              @click="deleteDefinition(data)"
            />
          </div>
        </template>
      </Column>
    </DataTable>

    <!-- Create / Edit Drawer -->
    <Drawer v-model:visible="drawerVisible" :header="editingId ? 'Edit Definition' : 'Upload Definition'" position="right" style="width: 40rem">
      <div class="drawer-form">
        <div class="field">
          <label>Name</label>
          <InputText v-model="form.name" class="w-full" placeholder="Route53" />
        </div>
        <div v-if="editingId === null" class="field">
          <label>Type Slug <span class="hint">(unique identifier, e.g. "route53")</span></label>
          <InputText v-model="form.type_slug" class="w-full" placeholder="route53" />
        </div>
        <div class="field">
          <label>Version</label>
          <InputText v-model="form.version" class="w-full" placeholder="1.0.0" />
        </div>
        <div class="field">
          <label>Source URL <span class="hint">(optional)</span></label>
          <InputText v-model="form.source_url" class="w-full" placeholder="https://..." />
        </div>
        <div class="field">
          <label>Definition JSON</label>
          <Textarea
            v-model="form.definitionJson"
            class="w-full definition-editor"
            rows="20"
            :class="{ 'p-invalid': jsonError }"
            @blur="validateJson()"
          />
          <small v-if="jsonError" class="error-text">{{ jsonError }}</small>
        </div>
      </div>
      <template #footer>
        <Button label="Cancel" text @click="drawerVisible = false" />
        <Button
          :label="editingId ? 'Save' : 'Upload'"
          icon="pi pi-check"
          :loading="saving"
          @click="saveDefinition"
        />
      </template>
    </Drawer>
  </div>
</template>

<style scoped>
.loading-skeletons { padding: 1rem; }
.row-actions { display: flex; gap: 0.25rem; }
.type-slug { font-family: monospace; font-size: 0.85em; }
.hint { font-size: 0.8em; opacity: 0.7; }
.muted { opacity: 0.5; }
.definition-editor { font-family: monospace; font-size: 0.85em; }
.error-text { color: var(--p-red-500); }
.field { margin-bottom: 1rem; }
.field label { display: block; margin-bottom: 0.25rem; font-size: 0.9em; }
</style>
