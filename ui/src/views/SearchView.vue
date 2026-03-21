<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import { searchRecords } from '../api/search'
import type { SearchResult } from '../types'
import { useNotificationStore } from '../stores/notification'

const route = useRoute()
const router = useRouter()
const notify = useNotificationStore()

const results = ref<SearchResult[]>([])
const loading = ref(false)
const query = ref('')

async function performSearch(q: string) {
  if (!q.trim()) {
    results.value = []
    return
  }
  loading.value = true
  try {
    results.value = await searchRecords({ q })
  } catch {
    notify.error('Search failed')
  } finally {
    loading.value = false
  }
}

function goToZone(zoneId: number) {
  router.push(`/zones/${zoneId}`)
}

// React to URL query param changes
watch(
  () => route.query.q as string | undefined,
  (newQ) => {
    query.value = newQ ?? ''
    if (newQ) performSearch(newQ)
  },
  { immediate: true },
)
</script>

<template>
  <div class="p-4">
    <div class="flex justify-between items-center mb-4">
      <h1 class="text-xl font-semibold">
        Record Search
        <span v-if="query" class="text-muted font-normal text-base ml-2">
          — "{{ query }}"
        </span>
      </h1>
    </div>

    <DataTable
      :value="results"
      :loading="loading"
      class="text-sm"
      size="small"
      stripedRows
      @rowClick="(e: any) => goToZone(e.data.zone_id)"
      style="cursor: pointer"
    >
      <Column field="name" header="Name">
        <template #body="{ data }">
          <span class="font-mono">{{ data.name }}</span>
        </template>
      </Column>
      <Column field="type" header="Type" style="width: 6rem" />
      <Column field="ttl" header="TTL" style="width: 5rem">
        <template #body="{ data }">
          <span class="font-mono">{{ data.ttl }}</span>
        </template>
      </Column>
      <Column field="value_template" header="Value">
        <template #body="{ data }">
          <span class="font-mono">{{ data.value_template }}</span>
        </template>
      </Column>
      <Column header="Zone">
        <template #body="{ data }">
          <router-link :to="`/zones/${data.zone_id}`" class="text-primary" @click.stop>
            {{ data.zone_name }}
          </router-link>
        </template>
      </Column>
      <Column field="view_name" header="View" />
    </DataTable>

    <p v-if="!loading && results.length === 0 && query" class="text-muted text-sm mt-4">
      No records found for "{{ query }}"
    </p>
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

.text-muted {
  color: var(--p-text-muted-color);
}

.font-normal {
  font-weight: 400;
}

.text-base {
  font-size: 1rem;
}

.ml-2 {
  margin-left: 0.5rem;
}

.text-sm {
  font-size: 0.875rem;
}

.mt-4 {
  margin-top: 1rem;
}

.text-primary {
  color: var(--p-primary-400);
  text-decoration: none;
}

.text-primary:hover {
  text-decoration: underline;
}

:root:not(.app-dark) .text-primary {
  color: var(--p-primary-600);
}

.font-mono {
  font-family: monospace;
}
</style>
