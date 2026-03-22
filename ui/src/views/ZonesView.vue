<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import AutoComplete from 'primevue/autocomplete'
import MultiSelect from 'primevue/multiselect'
import Select from 'primevue/select'
import Skeleton from 'primevue/skeleton'
import SelectButton from 'primevue/selectbutton'
import PrimeTag from 'primevue/tag'
import ToggleSwitch from 'primevue/toggleswitch'
import PageHeader from '../components/shared/PageHeader.vue'
import EmptyState from '../components/shared/EmptyState.vue'
import { useCrud } from '../composables/useCrud'
import { useConfirmAction } from '../composables/useConfirm'
import { useRole } from '../composables/useRole'
import { useNotificationStore } from '../stores/notification'
import { usePreferencesStore } from '../stores/preferences'
import * as zoneApi from '../api/zones'
import * as viewApi from '../api/views'
import * as gitRepoApi from '../api/gitRepos'
import * as soaPresetApi from '../api/soaPresets'
import { listTags } from '../api/tags'
import type { Zone, ZoneCreate, View, GitRepo } from '../types'
import type { SoaPreset } from '../api/soaPresets'

const router = useRouter()
const { isAdmin } = useRole()
const { confirmDelete } = useConfirmAction()
const notify = useNotificationStore()

type ZoneUpdateData = {
  name: string
  view_id?: number | null
  deployment_retention?: number | null
  manage_soa?: boolean
  manage_ns?: boolean
  soa_preset_id?: number | null
  git_repo_id?: number | null
  git_branch?: string | null
}

const { items: zones, loading, fetch: fetchZones, create, update, remove } = useCrud<
  Zone,
  ZoneCreate,
  ZoneUpdateData
>(
  {
    list: () => zoneApi.listZones(),
    create: zoneApi.createZone,
    update: (id: number, data: ZoneUpdateData) => zoneApi.updateZone(id, data),
    remove: zoneApi.deleteZone,
  },
  'Zone',
)

const allViews = ref<View[]>([])
const allGitRepos = ref<GitRepo[]>([])
const soaPresets = ref<SoaPreset[]>([])
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const form = ref({
  name: '',
  view_id: null as number | null,
  deployment_retention: null as number | null,
  manage_soa: false,
  manage_ns: false,
  soa_preset_id: null as number | null,
  git_repo_id: null as number | null,
  git_branch: '' as string,
  tags: [] as string[],
})

// Tag filter
const allTags = ref<string[]>([])
const selectedTagFilters = ref<string[]>([])
const tagSuggestions = ref<string[]>([])
let originalTags: string[] = []

const preferences = usePreferencesStore()

const zoneCategoryOptions = [
  { label: 'Forward', value: 'forward' },
  { label: 'Reverse', value: 'reverse' },
  { label: 'All', value: 'all' },
]
const zoneCategory = ref(preferences.zoneDefaultView)

const isReverseZone = (name: string) =>
  name.endsWith('.in-addr.arpa') || name.endsWith('.ip6.arpa')

const categoryFilteredZones = computed(() => {
  if (zoneCategory.value === 'forward')
    return zones.value.filter(z => !isReverseZone(z.name))
  if (zoneCategory.value === 'reverse')
    return zones.value.filter(z => isReverseZone(z.name))
  return zones.value
})

const filteredZones = computed(() => {
  let result = categoryFilteredZones.value

  // Exclude zones with hidden tags (from preferences)
  const hiddenTags = preferences.zoneHiddenTags
  if (hiddenTags.length > 0 && selectedTagFilters.value.length === 0) {
    result = result.filter(z =>
      !hiddenTags.some(tag => (z.tags ?? []).includes(tag))
    )
  }

  // Apply explicit tag filter (if user has selected tags)
  if (selectedTagFilters.value.length > 0) {
    result = result.filter(z =>
      selectedTagFilters.value.every(tag => (z.tags ?? []).includes(tag))
    )
  }

  return result
})

async function saveFilterDefaults() {
  try {
    await preferences.saveMany({
      zone_default_view: zoneCategory.value,
      zone_hidden_tags: selectedTagFilters.value,
    })
    notify.success('Filter defaults saved')
  } catch {
    notify.error('Failed to save defaults')
  }
}

// Clone
const showCloneDialog = ref(false)
const cloneName = ref('')
const cloneViewId = ref<number | null>(null)
const cloningSourceId = ref<number | null>(null)

function onTagSearch(event: { query: string }) {
  tagSuggestions.value = allTags.value.filter(t =>
    t.toLowerCase().includes(event.query.toLowerCase())
  )
}

function onTagKeydown(event: KeyboardEvent) {
  if (event.key !== 'Enter') return
  const input = (event.target as HTMLInputElement).value?.trim()
  if (!input) return
  if (!form.value.tags.includes(input)) {
    form.value.tags.push(input)
  }
  ;(event.target as HTMLInputElement).value = ''
  event.preventDefault()
}

function openCreate() {
  editingId.value = null
  form.value = { name: '', view_id: null, deployment_retention: null, manage_soa: false, manage_ns: false, soa_preset_id: null, git_repo_id: null, git_branch: '', tags: [] }
  originalTags = []
  dialogVisible.value = true
}

function openEdit(zone: Zone) {
  editingId.value = zone.id
  const tags = [...(zone.tags ?? [])]
  form.value = {
    name: zone.name,
    view_id: zone.view_id,
    deployment_retention: zone.deployment_retention,
    manage_soa: zone.manage_soa,
    manage_ns: zone.manage_ns,
    soa_preset_id: zone.soa_preset_id ?? null,
    git_repo_id: zone.git_repo_id,
    git_branch: zone.git_branch || '',
    tags,
  }
  originalTags = [...tags]
  dialogVisible.value = true
}

async function handleSubmit() {
  let ok: boolean
  const gitBranch = form.value.git_branch.trim() || null
  if (editingId.value !== null) {
    ok = await update(editingId.value, {
      name: form.value.name,
      view_id: form.value.view_id,
      deployment_retention: form.value.deployment_retention,
      manage_soa: form.value.manage_soa,
      manage_ns: form.value.manage_ns,
      soa_preset_id: form.value.soa_preset_id,
      git_repo_id: form.value.git_repo_id,
      git_branch: gitBranch,
    })
  } else {
    ok = await create({
      name: form.value.name,
      view_id: form.value.view_id!,
      deployment_retention: form.value.deployment_retention,
      manage_soa: form.value.manage_soa,
      manage_ns: form.value.manage_ns,
      soa_preset_id: form.value.soa_preset_id,
      git_repo_id: form.value.git_repo_id,
      git_branch: gitBranch,
    })
  }
  if (ok) {
    const tagsChanged = JSON.stringify(form.value.tags.slice().sort()) !== JSON.stringify(originalTags.slice().sort())
    if (tagsChanged) {
      if (editingId.value !== null) {
        await zoneApi.updateZoneTags(editingId.value, form.value.tags)
        await fetchZones()
      } else if (form.value.tags.length > 0) {
        const created = zones.value.find(z => z.name === form.value.name)
        if (created) {
          await zoneApi.updateZoneTags(created.id, form.value.tags)
          await fetchZones()
        }
      }
      listTags().then((t) => { allTags.value = t.map(tag => tag.name) }).catch(() => {})
    }
    dialogVisible.value = false
  }
}

function handleDelete(zone: Zone) {
  confirmDelete(`Delete zone "${zone.name}"?`, () => remove(zone.id))
}

function viewName(viewId: number): string {
  return allViews.value.find((v) => v.id === viewId)?.name || `#${viewId}`
}

function navigateToZone(zone: Zone) {
  router.push({ name: 'zone-detail', params: { id: zone.id } })
}

function openClone(zone: Zone) {
  cloningSourceId.value = zone.id
  cloneName.value = ''
  cloneViewId.value = zone.view_id
  showCloneDialog.value = true
}

async function submitClone() {
  if (!cloningSourceId.value || !cloneViewId.value || !cloneName.value) return
  try {
    const newZone = await zoneApi.cloneZone(cloningSourceId.value, {
      name: cloneName.value,
      view_id: cloneViewId.value,
    })
    notify.success('Zone cloned')
    showCloneDialog.value = false
    router.push(`/zones/${newZone.id}`)
  } catch {
    notify.error('Failed to clone zone')
  }
}

onMounted(async () => {
  await Promise.all([
    fetchZones(),
    viewApi.listViews().then((v) => (allViews.value = v)),
    gitRepoApi.listGitRepos().then((r) => (allGitRepos.value = r)),
    soaPresetApi.listSoaPresets().then((r) => { soaPresets.value = r }).catch(() => {}),
    listTags().then((t) => { allTags.value = t.map(tag => tag.name) }).catch(() => {}),
  ])

  // Apply saved preferences
  if (preferences.loaded) {
    zoneCategory.value = preferences.zoneDefaultView
  }
})
</script>

<template>
  <div>
    <PageHeader title="Zones" subtitle="DNS zones">
      <Button v-if="isAdmin" label="Add Zone" icon="pi pi-plus" @click="openCreate" />
    </PageHeader>

    <div v-if="loading" class="skeleton-table">
      <Skeleton v-for="i in 5" :key="i" height="2.5rem" class="mb-2" />
    </div>

    <EmptyState
      v-else-if="zones.length === 0"
      icon="pi pi-globe"
      message="No zones yet. Add your first zone."
    >
      <Button v-if="isAdmin" label="Add Zone" icon="pi pi-plus" @click="openCreate" />
    </EmptyState>

    <template v-else>
      <div class="filter-bar">
        <SelectButton
          v-model="zoneCategory"
          :options="zoneCategoryOptions"
          optionLabel="label"
          optionValue="value"
          :allowEmpty="false"
        />
        <MultiSelect
          v-model="selectedTagFilters"
          :options="allTags"
          placeholder="Filter by tag"
          class="tag-filter"
          :showClear="true"
        />
        <Button
          v-if="zoneCategory !== preferences.zoneDefaultView"
          label="Save as default"
          text
          size="small"
          @click="saveFilterDefaults"
        />
      </div>

      <DataTable
        :value="filteredZones"
        size="small"
        paginator
        :rows="25"
        :rowsPerPageOptions="[25, 50, 100]"
        sortField="name"
        :sortOrder="1"
        stripedRows
        selectionMode="single"
        @rowSelect="(e: any) => navigateToZone(e.data)"
        class="cursor-pointer"
      >
        <Column field="name" header="Name" sortable>
          <template #body="{ data }">
            <span class="font-mono">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="view_id" header="View" sortable>
          <template #body="{ data }">
            {{ viewName(data.view_id) }}
          </template>
        </Column>
        <Column header="Tags">
          <template #body="{ data }">
            <PrimeTag
              v-for="tag in (data.tags ?? []).slice(0, 3)"
              :key="tag"
              :value="tag"
              class="mr-1 text-xs"
              severity="secondary"
            />
            <span v-if="(data.tags ?? []).length > 3" class="text-sm text-muted">
              +{{ data.tags.length - 3 }}
            </span>
          </template>
        </Column>
        <Column v-if="isAdmin" header="Actions" style="width: 8rem; text-align: right">
          <template #body="{ data }">
            <div class="action-buttons" @click.stop>
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
                icon="pi pi-copy"
                text
                rounded
                size="small"
                aria-label="Clone"
                v-tooltip.top="'Clone'"
                @click="openClone(data)"
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
    </template>

    <Dialog
      v-model:visible="dialogVisible"
      :header="editingId ? 'Edit Zone' : 'Add Zone'"
      modal
      class="w-30rem"
    >
      <form @submit.prevent="handleSubmit" class="dialog-form">
        <div class="field">
          <label for="zone-name">Name</label>
          <InputText id="zone-name" v-model="form.name" class="w-full" placeholder="example.com" />
        </div>
        <div class="field">
          <label for="zone-view">View</label>
          <Select
            id="zone-view"
            v-model="form.view_id"
            :options="allViews"
            optionLabel="name"
            optionValue="id"
            placeholder="Select a view"
            class="w-full"
          />
        </div>
        <div class="field">
          <label for="zone-retention">Deployment Retention (optional)</label>
          <InputNumber
            id="zone-retention"
            v-model="form.deployment_retention"
            :min="1"
            :max="1000"
            class="w-full"
            placeholder="Default"
          />
          <small class="text-surface-400">Leave blank to use system default (10)</small>
        </div>
        <div class="field-group">
          <label class="field-group-label">Record Management</label>
          <div class="toggle-row">
            <ToggleSwitch id="zone-manage-soa" v-model="form.manage_soa" />
            <label for="zone-manage-soa" class="toggle-label" v-tooltip.right="'When enabled, SOA records are included in diff previews and deployments. Usually leave off — most providers manage SOA automatically.'">Manage SOA records</label>
          </div>
          <div v-if="form.manage_soa" class="field">
            <label>SOA Preset</label>
            <Select v-model="form.soa_preset_id"
                    :options="soaPresets"
                    optionLabel="name"
                    optionValue="id"
                    placeholder="None (use defaults)"
                    :showClear="true"
                    class="w-full" />
          </div>
          <div class="toggle-row">
            <ToggleSwitch id="zone-manage-ns" v-model="form.manage_ns" />
            <label for="zone-manage-ns" class="toggle-label" v-tooltip.right="'When enabled, NS records are included in diff previews and deployments. Enable for self-hosted providers (e.g. PowerDNS).'">Manage NS records</label>
          </div>
        </div>
        <div v-if="allGitRepos.length > 0" class="field-group">
          <label class="field-group-label">GitOps</label>
          <div class="field">
            <label for="zone-git-repo">Git Repository</label>
            <Select
              id="zone-git-repo"
              v-model="form.git_repo_id"
              :options="[{ id: null, name: 'None' }, ...allGitRepos]"
              optionLabel="name"
              optionValue="id"
              placeholder="None"
              class="w-full"
            />
          </div>
          <div v-if="form.git_repo_id" class="field">
            <label for="zone-git-branch">Branch Override</label>
            <InputText
              id="zone-git-branch"
              v-model="form.git_branch"
              class="w-full"
              placeholder="Use repo default"
            />
            <small class="text-surface-400">Leave blank to use the repository's default branch</small>
          </div>
        </div>
        <div class="field">
          <label>Tags</label>
          <AutoComplete
            v-model="form.tags"
            :suggestions="tagSuggestions"
            multiple
            @complete="onTagSearch"
            @keydown.enter="onTagKeydown"
            class="w-full"
            placeholder="Add tags..."
          />
        </div>
        <Button type="submit" :label="editingId ? 'Save' : 'Create'" class="w-full" />
      </form>
    </Dialog>

    <Dialog v-model:visible="showCloneDialog" header="Clone Zone" modal>
      <div class="clone-dialog-body">
        <label class="text-sm">New Zone Name</label>
        <InputText v-model="cloneName" placeholder="new-zone.example.com" class="w-full" />
        <label class="text-sm">View</label>
        <Select
          v-model="cloneViewId"
          :options="allViews"
          optionLabel="name"
          optionValue="id"
          placeholder="Select view"
          class="w-full"
        />
        <div class="flex justify-end gap-2">
          <Button label="Cancel" severity="secondary" @click="showCloneDialog = false" />
          <Button label="Clone" icon="pi pi-copy" @click="submitClone" />
        </div>
      </div>
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

.cursor-pointer :deep(tr) {
  cursor: pointer;
}

.field-group {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.field-group-label {
  font-size: 0.875rem;
  font-weight: 500;
  color: var(--p-surface-400);
}

.toggle-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.toggle-label {
  font-size: 0.875rem;
  cursor: pointer;
}

.filter-bar {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}

.tag-filter {
  width: 16rem;
}

.text-xs {
  font-size: 0.75rem;
}

.text-sm {
  font-size: 0.875rem;
}

.text-muted {
  color: var(--p-text-muted-color);
}

.mr-1 {
  margin-right: 0.25rem;
}

.clone-dialog-body {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  padding: 0.5rem 0;
  min-width: 22rem;
}

.flex {
  display: flex;
}

.justify-end {
  justify-content: flex-end;
}

.gap-2 {
  gap: 0.5rem;
}
</style>
