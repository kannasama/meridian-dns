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
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import * as soaPresetApi from '../api/soaPresets'
import type { SoaPreset, SoaPresetCreate } from '../api/soaPresets'

const { isAdmin, isOperator } = useRole()
const canEdit = computed(() => isAdmin.value || isOperator.value)

const notify = useNotificationStore()
const { confirmDelete } = useConfirmAction()

const presets = ref<SoaPreset[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)

const form = ref<SoaPresetCreate>({
  name: '',
  mname_template: '',
  rname_template: '',
  refresh: 3600,
  retry: 900,
  expire: 604800,
  minimum: 300,
  default_ttl: 3600,
})

async function fetchPresets() {
  loading.value = true
  try {
    presets.value = await soaPresetApi.listSoaPresets()
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load SOA presets')
  } finally {
    loading.value = false
  }
}

function openCreate() {
  editingId.value = null
  form.value = {
    name: '',
    mname_template: '',
    rname_template: '',
    refresh: 3600,
    retry: 900,
    expire: 604800,
    minimum: 300,
    default_ttl: 3600,
  }
  dialogVisible.value = true
}

async function openEdit(preset: SoaPreset) {
  editingId.value = preset.id
  try {
    const full = await soaPresetApi.getSoaPreset(preset.id)
    form.value = {
      name: full.name,
      mname_template: full.mname_template,
      rname_template: full.rname_template,
      refresh: full.refresh,
      retry: full.retry,
      expire: full.expire,
      minimum: full.minimum,
      default_ttl: full.default_ttl,
    }
    dialogVisible.value = true
  } catch (e: unknown) {
    notify.error(e instanceof Error ? e.message : 'Failed to load SOA preset')
  }
}

async function save() {
  saving.value = true
  try {
    if (editingId.value === null) {
      await soaPresetApi.createSoaPreset(form.value)
      notify.success('SOA preset created')
    } else {
      await soaPresetApi.updateSoaPreset(editingId.value, form.value)
      notify.success('SOA preset updated')
    }
    dialogVisible.value = false
    await fetchPresets()
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : 'Save failed'
    notify.error(msg)
  } finally {
    saving.value = false
  }
}

function handleDelete(preset: SoaPreset) {
  confirmDelete(`Delete SOA preset "${preset.name}"?`, async () => {
    try {
      await soaPresetApi.deleteSoaPreset(preset.id)
      notify.success('SOA preset deleted')
      await fetchPresets()
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : 'Delete failed'
      notify.error(msg)
    }
  })
}

onMounted(fetchPresets)
</script>

<template>
  <div>
    <PageHeader title="SOA Presets" subtitle="Reusable SOA record configurations">
      <Button
        v-if="canEdit"
        label="New SOA Preset"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="presets.length === 0"
      icon="pi pi-server"
      message="No SOA presets configured"
    >
      <Button
        v-if="canEdit"
        label="New SOA Preset"
        icon="pi pi-plus"
        @click="openCreate"
      />
    </EmptyState>

    <DataTable
      v-else
      :value="presets"
      size="small"
      paginator
      :rows="25"
      :rowsPerPageOptions="[25, 50, 100]"
      sortField="name"
      :sortOrder="1"
      stripedRows
    >
      <Column field="name" header="Name" sortable />
      <Column field="mname_template" header="Primary NS">
        <template #body="{ data }">
          <span class="font-mono text-truncate">{{ data.mname_template }}</span>
        </template>
      </Column>
      <Column field="rname_template" header="Hostmaster">
        <template #body="{ data }">
          <span class="font-mono text-truncate">{{ data.rname_template }}</span>
        </template>
      </Column>
      <Column field="refresh" header="Refresh" style="text-align: right">
        <template #body="{ data }">
          <span class="text-right">{{ data.refresh }}s</span>
        </template>
      </Column>
      <Column field="default_ttl" header="Default TTL" style="text-align: right">
        <template #body="{ data }">
          <span class="text-right">{{ data.default_ttl }}s</span>
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
      :header="editingId !== null ? 'Edit SOA Preset' : 'New SOA Preset'"
      modal
      class="w-40rem"
    >
      <form @submit.prevent="save" class="dialog-form">
        <div class="field">
          <label for="soa-name">Name</label>
          <InputText id="soa-name" v-model="form.name" class="w-full" required />
        </div>
        <div class="field">
          <label for="soa-mname">Primary NS (mname_template)</label>
          <InputText
            id="soa-mname"
            v-model="form.mname_template"
            class="w-full font-mono"
            placeholder="ns1.{{sys.zone}}"
          />
        </div>
        <div class="field">
          <label for="soa-rname">Hostmaster (rname_template)</label>
          <InputText
            id="soa-rname"
            v-model="form.rname_template"
            class="w-full font-mono"
            placeholder="hostmaster.{{sys.zone}}"
          />
        </div>
        <div class="field-row">
          <div class="field">
            <label for="soa-refresh">Refresh</label>
            <InputNumber id="soa-refresh" v-model="form.refresh" :min="1" class="w-full" />
          </div>
          <div class="field">
            <label for="soa-retry">Retry</label>
            <InputNumber id="soa-retry" v-model="form.retry" :min="1" class="w-full" />
          </div>
        </div>
        <div class="field-row">
          <div class="field">
            <label for="soa-expire">Expire</label>
            <InputNumber id="soa-expire" v-model="form.expire" :min="1" class="w-full" />
          </div>
          <div class="field">
            <label for="soa-minimum">Minimum</label>
            <InputNumber id="soa-minimum" v-model="form.minimum" :min="1" class="w-full" />
          </div>
        </div>
        <div class="field">
          <label for="soa-default-ttl">Default TTL</label>
          <InputNumber id="soa-default-ttl" v-model="form.default_ttl" :min="1" class="w-full" />
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
  grid-template-columns: 1fr 1fr;
  gap: 0.5rem;
}

.w-full {
  width: 100%;
}

.font-mono {
  font-family: monospace;
}

.text-truncate {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 20ch;
  display: inline-block;
}

.text-right {
  display: block;
  text-align: right;
}
</style>
