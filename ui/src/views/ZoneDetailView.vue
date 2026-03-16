<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { onMounted, ref, computed, watch, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Popover from 'primevue/popover'
import Select from 'primevue/select'
import Skeleton from 'primevue/skeleton'
import Tag from 'primevue/tag'
import ToggleSwitch from 'primevue/toggleswitch'
import PageHeader from '../components/shared/PageHeader.vue'
import ImportDialog from '../components/records/ImportDialog.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import { ApiRequestError } from '../api/client'
import * as zoneApi from '../api/zones'
import * as recordApi from '../api/records'
import { downloadZoneExport } from '../api/backup'
import { captureCurrentState } from '../api/deployments'
import * as viewApi from '../api/views'
import * as providerApi from '../api/providers'
import { useVariableAutocomplete } from '../composables/useVariableAutocomplete'
import type { Zone, DnsRecord, RecordCreate, Provider } from '../types'
import Toolbar from 'primevue/toolbar'

const route = useRoute()
const router = useRouter()
const { isOperator } = useRole()
const { confirmDelete } = useConfirmAction()
const notify = useNotificationStore()

const zoneId = computed(() => Number(route.params.id))
const allZones = ref<Zone[]>([])
const zone = ref<Zone | null>(null)
const records = ref<DnsRecord[]>([])
const loading = ref(true)
const viewProviders = ref<Provider[]>([])
const proxied = ref(false)
const autoTtl = ref(true)
const capturing = ref(false)
const selectedRecords = ref<DnsRecord[]>([])
const bulkTtlDialogVisible = ref(false)
const bulkTtlValue = ref(300)
const bulkAutoTtlDialogVisible = ref(false)
const bulkAutoTtlValue = ref(true)
const bulkProxyDialogVisible = ref(false)
const bulkProxyValue = ref(false)

const {
  variables, varFilter, varPanelRef, filteredVars,
  loadVariables, togglePanel, hidePanel, onValueInput,
} = useVariableAutocomplete(zoneId)
void varPanelRef // used as template ref

const valueInputRef = ref<{ $el?: HTMLElement } | null>(null)

function insertVariable(varName: string) {
  const input = valueInputRef.value?.$el?.querySelector?.('input') as HTMLInputElement
    ?? document.getElementById('rec-value') as HTMLInputElement
  if (input) {
    const start = input.selectionStart ?? form.value.value_template.length
    const end = input.selectionEnd ?? start
    const before = form.value.value_template.slice(0, start)
    const after = form.value.value_template.slice(end)
    const token = `{{${varName}}}`
    form.value.value_template = before + token + after
    // Restore cursor after the inserted token
    const cursorPos = start + token.length
    nextTick(() => {
      input.focus()
      input.setSelectionRange(cursorPos, cursorPos)
    })
  } else {
    form.value.value_template += `{{${varName}}}`
  }
  hidePanel()
}

const dialogVisible = ref(false)
const importDialogVisible = ref(false)
const editingRecordId = ref<number | null>(null)
const form = ref<RecordCreate>({
  name: '',
  type: 'A',
  ttl: 300,
  value_template: '',
  priority: 0,
})

const recordTypes = ['A', 'AAAA', 'CNAME', 'MX', 'TXT', 'SRV', 'NS', 'PTR'].map((t) => ({
  label: t,
  value: t,
}))

const hasCloudflareProvider = computed(() =>
  viewProviders.value.some((p) => p.type === 'cloudflare'),
)

const showProxyToggle = computed(
  () =>
    hasCloudflareProvider.value &&
    ['A', 'AAAA', 'CNAME'].includes(form.value.type),
)

const showAutoTtlToggle = computed(() => hasCloudflareProvider.value)

async function fetchData() {
  loading.value = true
  try {
    const [z, r] = await Promise.all([zoneApi.getZone(zoneId.value), recordApi.listRecords(zoneId.value)])
    zone.value = z
    records.value = r

    // Fetch view providers to detect Cloudflare
    if (z.view_id) {
      try {
        const view = await viewApi.getView(z.view_id)
        const providers = await Promise.all(
          view.provider_ids.map((id) => providerApi.getProvider(id)),
        )
        viewProviders.value = providers
      } catch {
        // Non-critical — proxy toggle just won't show
      }
    }
    loadVariables()
  } catch {
    notify.error('Failed to load zone')
  } finally {
    loading.value = false
  }
  zoneApi.listZones().then(z => { allZones.value = z }).catch(() => {})
}

function expandTemplate(sTemplate: string): string | null {
  if (!sTemplate.includes('{{')) return null
  return sTemplate.replace(/\{\{([A-Za-z0-9_]+)\}\}/g, (_match, varName) => {
    const zoneVar = variables.value.find(v => v.name === varName && v.scope === 'zone')
    const globalVar = variables.value.find(v => v.name === varName && v.scope === 'global')
    const resolved = zoneVar || globalVar
    return resolved ? resolved.value : `{{${varName}}}`
  })
}

function hasUnresolvedVars(sTemplate: string): boolean {
  const expanded = expandTemplate(sTemplate)
  return expanded !== null && /\{\{[A-Za-z0-9_]+\}\}/.test(expanded)
}

function openCreateRecord() {
  editingRecordId.value = null
  form.value = { name: '', type: 'A', ttl: 300, value_template: '', priority: 0 }
  proxied.value = false
  autoTtl.value = hasCloudflareProvider.value
  dialogVisible.value = true
}

function openEditRecord(rec: DnsRecord) {
  editingRecordId.value = rec.id
  form.value = {
    name: rec.name,
    type: rec.type,
    ttl: rec.ttl,
    value_template: rec.value_template,
    priority: rec.priority,
  }
  proxied.value = (rec.provider_meta as Record<string, unknown>)?.proxied === true
  autoTtl.value = (rec.provider_meta as Record<string, unknown>)?.auto_ttl === true
  dialogVisible.value = true
}

async function handleSubmitRecord() {
  try {
    const payload: RecordCreate = { ...form.value }
    if (hasCloudflareProvider.value) {
      const meta: Record<string, unknown> = { auto_ttl: autoTtl.value }
      if (['A', 'AAAA', 'CNAME'].includes(payload.type)) {
        meta.proxied = proxied.value
      }
      payload.provider_meta = meta
    }
    if (editingRecordId.value !== null) {
      await recordApi.updateRecord(zoneId.value, editingRecordId.value, payload)
      const idx = records.value.findIndex((r) => r.id === editingRecordId.value)
      if (idx !== -1) {
        const existing = records.value[idx]!
        records.value[idx] = {
          ...existing,
          name: payload.name,
          type: payload.type,
          ttl: payload.ttl ?? existing.ttl,
          value_template: payload.value_template,
          priority: payload.priority ?? existing.priority,
          provider_meta: payload.provider_meta ?? existing.provider_meta,
        }
      }
      notify.success('Record updated')
    } else {
      const result = await recordApi.createRecord(zoneId.value, payload)
      records.value.push({
        id: result.id,
        zone_id: zoneId.value,
        name: payload.name,
        type: payload.type,
        ttl: payload.ttl ?? 300,
        value_template: payload.value_template,
        priority: payload.priority ?? 0,
        provider_meta: payload.provider_meta ?? null,
        last_audit_id: null,
        pending_delete: false,
        created_at: Date.now(),
        updated_at: Date.now(),
      })
      notify.success('Record created')
    }
    dialogVisible.value = false
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Failed to save record'
    notify.error('Error', msg)
  }
}

function handleDeleteRecord(rec: DnsRecord) {
  confirmDelete(`Delete record "${rec.name}" (${rec.type})?`, async () => {
    try {
      await recordApi.deleteRecord(zoneId.value, rec.id)
      const idx = records.value.findIndex((r) => r.id === rec.id)
      if (idx !== -1) {
        records.value[idx]!.pending_delete = true
      }
      notify.success('Record deleted')
    } catch (err) {
      const msg = err instanceof ApiRequestError ? err.body.message : 'Failed to delete'
      notify.error('Error', msg)
    }
  })
}

async function handleRestoreRecord(rec: DnsRecord) {
  try {
    await recordApi.restoreRecord(zoneId.value, rec.id)
    const idx = records.value.findIndex((r) => r.id === rec.id)
    if (idx !== -1) {
      records.value[idx]!.pending_delete = false
    }
    notify.success('Record restored')
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Failed to restore'
    notify.error('Error', msg)
  }
}

function rowClass(data: DnsRecord) {
  return data.pending_delete ? 'row-pending-delete' : ''
}

async function handleBulkDelete() {
  const ids = selectedRecords.value
    .filter((r) => !r.pending_delete)
    .map((r) => r.id)
  if (ids.length === 0) return
  try {
    await recordApi.batchRecords(zoneId.value, { deletes: ids })
    for (const id of ids) {
      const idx = records.value.findIndex((r) => r.id === id)
      if (idx !== -1) {
        records.value[idx]!.pending_delete = true
      }
    }
    selectedRecords.value = []
    notify.success(`${ids.length} record(s) deleted`)
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Bulk delete failed'
    notify.error('Error', msg)
  }
}

async function handleBulkSetTtl() {
  const ids = selectedRecords.value
    .filter((r) => !r.pending_delete)
    .map((r) => r.id)
  if (ids.length === 0) return
  try {
    await recordApi.batchRecords(zoneId.value, {
      updates: ids.map((id) => ({ id, ttl: bulkTtlValue.value })),
    })
    for (const id of ids) {
      const idx = records.value.findIndex((r) => r.id === id)
      if (idx !== -1) {
        records.value[idx]!.ttl = bulkTtlValue.value
      }
    }
    selectedRecords.value = []
    bulkTtlDialogVisible.value = false
    notify.success(`TTL updated for ${ids.length} record(s)`)
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Bulk TTL update failed'
    notify.error('Error', msg)
  }
}

async function handleBulkSetAutoTtl() {
  const ids = selectedRecords.value
    .filter((r) => !r.pending_delete)
    .map((r) => r.id)
  if (ids.length === 0) return
  try {
    await recordApi.batchRecords(zoneId.value, {
      updates: ids.map((id) => {
        const rec = records.value.find((r) => r.id === id)
        const meta = { ...(rec?.provider_meta as Record<string, unknown> ?? {}), auto_ttl: bulkAutoTtlValue.value }
        return { id, provider_meta: meta }
      }),
    })
    for (const id of ids) {
      const idx = records.value.findIndex((r) => r.id === id)
      if (idx !== -1) {
        const rec = records.value[idx]!
        rec.provider_meta = { ...(rec.provider_meta as Record<string, unknown> ?? {}), auto_ttl: bulkAutoTtlValue.value }
      }
    }
    selectedRecords.value = []
    bulkAutoTtlDialogVisible.value = false
    notify.success(`Auto-TTL ${bulkAutoTtlValue.value ? 'enabled' : 'disabled'} for ${ids.length} record(s)`)
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Bulk Auto-TTL update failed'
    notify.error('Error', msg)
  }
}

async function handleBulkSetProxy() {
  const proxyableIds = selectedRecords.value
    .filter((r) => !r.pending_delete && ['A', 'AAAA', 'CNAME'].includes(r.type))
    .map((r) => r.id)
  if (proxyableIds.length === 0) return
  try {
    await recordApi.batchRecords(zoneId.value, {
      updates: proxyableIds.map((id) => {
        const rec = records.value.find((r) => r.id === id)
        const meta: Record<string, unknown> = { ...(rec?.provider_meta as Record<string, unknown> ?? {}), proxied: bulkProxyValue.value }
        if (bulkProxyValue.value) meta.auto_ttl = true
        return { id, provider_meta: meta }
      }),
    })
    for (const id of proxyableIds) {
      const idx = records.value.findIndex((r) => r.id === id)
      if (idx !== -1) {
        const rec = records.value[idx]!
        const meta: Record<string, unknown> = { ...(rec.provider_meta as Record<string, unknown> ?? {}), proxied: bulkProxyValue.value }
        if (bulkProxyValue.value) meta.auto_ttl = true
        rec.provider_meta = meta
      }
    }
    selectedRecords.value = []
    bulkProxyDialogVisible.value = false
    notify.success(`Proxy ${bulkProxyValue.value ? 'enabled' : 'disabled'} for ${proxyableIds.length} record(s)`)
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Bulk proxy update failed'
    notify.error('Error', msg)
  }
}

const bulkHasProxyableRecords = computed(() =>
  selectedRecords.value.some((r) => !r.pending_delete && ['A', 'AAAA', 'CNAME'].includes(r.type)),
)

function goToDeploy() {
  router.push({ name: 'deployments', query: { zones: String(zoneId.value) } })
}

async function doCapture() {
  capturing.value = true
  try {
    const result = await captureCurrentState(zoneId.value)
    notify.success(result.message)
    await fetchData()
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Capture failed')
  } finally {
    capturing.value = false
  }
}

async function doExportZone() {
  try {
    await downloadZoneExport(zoneId.value)
    notify.success('Zone exported')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Zone export failed')
  }
}

const showPriority = computed(() => form.value.type === 'MX' || form.value.type === 'SRV')

const hasPriorityRecords = computed(() =>
  records.value.some((r) => r.type === 'MX' || r.type === 'SRV'),
)

watch(
  () => form.value.type,
  (newType) => {
    if (newType !== 'MX' && newType !== 'SRV') {
      form.value.priority = 0
    }
    if (!['A', 'AAAA', 'CNAME'].includes(newType)) {
      proxied.value = false
    }
  },
)

watch(proxied, (bProxied) => {
  if (bProxied) {
    autoTtl.value = true  // Cloudflare enforces TTL=1 on proxied records
  }
})

watch(zoneId, () => {
  fetchData()
})

onMounted(fetchData)
</script>

<template>
  <div>
    <div v-if="loading">
      <Skeleton height="2rem" width="20rem" class="mb-2" />
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <template v-else-if="zone">
      <PageHeader :title="zone.name" subtitle="Zone records">
        <Select
          :modelValue="zoneId"
          @update:modelValue="(id: number) => router.push({ name: 'zone-detail', params: { id } })"
          :options="allZones"
          optionLabel="name"
          optionValue="id"
          placeholder="Switch zone..."
          class="zone-switcher mr-2"
          filter
          filterPlaceholder="Search zones..."
        />
        <Button
          v-if="isOperator"
          label="Deploy"
          icon="pi pi-play"
          severity="success"
          @click="goToDeploy"
          class="mr-2"
        />
        <Button
          label="Export"
          icon="pi pi-upload"
          severity="secondary"
          @click="doExportZone"
          class="mr-2"
        />
        <Button
          v-if="isOperator && zone?.git_repo_id"
          label="Capture State"
          icon="pi pi-camera"
          severity="secondary"
          :loading="capturing"
          @click="doCapture"
          class="mr-2"
        />
        <Button
          v-if="isOperator"
          label="Import"
          icon="pi pi-download"
          severity="secondary"
          @click="importDialogVisible = true"
          class="mr-2"
        />
        <Button
          v-if="isOperator"
          label="Add Record"
          icon="pi pi-plus"
          @click="openCreateRecord"
        />
      </PageHeader>

      <EmptyState
        v-if="records.length === 0"
        icon="pi pi-list"
        message="No records yet. Add your first DNS record."
      >
        <Button
          v-if="isOperator"
          label="Add Record"
          icon="pi pi-plus"
          @click="openCreateRecord"
        />
      </EmptyState>

      <Toolbar v-if="selectedRecords.length > 0" class="mb-2">
        <template #start>
          <span class="text-sm font-semibold">{{ selectedRecords.length }} selected</span>
        </template>
        <template #end>
          <Button
            label="Set TTL"
            icon="pi pi-clock"
            severity="secondary"
            size="small"
            class="mr-2"
            @click="bulkTtlDialogVisible = true"
          />
          <Button
            v-if="hasCloudflareProvider"
            label="Auto-TTL"
            icon="pi pi-bolt"
            severity="secondary"
            size="small"
            class="mr-2"
            @click="bulkAutoTtlDialogVisible = true"
          />
          <Button
            v-if="hasCloudflareProvider && bulkHasProxyableRecords"
            label="Proxy"
            icon="pi pi-shield"
            severity="secondary"
            size="small"
            class="mr-2"
            @click="bulkProxyDialogVisible = true"
          />
          <Button
            label="Delete Selected"
            icon="pi pi-trash"
            severity="danger"
            size="small"
            @click="handleBulkDelete"
          />
        </template>
      </Toolbar>

      <DataTable
        v-if="records.length > 0"
        v-model:selection="selectedRecords"
        :value="records"
        size="small"
        paginator
        :rows="50"
        :rowsPerPageOptions="[25, 50, 100]"
        sortMode="multiple"
        :multiSortMeta="[{ field: 'type', order: 1 }, { field: 'name', order: 1 }]"
        stripedRows
        :rowClass="rowClass"
        dataKey="id"
      >
        <Column v-if="isOperator" selectionMode="multiple" headerStyle="width: 3rem" />
        <Column field="name" header="Name" sortable>
          <template #body="{ data }">
            <span class="font-mono">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="type" header="Type" sortable style="width: 6rem">
          <template #body="{ data }">
            <Tag :value="data.type" severity="secondary" />
          </template>
        </Column>
        <Column field="value_template" header="Value">
          <template #body="{ data }">
            <div v-if="expandTemplate(data.value_template)" class="value-expanded">
              <span class="font-mono" :class="{ 'text-warn': hasUnresolvedVars(data.value_template) }">
                {{ expandTemplate(data.value_template) }}
              </span>
              <span class="font-mono value-template-raw">{{ data.value_template }}</span>
            </div>
            <span v-else class="font-mono">{{ data.value_template }}</span>
          </template>
        </Column>
        <Column field="ttl" header="TTL" sortable style="width: 7rem">
          <template #body="{ data }">
            <span class="font-mono">{{ data.ttl }}</span>
            <Tag
              v-if="hasCloudflareProvider && data.provider_meta?.auto_ttl"
              value="Auto"
              severity="secondary"
              class="ml-auto-ttl"
            />
          </template>
        </Column>
        <Column v-if="hasPriorityRecords" field="priority" header="Priority" sortable style="width: 5rem">
          <template #body="{ data }">
            <span class="font-mono">{{ data.priority }}</span>
          </template>
        </Column>
        <Column v-if="hasCloudflareProvider" header="Proxy" style="width: 5rem">
          <template #body="{ data }">
            <Tag v-if="data.provider_meta?.proxied" value="Proxied" severity="info" />
          </template>
        </Column>
        <Column v-if="isOperator" header="Actions" style="width: 6rem; text-align: right">
          <template #body="{ data }">
            <div class="action-buttons">
              <template v-if="data.pending_delete">
                <Button
                  icon="pi pi-undo"
                  text
                  rounded
                  size="small"
                  severity="warn"
                  aria-label="Undo Delete"
                  v-tooltip.top="'Undo Delete'"
                  @click="handleRestoreRecord(data)"
                />
              </template>
              <template v-else>
                <Button
                  icon="pi pi-pencil"
                  text
                  rounded
                  size="small"
                  aria-label="Edit"
                  v-tooltip.top="'Edit'"
                  @click="openEditRecord(data)"
                />
                <Button
                  icon="pi pi-trash"
                  text
                  rounded
                  size="small"
                  severity="danger"
                  aria-label="Delete"
                  v-tooltip.top="'Delete'"
                  @click="handleDeleteRecord(data)"
                />
              </template>
            </div>
          </template>
        </Column>
      </DataTable>
    </template>

    <Dialog
      v-model:visible="dialogVisible"
      :header="editingRecordId ? 'Edit Record' : 'Add Record'"
      modal
      class="w-30rem"
    >
      <form @submit.prevent="handleSubmitRecord" class="dialog-form">
        <div class="field">
          <label for="rec-name">Name</label>
          <InputText id="rec-name" v-model="form.name" class="w-full" placeholder="@" />
        </div>
        <div class="field">
          <label for="rec-type">Type</label>
          <Select
            id="rec-type"
            v-model="form.type"
            :options="recordTypes"
            optionLabel="label"
            optionValue="value"
            class="w-full"
          />
        </div>
        <div class="field">
          <label for="rec-value">Value Template</label>
          <div class="var-input-row">
            <InputText
              id="rec-value"
              ref="valueInputRef"
              v-model="form.value_template"
              class="flex-1 font-mono"
              placeholder="192.168.1.1 or {{my_var}}"
              @input="onValueInput"
            />
            <Button
              v-if="form.value_template"
              icon="pi pi-times"
              severity="secondary"
              text
              aria-label="Clear value"
              v-tooltip.top="'Clear value'"
              @click="form.value_template = ''"
            />
            <Button
              icon="pi pi-search"
              severity="secondary"
              text
              aria-label="Browse variables"
              v-tooltip.top="'Browse variables'"
              @click="(e: any) => togglePanel(e)"
            />
          </div>
          <Popover ref="varPanelRef">
            <div class="var-panel">
              <InputText
                v-model="varFilter"
                placeholder="Filter variables..."
                class="w-full var-panel-filter"
              />
              <div class="var-panel-list">
                <div
                  v-for="v in filteredVars"
                  :key="v.id"
                  class="var-panel-item"
                  @click="insertVariable(v.name)"
                >
                  <div class="var-item-content">
                    <span class="font-mono text-sm" v-text="'{{' + v.name + '}}'"></span>
                    <span class="font-mono var-item-value">{{ v.value }}</span>
                  </div>
                  <Tag :value="v.scope" :severity="v.scope === 'global' ? 'info' : 'warn'" />
                </div>
                <div v-if="filteredVars.length === 0" class="var-panel-empty">
                  No variables found
                </div>
              </div>
            </div>
          </Popover>
        </div>
        <div class="form-row">
          <div class="field flex-1">
            <label for="rec-ttl">TTL</label>
            <InputNumber id="rec-ttl" v-model="form.ttl" :min="1" :max="604800" class="w-full" />
          </div>
          <div v-if="showPriority" class="field flex-1">
            <label for="rec-priority">Priority</label>
            <InputNumber id="rec-priority" v-model="form.priority" :min="0" class="w-full" />
          </div>
        </div>
        <div v-if="showProxyToggle" class="field">
          <div class="proxy-row">
            <label for="rec-proxied">Cloudflare Proxy</label>
            <ToggleSwitch id="rec-proxied" v-model="proxied" />
          </div>
          <small class="text-muted">Route traffic through Cloudflare's CDN/WAF</small>
        </div>
        <div v-if="showAutoTtlToggle" class="field">
          <div class="proxy-row">
            <label for="rec-auto-ttl">Cloudflare Auto TTL</label>
            <ToggleSwitch id="rec-auto-ttl" v-model="autoTtl" :disabled="proxied" />
          </div>
          <small class="text-muted">
            {{ proxied ? 'Auto TTL is required for proxied records' : 'Cloudflare will use Auto TTL (other providers use the TTL value above)' }}
          </small>
        </div>
        <Button type="submit" :label="editingRecordId ? 'Save' : 'Create'" class="w-full" />
      </form>
    </Dialog>

    <ImportDialog
      v-model:visible="importDialogVisible"
      :zoneId="zoneId"
      @imported="fetchData"
    />

    <Dialog
      v-model:visible="bulkTtlDialogVisible"
      header="Set TTL for Selected Records"
      modal
      class="w-20rem"
    >
      <div class="dialog-form">
        <div class="field">
          <label for="bulk-ttl">TTL (seconds)</label>
          <InputNumber id="bulk-ttl" v-model="bulkTtlValue" :min="1" :max="604800" class="w-full" />
        </div>
        <Button label="Apply" icon="pi pi-check" class="w-full" @click="handleBulkSetTtl" />
      </div>
    </Dialog>

    <Dialog
      v-model:visible="bulkAutoTtlDialogVisible"
      header="Cloudflare Auto-TTL"
      modal
      class="w-20rem"
    >
      <div class="dialog-form">
        <div class="field">
          <div class="proxy-row">
            <label for="bulk-auto-ttl">Enable Auto-TTL</label>
            <ToggleSwitch id="bulk-auto-ttl" v-model="bulkAutoTtlValue" />
          </div>
          <small class="text-muted">Cloudflare will manage TTL automatically</small>
        </div>
        <Button label="Apply" icon="pi pi-check" class="w-full" @click="handleBulkSetAutoTtl" />
      </div>
    </Dialog>

    <Dialog
      v-model:visible="bulkProxyDialogVisible"
      header="Cloudflare Proxy"
      modal
      class="w-20rem"
    >
      <div class="dialog-form">
        <div class="field">
          <div class="proxy-row">
            <label for="bulk-proxy">Enable Proxy</label>
            <ToggleSwitch id="bulk-proxy" v-model="bulkProxyValue" />
          </div>
          <small class="text-muted">Route traffic through Cloudflare's CDN/WAF (A, AAAA, CNAME only)</small>
        </div>
        <Button label="Apply" icon="pi pi-check" class="w-full" @click="handleBulkSetProxy" />
      </div>
    </Dialog>
  </div>
</template>

<style scoped>
.zone-switcher {
  width: 14rem;
}

.mb-2 {
  margin-bottom: 0.5rem;
}

.mr-2 {
  margin-right: 0.5rem;
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

.form-row {
  display: flex;
  gap: 1rem;
}

.proxy-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.var-input-row {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}

.var-panel {
  width: 20rem;
}

.var-panel-filter {
  margin-bottom: 0.5rem;
}

.var-panel-list {
  max-height: 15rem;
  overflow-y: auto;
}

.var-panel-item {
  padding: 0.5rem;
  cursor: pointer;
  border-radius: var(--p-border-radius);
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.var-panel-item:hover {
  background: var(--p-surface-hover);
}

.var-item-content {
  display: flex;
  flex-direction: column;
  gap: 0.125rem;
  flex: 1;
  overflow: hidden;
}

.var-item-value {
  font-size: 0.75rem;
  color: var(--p-text-muted-color);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.var-panel-empty {
  color: var(--p-text-muted-color);
  font-size: 0.875rem;
  padding: 0.5rem;
}

.value-expanded {
  display: flex;
  flex-direction: column;
  gap: 0.125rem;
}

.value-template-raw {
  font-size: 0.75rem;
  color: var(--p-text-muted-color);
}

.text-warn {
  color: var(--p-orange-400);
}

.ml-auto-ttl {
  margin-left: 0.375rem;
  font-size: 0.7rem;
}

.text-muted {
  color: var(--p-text-muted-color);
}

.flex-1 {
  flex: 1;
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

.w-30rem {
  width: 30rem;
}

.w-20rem {
  width: 20rem;
}

.font-semibold {
  font-weight: 600;
}

.text-sm {
  font-size: 0.875rem;
}
</style>

<style>
.row-pending-delete {
  opacity: 0.5;
}

.row-pending-delete td span {
  text-decoration: line-through;
}
</style>
