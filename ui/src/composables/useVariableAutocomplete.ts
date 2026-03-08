import { ref, computed } from 'vue'
import { listVariables } from '../api/variables'
import type { Variable } from '../types'

export function useVariableAutocomplete(zoneId: number) {
  const variables = ref<Variable[]>([])
  const varFilter = ref('')
  const varPanelRef = ref()

  const filteredVars = computed(() => {
    if (!varFilter.value) return variables.value
    const q = varFilter.value.toLowerCase()
    return variables.value.filter(
      v => v.name.toLowerCase().includes(q) || v.value.toLowerCase().includes(q),
    )
  })

  async function loadVariables() {
    try {
      variables.value = await listVariables(undefined, zoneId)
      const globals = await listVariables('global')
      const ids = new Set(variables.value.map(v => v.id))
      for (const g of globals) {
        if (!ids.has(g.id)) variables.value.push(g)
      }
    } catch { /* non-critical */ }
  }

  function showPanel(e: Event) {
    loadVariables()
    varPanelRef.value?.show(e)
  }

  function togglePanel(e: Event) {
    loadVariables()
    varPanelRef.value?.toggle(e)
  }

  function hidePanel() {
    varPanelRef.value?.hide()
  }

  function onValueInput(e: Event) {
    const val = (e.target as HTMLInputElement).value
    if (val.endsWith('{{')) {
      showPanel(e)
    }
  }

  return {
    variables,
    varFilter,
    varPanelRef,
    filteredVars,
    loadVariables,
    showPanel,
    togglePanel,
    hidePanel,
    onValueInput,
  }
}
