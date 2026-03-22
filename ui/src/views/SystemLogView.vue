<script setup lang="ts">
import { ref, onMounted } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Select from 'primevue/select'
import Tag from 'primevue/tag'
import DatePicker from 'primevue/datepicker'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import PageHeader from '../components/shared/PageHeader.vue'
import { querySystemLogs } from '../api/systemLogs'
import type { SystemLog } from '../types'

const logs = ref<SystemLog[]>([])
const loading = ref(false)
const selectedLog = ref<SystemLog | null>(null)
const showDetail = ref(false)

const filterCategory = ref<string | undefined>()
const filterSeverity = ref<string | undefined>()
const filterFrom = ref<Date | undefined>()
const filterTo = ref<Date | undefined>()

const categoryOptions = [
  { label: 'All', value: undefined },
  { label: 'Deployment', value: 'deployment' },
  { label: 'Provider', value: 'provider' },
  { label: 'System', value: 'system' },
]

const severityOptions = [
  { label: 'All', value: undefined },
  { label: 'Info', value: 'info' },
  { label: 'Warning', value: 'warn' },
  { label: 'Error', value: 'error' },
]

async function fetchLogs() {
  loading.value = true
  try {
    logs.value = await querySystemLogs({
      category: filterCategory.value,
      severity: filterSeverity.value,
      from: filterFrom.value ? Math.floor(filterFrom.value.getTime() / 1000) : undefined,
      to: filterTo.value ? Math.floor(filterTo.value.getTime() / 1000) : undefined,
      limit: 500,
    })
  } finally {
    loading.value = false
  }
}

function severityColor(severity: string): string {
  switch (severity) {
    case 'error': return 'danger'
    case 'warn': return 'warn'
    case 'info': return 'info'
    default: return 'secondary'
  }
}

function successColor(success: boolean | null): string | undefined {
  if (success === null) return undefined
  return success ? 'success' : 'danger'
}

function formatTime(epoch: number): string {
  return new Date(epoch * 1000).toLocaleString()
}

function viewDetail(log: SystemLog) {
  selectedLog.value = log
  showDetail.value = true
}

function clearFilters() {
  filterCategory.value = undefined
  filterSeverity.value = undefined
  filterFrom.value = undefined
  filterTo.value = undefined
  fetchLogs()
}

onMounted(fetchLogs)
</script>

<template>
  <div>
    <PageHeader title="System Log" subtitle="Technical logs for deployments, provider operations, and system events" />

    <div class="filter-bar">
      <Select
        v-model="filterCategory"
        :options="categoryOptions"
        optionLabel="label"
        optionValue="value"
        placeholder="Category"
        class="filter-select"
      />
      <Select
        v-model="filterSeverity"
        :options="severityOptions"
        optionLabel="label"
        optionValue="value"
        placeholder="Severity"
        class="filter-select"
      />
      <DatePicker
        v-model="filterFrom"
        placeholder="From"
        showTime
        showIcon
        class="filter-date"
      />
      <DatePicker
        v-model="filterTo"
        placeholder="To"
        showTime
        showIcon
        class="filter-date"
      />
      <Button label="Search" icon="pi pi-search" @click="fetchLogs" />
      <Button label="Clear" icon="pi pi-filter-slash" severity="secondary" text @click="clearFilters" />
    </div>

    <DataTable
      :value="logs"
      :loading="loading"
      stripedRows
      scrollable
      scrollHeight="calc(100vh - 280px)"
      :rowClass="(data: SystemLog) => data.severity === 'error' ? 'log-row-error' : ''"
      class="system-log-table"
    >
      <Column field="created_at" header="Time" :sortable="true" style="width: 170px">
        <template #body="{ data }">
          <span class="monospace">{{ formatTime(data.created_at) }}</span>
        </template>
      </Column>
      <Column field="severity" header="Severity" :sortable="true" style="width: 90px">
        <template #body="{ data }">
          <Tag :value="data.severity" :severity="severityColor(data.severity)" />
        </template>
      </Column>
      <Column field="category" header="Category" :sortable="true" style="width: 110px">
        <template #body="{ data }">
          <span>{{ data.category }}</span>
        </template>
      </Column>
      <Column field="operation" header="Operation" :sortable="true" style="width: 140px">
        <template #body="{ data }">
          <span class="monospace">{{ data.operation ?? '\u2014' }}</span>
        </template>
      </Column>
      <Column field="record_name" header="Record" style="min-width: 180px">
        <template #body="{ data }">
          <template v-if="data.record_name">
            <span class="monospace">{{ data.record_name }}</span>
            <Tag v-if="data.record_type" :value="data.record_type" severity="secondary" class="ml-2" />
          </template>
          <span v-else>&#x2014;</span>
        </template>
      </Column>
      <Column field="success" header="Status" :sortable="true" style="width: 80px">
        <template #body="{ data }">
          <Tag v-if="data.success !== null" :value="data.success ? 'OK' : 'FAIL'" :severity="successColor(data.success)" />
          <span v-else>&#x2014;</span>
        </template>
      </Column>
      <Column field="message" header="Message" style="min-width: 250px">
        <template #body="{ data }">
          <span>{{ data.message }}</span>
        </template>
      </Column>
      <Column header="" style="width: 60px">
        <template #body="{ data }">
          <Button
            v-if="data.detail"
            icon="pi pi-eye"
            text
            rounded
            size="small"
            @click="viewDetail(data)"
          />
        </template>
      </Column>
    </DataTable>

    <Dialog
      v-model:visible="showDetail"
      header="Log Detail"
      modal
      :style="{ width: '600px' }"
    >
      <template v-if="selectedLog">
        <div class="detail-grid">
          <div class="detail-label">Time</div>
          <div>{{ formatTime(selectedLog.created_at) }}</div>
          <div class="detail-label">Category</div>
          <div>{{ selectedLog.category }}</div>
          <div class="detail-label">Severity</div>
          <div><Tag :value="selectedLog.severity" :severity="severityColor(selectedLog.severity)" /></div>
          <div class="detail-label">Operation</div>
          <div class="monospace">{{ selectedLog.operation ?? '\u2014' }}</div>
          <div class="detail-label">Record</div>
          <div class="monospace">{{ selectedLog.record_name ?? '\u2014' }} {{ selectedLog.record_type ?? '' }}</div>
          <div class="detail-label">Status Code</div>
          <div class="monospace">{{ selectedLog.status_code ?? '\u2014' }}</div>
          <div class="detail-label">Message</div>
          <div>{{ selectedLog.message }}</div>
          <div class="detail-label">Detail</div>
          <pre class="detail-pre">{{ selectedLog.detail ?? '\u2014' }}</pre>
        </div>
      </template>
    </Dialog>
  </div>
</template>

<style scoped>
.filter-bar {
  display: flex;
  gap: 0.5rem;
  align-items: center;
  margin-bottom: 1rem;
  flex-wrap: wrap;
}

.filter-select {
  width: 140px;
}

.filter-date {
  width: 200px;
}

.monospace {
  font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
  font-size: 0.85rem;
}

.ml-2 {
  margin-left: 0.5rem;
}

.detail-grid {
  display: grid;
  grid-template-columns: 100px 1fr;
  gap: 0.5rem 1rem;
  align-items: start;
}

.detail-label {
  font-weight: 600;
  color: var(--p-text-muted-color);
}

.detail-pre {
  font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
  font-size: 0.85rem;
  white-space: pre-wrap;
  word-break: break-all;
  background: var(--p-surface-100);
  padding: 0.5rem;
  border-radius: 4px;
  margin: 0;
  max-height: 300px;
  overflow-y: auto;
}

:deep(.log-row-error) {
  background-color: color-mix(in srgb, var(--p-red-500) 8%, transparent) !important;
}

.system-log-table {
  font-size: 0.9rem;
}
</style>
