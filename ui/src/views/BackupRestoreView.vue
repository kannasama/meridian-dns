<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import Button from 'primevue/button'
import Checkbox from 'primevue/checkbox'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Message from 'primevue/message'
import Select from 'primevue/select'
import PageHeader from '../components/shared/PageHeader.vue'
import { useNotificationStore } from '../stores/notification'
import {
  downloadBackup,
  restoreFromFile,
  restoreFromRepo,
} from '../api/backup'
import type { RestoreResult, RestoreSummary } from '../api/backup'
import { listGitRepos } from '../api/gitRepos'
import { listSettings, updateSettings } from '../api/settings'
import type { GitRepo, SystemSetting } from '../types'

const notify = useNotificationStore()

// Backup settings
const repos = ref<GitRepo[]>([])
const selectedRepoId = ref<number | null>(null)
const autoInterval = ref(0)
const settingsLoading = ref(false)
const settingsSaving = ref(false)

const repoOptions = computed(() => [
  { label: 'None', value: null },
  ...repos.value.filter(r => r.is_enabled).map(r => ({ label: r.name, value: r.id })),
])

const intervalOptions = [
  { label: 'Disabled', value: 0 },
  { label: 'Every 6 hours', value: 21600 },
  { label: 'Every 12 hours', value: 43200 },
  { label: 'Every 24 hours', value: 86400 },
]

const hasBackupRepo = computed(() => selectedRepoId.value !== null && selectedRepoId.value > 0)

// Export section
const exporting = ref(false)
const commitToGit = ref(false)

async function doExport() {
  exporting.value = true
  try {
    await downloadBackup(commitToGit.value)
    notify.success('Configuration exported')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Export failed')
  } finally {
    exporting.value = false
  }
}

// File restore section
const fileContent = ref('')
const fileName = ref('')
const filePreview = ref<RestoreResult | null>(null)
const fileApplyResult = ref<RestoreResult | null>(null)
const fileLoading = ref(false)

function onFileSelect(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  if (!file) return
  fileName.value = file.name

  const reader = new FileReader()
  reader.onload = async () => {
    fileContent.value = reader.result as string
    filePreview.value = null
    fileApplyResult.value = null

    // Auto-preview
    fileLoading.value = true
    try {
      filePreview.value = await restoreFromFile(fileContent.value, false)
    } catch (e: unknown) {
      notify.error((e as Error).message || 'Failed to preview restore')
    } finally {
      fileLoading.value = false
    }
  }
  reader.readAsText(file)
}

async function doFileRestore() {
  fileLoading.value = true
  try {
    fileApplyResult.value = await restoreFromFile(fileContent.value, true)
    notify.success('Restore applied successfully')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Restore failed')
  } finally {
    fileLoading.value = false
  }
}

// Repo restore section
const repoPreview = ref<RestoreResult | null>(null)
const repoApplyResult = ref<RestoreResult | null>(null)
const repoLoading = ref(false)

async function doRepoPreview() {
  repoLoading.value = true
  repoPreview.value = null
  repoApplyResult.value = null
  try {
    repoPreview.value = await restoreFromRepo(false)
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Failed to preview repo restore')
  } finally {
    repoLoading.value = false
  }
}

async function doRepoRestore() {
  repoLoading.value = true
  try {
    repoApplyResult.value = await restoreFromRepo(true)
    notify.success('Restore from repo applied successfully')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Restore from repo failed')
  } finally {
    repoLoading.value = false
  }
}

function totalChanges(summaries: RestoreSummary[]): number {
  return summaries.reduce((sum, s) => sum + s.created + s.updated, 0)
}

onMounted(async () => {
  settingsLoading.value = true
  try {
    const [allRepos, settings] = await Promise.all([
      listGitRepos(),
      listSettings(),
    ])
    repos.value = allRepos

    const repoSetting = settings.find((s: SystemSetting) => s.key === 'backup.git_repo_id')
    if (repoSetting && repoSetting.value) {
      selectedRepoId.value = parseInt(repoSetting.value, 10) || null
    }

    const intervalSetting = settings.find((s: SystemSetting) => s.key === 'backup.auto_interval_seconds')
    if (intervalSetting && intervalSetting.value) {
      autoInterval.value = parseInt(intervalSetting.value, 10) || 0
    }
  } catch { /* ignore */ } finally {
    settingsLoading.value = false
  }
})

async function saveBackupSettings() {
  settingsSaving.value = true
  try {
    await updateSettings({
      'backup.git_repo_id': selectedRepoId.value?.toString() ?? '0',
      'backup.auto_interval_seconds': autoInterval.value.toString(),
    })
    notify.success('Backup settings saved')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Failed to save settings')
  } finally {
    settingsSaving.value = false
  }
}
</script>

<template>
  <div class="backup-restore-view">
    <PageHeader title="Backup & Restore" icon="pi pi-download" />

    <div class="sections-grid">
      <!-- Backup Settings -->
      <div class="section-card">
        <div class="section-header">
          <i class="pi pi-cog" />
          <h3>Backup Settings</h3>
        </div>
        <div class="settings-form">
          <div class="settings-field">
            <label>Git Repository</label>
            <Select
              v-model="selectedRepoId"
              :options="repoOptions"
              optionLabel="label"
              optionValue="value"
              placeholder="Select repository..."
              class="w-full"
              :loading="settingsLoading"
            />
            <small class="field-hint">Backup files are committed here on export.</small>
          </div>
          <div class="settings-field">
            <label>Auto-Backup</label>
            <Select
              v-model="autoInterval"
              :options="intervalOptions"
              optionLabel="label"
              optionValue="value"
              class="w-full"
            />
          </div>
          <Button
            label="Save Settings"
            icon="pi pi-save"
            :loading="settingsSaving"
            @click="saveBackupSettings"
            class="align-self-start"
          />
        </div>
      </div>

      <!-- Export Section -->
      <div class="section-card">
        <div class="section-header">
          <i class="pi pi-upload" />
          <h3>Export Configuration</h3>
        </div>
        <p class="section-desc">
          Download a full system configuration backup as JSON.
          Encrypted credentials are excluded — you'll need to re-enter them after restore.
        </p>
        <div class="section-actions">
          <div class="checkbox-row">
            <Checkbox
              v-model="commitToGit"
              :binary="true"
              input-id="commit-git"
              :disabled="!hasBackupRepo"
            />
            <label for="commit-git">
              Commit to GitOps repository
              <small v-if="!hasBackupRepo" class="field-hint"> — select a backup repository above</small>
            </label>
          </div>
          <Button
            label="Export Configuration"
            icon="pi pi-download"
            :loading="exporting"
            @click="doExport"
          />
        </div>
      </div>

      <!-- Restore from File Section -->
      <div class="section-card">
        <div class="section-header">
          <i class="pi pi-file-import" />
          <h3>Restore from File</h3>
        </div>
        <p class="section-desc">
          Upload a previously exported backup file to preview or apply changes.
        </p>
        <div class="section-actions">
          <label class="file-upload-label">
            <input type="file" accept=".json" @change="onFileSelect" />
            <span class="file-upload-btn">
              <i class="pi pi-file" />
              {{ fileName || 'Choose backup file...' }}
            </span>
          </label>
        </div>

        <template v-if="filePreview">
          <div class="preview-section">
            <h4>Preview ({{ totalChanges(filePreview.summaries) }} changes)</h4>
            <DataTable :value="filePreview.summaries" size="small" striped-rows>
              <Column field="entity_type" header="Entity Type" />
              <Column field="created" header="Create" />
              <Column field="updated" header="Update" />
              <Column field="skipped" header="Skip" />
            </DataTable>

            <div v-if="filePreview.credential_warnings.length" class="credential-warnings">
              <Message severity="warn" :closable="false">
                The following entities will be created without credentials.
                You'll need to re-enter them after restore:
              </Message>
              <ul>
                <li v-for="w in filePreview.credential_warnings" :key="w">{{ w }}</li>
              </ul>
            </div>

            <Button
              label="Apply Restore"
              icon="pi pi-check"
              severity="warning"
              :loading="fileLoading"
              class="mt-3"
              @click="doFileRestore"
            />
          </div>
        </template>

        <template v-if="fileApplyResult">
          <Message severity="success" :closable="false" class="mt-3">
            Restore applied: {{ totalChanges(fileApplyResult.summaries) }} changes
          </Message>
        </template>
      </div>

      <!-- Restore from Git Repository Section -->
      <div class="section-card">
        <div class="section-header">
          <i class="pi pi-github" />
          <h3>Restore from Git Repository</h3>
        </div>
        <template v-if="hasBackupRepo">
          <p class="section-desc">
            Pull the latest backup from the configured GitOps repository and restore.
          </p>
          <div class="section-actions">
            <Button
              label="Preview from Repo"
              icon="pi pi-eye"
              :loading="repoLoading"
              @click="doRepoPreview"
            />
          </div>

          <template v-if="repoPreview">
            <div class="preview-section">
              <h4>Preview ({{ totalChanges(repoPreview.summaries) }} changes)</h4>
              <DataTable :value="repoPreview.summaries" size="small" striped-rows>
                <Column field="entity_type" header="Entity Type" />
                <Column field="created" header="Create" />
                <Column field="updated" header="Update" />
                <Column field="skipped" header="Skip" />
              </DataTable>

              <div v-if="repoPreview.credential_warnings.length" class="credential-warnings">
                <Message severity="warn" :closable="false">
                  Entities requiring credential re-entry:
                </Message>
                <ul>
                  <li v-for="w in repoPreview.credential_warnings" :key="w">{{ w }}</li>
                </ul>
              </div>

              <Button
                label="Apply Restore"
                icon="pi pi-check"
                severity="warning"
                :loading="repoLoading"
                class="mt-3"
                @click="doRepoRestore"
              />
            </div>
          </template>

          <template v-if="repoApplyResult">
            <Message severity="success" :closable="false" class="mt-3">
              Restore applied: {{ totalChanges(repoApplyResult.summaries) }} changes
            </Message>
          </template>
        </template>
        <template v-else>
          <p class="section-desc">
            Configure a backup repository in Backup Settings above to enable restore from Git.
          </p>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backup-restore-view {
  padding: 0 1.5rem 1.5rem;
}

.sections-grid {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.section-card {
  background: var(--p-surface-800);
  border: 1px solid var(--p-surface-700);
  border-radius: 8px;
  padding: 1.5rem;
}

:root:not(.app-dark) .section-card {
  background: var(--p-surface-0);
  border-color: var(--p-surface-200);
}

.section-header {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}

.section-header i {
  font-size: 1.25rem;
  color: var(--p-primary-400);
}

.section-header h3 {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 600;
}

.section-desc {
  color: var(--p-surface-400);
  font-size: 0.875rem;
  margin: 0 0 1rem;
}

:root:not(.app-dark) .section-desc {
  color: var(--p-surface-500);
}

.section-actions {
  display: flex;
  align-items: center;
  gap: 1rem;
  flex-wrap: wrap;
}

.checkbox-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.875rem;
}

.file-upload-label {
  cursor: pointer;
}

.file-upload-label input[type='file'] {
  display: none;
}

.file-upload-btn {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 1rem;
  border: 1px solid var(--p-surface-600);
  border-radius: 6px;
  font-size: 0.875rem;
  background: var(--p-surface-700);
  color: var(--p-surface-200);
  transition: background 0.15s;
}

:root:not(.app-dark) .file-upload-btn {
  background: var(--p-surface-50);
  border-color: var(--p-surface-300);
  color: var(--p-surface-700);
}

.file-upload-btn:hover {
  background: var(--p-surface-600);
}

.preview-section {
  margin-top: 1rem;
}

.preview-section h4 {
  margin: 0 0 0.5rem;
  font-size: 0.95rem;
}

.credential-warnings {
  margin-top: 0.75rem;
}

.credential-warnings ul {
  margin: 0.25rem 0 0;
  padding-left: 1.25rem;
  font-size: 0.875rem;
}

.mt-3 {
  margin-top: 0.75rem;
}

.settings-form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.settings-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  max-width: 24rem;
}

.settings-field label {
  font-size: 0.875rem;
  font-weight: 500;
}

.field-hint {
  color: var(--p-surface-400);
  font-size: 0.8rem;
}

:root:not(.app-dark) .field-hint {
  color: var(--p-surface-500);
}

.align-self-start {
  align-self: flex-start;
}
</style>
