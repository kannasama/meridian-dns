<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import Tag from 'primevue/tag'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Skeleton from 'primevue/skeleton'
import PageHeader from '../components/shared/PageHeader.vue'
import * as providerApi from '../api/providers'
import * as zoneApi from '../api/zones'
import * as viewApi from '../api/views'
import * as healthApi from '../api/health'
import { syncCheckZone, syncCheckAll, type SyncCheckAllResult } from '../api/zones'
import type { Zone, View, ProviderHealth } from '../types'

const router = useRouter()

const zoneCount = ref(0)
const providerCount = ref(0)
const healthStatus = ref<string>('unknown')
const zones = ref<Zone[]>([])
const allViews = ref<View[]>([])
const loading = ref(true)
const providerHealth = ref<ProviderHealth[]>([])
const healthLoading = ref(false)
const refreshingZones = ref<Set<number>>(new Set())
const refreshingAll = ref(false)
// Offset between server clock and browser clock (server_time - Date.now()/1000)
const serverTimeOffset = ref(0)

const viewMap = computed(() => {
  const map = new Map<number, string>()
  for (const v of allViews.value) {
    map.set(v.id, v.name)
  }
  return map
})

const stats = ref([
  { label: 'Zones', icon: 'pi pi-globe', value: 0 },
  { label: 'Providers', icon: 'pi pi-server', value: 0 },
])

onMounted(async () => {
  try {
    const [providers, allZones, views, health] = await Promise.all([
      providerApi.listProviders(),
      zoneApi.listZones(),
      viewApi.listViews(),
      healthApi.getHealth().catch(() => ({ status: 'unreachable' })),
    ])
    allViews.value = views
    providerCount.value = providers.length
    zoneCount.value = allZones.length
    zones.value = allZones
    healthStatus.value = health.status

    stats.value = [
      { label: 'Zones', icon: 'pi pi-globe', value: allZones.length },
      { label: 'Providers', icon: 'pi pi-server', value: providers.length },
    ]
  } finally {
    loading.value = false
  }

  healthLoading.value = true
  providerApi.getProviderHealth()
    .then(h => providerHealth.value = h)
    .catch(() => {})
    .finally(() => healthLoading.value = false)
})

function navigateToZone(zone: Zone) {
  router.push({ name: 'zone-detail', params: { id: zone.id } })
}

function syncStatusSeverity(status?: string) {
  switch (status) {
    case 'in_sync': return 'success'
    case 'drift': return 'warn'
    case 'error': return 'danger'
    default: return 'secondary'
  }
}

function relativeTime(ts: string | number | null | undefined): string {
  if (ts == null) return 'Never'
  const sec = typeof ts === 'number' ? ts : new Date(ts).getTime() / 1000
  // Use server-adjusted "now" to avoid clock skew between browser and server
  const nowSec = Date.now() / 1000 + serverTimeOffset.value
  const diff = nowSec - sec
  const mins = Math.floor(diff / 60)
  if (mins < 1) return 'Just now'
  if (mins < 60) return `${mins}m ago`
  const hours = Math.floor(mins / 60)
  if (hours < 24) return `${hours}h ago`
  return `${Math.floor(hours / 24)}d ago`
}

async function refreshZone(zone: Zone) {
  refreshingZones.value.add(zone.id)
  try {
    const result = await syncCheckZone(zone.id)
    zone.sync_status = result.sync_status
    zone.sync_checked_at = result.sync_checked_at
    if (result.server_time) {
      serverTimeOffset.value = result.server_time - Date.now() / 1000
    }
  } catch { /* ignore */ }
  refreshingZones.value.delete(zone.id)
}

async function refreshAllZones() {
  refreshingAll.value = true
  try {
    const resp = await syncCheckAll()
    if (resp.server_time) {
      serverTimeOffset.value = resp.server_time - Date.now() / 1000
    }
    for (const r of resp.results) {
      const z = zones.value.find(z => z.id === r.zone_id)
      if (z) {
        z.sync_status = r.sync_status
        z.sync_checked_at = r.sync_checked_at
      }
    }
  } catch { /* ignore */ }
  refreshingAll.value = false
}
</script>

<template>
  <div>
    <PageHeader title="Dashboard" subtitle="System overview" />

    <div v-if="loading" class="stats-grid">
      <Skeleton v-for="i in 3" :key="i" height="5rem" />
    </div>

    <template v-else>
      <div class="stats-grid">
        <div v-for="stat in stats" :key="stat.label" class="stat-card">
          <div class="stat-icon">
            <i :class="stat.icon" />
          </div>
          <div class="stat-content">
            <span class="stat-value">{{ stat.value }}</span>
            <span class="stat-label">{{ stat.label }}</span>
          </div>
        </div>
        <div class="stat-card">
          <div class="stat-icon">
            <i
              :class="healthStatus === 'ok' ? 'pi pi-check-circle' : 'pi pi-exclamation-triangle'"
            />
          </div>
          <div class="stat-content">
            <Tag
              :value="healthStatus"
              :severity="healthStatus === 'ok' ? 'success' : 'danger'"
            />
            <span class="stat-label">System Health</span>
          </div>
        </div>
      </div>

      <h3 class="section-title">Provider Health</h3>
      <DataTable :value="providerHealth" size="small" :loading="healthLoading" stripedRows>
        <Column field="name" header="Provider">
          <template #body="{ data }">
            <span class="font-mono">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="type" header="Type">
          <template #body="{ data }">
            <Tag :value="data.type" severity="secondary" />
          </template>
        </Column>
        <Column field="status" header="Status">
          <template #body="{ data }">
            <Tag :value="data.status"
              :severity="data.status === 'healthy' ? 'success' : data.status === 'error' ? 'danger' : 'warn'" />
          </template>
        </Column>
        <Column field="message" header="Message" />
      </DataTable>

      <div class="flex align-items-center justify-content-between mb-3">
        <h3 class="section-title" style="margin: 0">Zones</h3>
        <Button
          icon="pi pi-refresh"
          label="Refresh All"
          size="small"
          text
          :loading="refreshingAll"
          @click="refreshAllZones"
        />
      </div>
      <DataTable
        v-if="zones.length > 0"
        :value="zones"
        size="small"
        paginator
        :rows="25"
        stripedRows
        selectionMode="single"
        @rowSelect="(e: any) => navigateToZone(e.data)"
        class="cursor-pointer"
      >
        <Column field="name" header="Name">
          <template #body="{ data }">
            <span class="font-mono">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="view_id" header="View">
          <template #body="{ data }">
            {{ viewMap.get(data.view_id) || '—' }}
          </template>
        </Column>
        <Column field="sync_status" header="Sync Status">
          <template #body="{ data }">
            <Tag :value="data.sync_status || 'unknown'" :severity="syncStatusSeverity(data.sync_status)" />
          </template>
        </Column>
        <Column header="Last Checked">
          <template #body="{ data }">
            <span class="text-surface-400 text-sm">{{ relativeTime(data.sync_checked_at) }}</span>
          </template>
        </Column>
        <Column header="" style="width: 3rem">
          <template #body="{ data }">
            <Button
              icon="pi pi-refresh"
              text
              rounded
              size="small"
              :loading="refreshingZones.has(data.id)"
              @click.stop="refreshZone(data)"
            />
          </template>
        </Column>
      </DataTable>
    </template>
  </div>
</template>

<style scoped>
.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(14rem, 1fr));
  gap: 1rem;
  margin-bottom: 1.5rem;
}

.stat-card {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1rem 1.25rem;
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.5rem;
}

:root:not(.app-dark) .stat-card {
  background: var(--p-surface-50);
  border-color: var(--p-surface-200);
}

.stat-icon {
  font-size: 1.5rem;
  color: var(--p-primary-400);
}

:root:not(.app-dark) .stat-icon {
  color: var(--p-primary-600);
}

.stat-content {
  display: flex;
  flex-direction: column;
}

.stat-value {
  font-size: 1.5rem;
  font-weight: 700;
  line-height: 1;
  color: var(--p-surface-0);
}

:root:not(.app-dark) .stat-value {
  color: var(--p-surface-900);
}

.stat-label {
  font-size: 0.8rem;
  color: var(--p-surface-400);
  margin-top: 0.25rem;
}

.section-title {
  font-size: 1.1rem;
  font-weight: 600;
  margin: 0 0 0.75rem;
  color: var(--p-surface-200);
}

:root:not(.app-dark) .section-title {
  color: var(--p-surface-700);
}

.cursor-pointer :deep(tr) {
  cursor: pointer;
}

.flex { display: flex; }
.align-items-center { align-items: center; }
.justify-content-between { justify-content: space-between; }
.mb-3 { margin-bottom: 0.75rem; }
.text-surface-400 { color: var(--p-surface-400); }
.text-sm { font-size: 0.875rem; }
</style>
