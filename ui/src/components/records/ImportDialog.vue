<script setup lang="ts">
import { ref, computed } from 'vue'
import Dialog from 'primevue/dialog'
import TabMenu from 'primevue/tabmenu'
import Textarea from 'primevue/textarea'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Popover from 'primevue/popover'
import Tag from 'primevue/tag'
import Message from 'primevue/message'
import FileUpload from 'primevue/fileupload'
import { useNotificationStore } from '../../stores/notification'
import { useVariableAutocomplete } from '../../composables/useVariableAutocomplete'
import { ApiRequestError } from '../../api/client'
import * as recordApi from '../../api/records'
import type { RecordCreate } from '../../types'
import { parseCsv, parseJson, parseDnsControl } from '../../utils/importParsers'
import type { ParseResult } from '../../utils/importParsers'

const props = defineProps<{
  visible: boolean
  zoneId: number
}>()

const emit = defineEmits<{
  'update:visible': [value: boolean]
  imported: []
}>()

const notify = useNotificationStore()
const { varFilter, varPanelRef, filteredVars, togglePanel, hidePanel, onValueInput } =
  useVariableAutocomplete(props.zoneId)
void varPanelRef // used as template ref
const activeImportIndex = ref<number | null>(null)

function insertImportVariable(varName: string) {
  if (activeImportIndex.value !== null && parseResult.value) {
    parseResult.value.records[activeImportIndex.value]!.value_template += `{{${varName}}}`
  }
  hidePanel()
}

function onImportValueInput(index: number, e: Event) {
  activeImportIndex.value = index
  onValueInput(e)
}

const tabs = [
  { label: 'CSV', icon: 'pi pi-file' },
  { label: 'JSON', icon: 'pi pi-code' },
  { label: 'DNSControl', icon: 'pi pi-cog' },
  { label: 'Provider', icon: 'pi pi-cloud-download' },
]
const activeTab = ref(0)
const rawInput = ref('')
const parseResult = ref<ParseResult | null>(null)
const selectedRecords = ref<RecordCreate[]>([])
const importing = ref(false)
const fetchingProvider = ref(false)

function handleParse() {
  const text = rawInput.value.trim()
  if (!text) return

  let result: ParseResult
  switch (activeTab.value) {
    case 0:
      result = parseCsv(text)
      break
    case 1:
      result = parseJson(text)
      break
    case 2:
      result = parseDnsControl(text)
      break
    default:
      return
  }
  assignUids(result.records)
  parseResult.value = result
  selectedRecords.value = [...result.records]
}

async function handleFetchProvider() {
  fetchingProvider.value = true
  try {
    const providerRecords = await recordApi.fetchProviderRecords(props.zoneId)
    const records: RecordCreate[] = providerRecords.map((r) => ({
      name: r.name,
      type: r.type,
      value_template: r.value,
      ttl: r.ttl,
      priority: r.priority,
    }))
    assignUids(records)
    parseResult.value = { records, warnings: [] }
    selectedRecords.value = [...records]
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Failed to fetch'
    notify.error('Provider fetch failed', msg)
  } finally {
    fetchingProvider.value = false
  }
}

function handleFileUpload(event: { files: File[] }) {
  const file = event.files[0]
  if (!file) return
  const reader = new FileReader()
  reader.onload = (e) => {
    rawInput.value = e.target?.result as string
    handleParse()
  }
  reader.readAsText(file)
}

async function handleImport() {
  if (selectedRecords.value.length === 0) return
  importing.value = true
  try {
    const result = await recordApi.createRecordsBatch(props.zoneId, selectedRecords.value)
    notify.success('Import complete', `${result.count} records created`)
    emit('update:visible', false)
    emit('imported')
    resetState()
  } catch (err) {
    const msg = err instanceof ApiRequestError ? err.body.message : 'Import failed'
    notify.error('Import failed', msg)
  } finally {
    importing.value = false
  }
}

function resetState() {
  rawInput.value = ''
  parseResult.value = null
  selectedRecords.value = []
}

function assignUids(records: RecordCreate[]) {
  records.forEach((r, i) => {
    ;(r as any)._uid = `${r.name}:${r.type}:${r.value_template}:${i}`
  })
}

const isTextTab = computed(() => activeTab.value < 3)

const placeholder = computed(() => {
  if (activeTab.value === 0) return 'name,type,value,ttl,priority\n@,A,192.168.1.1,300,0'
  if (activeTab.value === 1) return '[{"name":"@","type":"A","value_template":"192.168.1.1"}]'
  return 'Paste DNSControl D() block here...'
})
</script>

<template>
  <Dialog
    :visible="props.visible"
    @update:visible="emit('update:visible', $event)"
    header="Import Records"
    modal
    class="import-dialog"
    :style="{ width: '50rem' }"
  >
    <TabMenu :model="tabs" v-model:activeIndex="activeTab" @tab-change="resetState" />

    <div v-if="isTextTab" class="input-section">
      <Textarea
        v-model="rawInput"
        :rows="8"
        class="w-full font-mono"
        :placeholder="placeholder"
      />
      <div class="input-actions">
        <FileUpload
          mode="basic"
          :auto="true"
          accept=".csv,.json,.txt,.js"
          chooseLabel="Upload File"
          @select="handleFileUpload"
          class="upload-btn"
        />
        <Button label="Parse" icon="pi pi-check" @click="handleParse" :disabled="!rawInput.trim()" />
      </div>
    </div>

    <div v-else class="input-section">
      <p>Fetch current records from the zone's DNS provider(s) for import.</p>
      <Button
        label="Fetch from Provider"
        icon="pi pi-cloud-download"
        :loading="fetchingProvider"
        @click="handleFetchProvider"
      />
    </div>

    <div v-if="parseResult?.warnings?.length" class="warnings">
      <Message v-for="(w, i) in parseResult.warnings" :key="i" severity="warn" :closable="false">
        {{ w }}
      </Message>
    </div>

    <div v-if="parseResult && parseResult.records.length > 0" class="preview-section">
      <h4>Preview ({{ selectedRecords.length }} / {{ parseResult.records.length }} selected)</h4>
      <DataTable
        v-model:selection="selectedRecords"
        :value="parseResult.records"
        size="small"
        :paginator="parseResult.records.length > 25"
        :rows="25"
        dataKey="_uid"
      >
        <Column selectionMode="multiple" style="width: 3rem" />
        <Column field="name" header="Name">
          <template #body="{ data }">
            <span class="font-mono">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="type" header="Type" style="width: 5rem">
          <template #body="{ data }">
            <Tag :value="data.type" severity="secondary" />
          </template>
        </Column>
        <Column field="value_template" header="Value">
          <template #body="{ index }">
            <div class="var-input-row">
              <InputText
                v-model="parseResult!.records[index]!.value_template"
                class="flex-1 font-mono"
                size="small"
                @input="(e: Event) => onImportValueInput(index, e)"
              />
              <Button
                icon="pi pi-search"
                severity="secondary"
                text
                size="small"
                aria-label="Browse variables"
                v-tooltip.top="'Browse variables'"
                @click="(e: any) => { activeImportIndex = index; togglePanel(e) }"
              />
            </div>
          </template>
        </Column>
        <Column field="ttl" header="TTL" style="width: 5rem">
          <template #body="{ index }">
            <InputNumber
              v-model="parseResult!.records[index]!.ttl"
              :min="1"
              size="small"
              class="w-full"
            />
          </template>
        </Column>
      </DataTable>

      <Button
        label="Import Selected"
        icon="pi pi-download"
        class="w-full mt-1"
        :loading="importing"
        :disabled="selectedRecords.length === 0"
        @click="handleImport"
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
            @click="insertImportVariable(v.name)"
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
  </Dialog>
</template>

<style scoped>
.import-dialog .input-section {
  margin-top: 1rem;
}

.input-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
  margin-top: 0.5rem;
}

.warnings {
  margin-top: 0.75rem;
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.preview-section {
  margin-top: 1rem;
}

.preview-section h4 {
  margin: 0 0 0.5rem;
  font-size: 0.9rem;
  font-weight: 600;
}

.mt-1 {
  margin-top: 0.5rem;
}

.w-full {
  width: 100%;
}

.upload-btn :deep(.p-button) {
  font-size: 0.85rem;
}

.var-input-row {
  display: flex;
  gap: 0.25rem;
  align-items: center;
}

.flex-1 {
  flex: 1;
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
</style>
