import { computed } from 'vue'
import { useAuthStore } from '../stores/auth'

export function useRole() {
  const auth = useAuthStore()

  function hasPermission(permission: string): boolean {
    return auth.permissions.includes(permission)
  }

  function hasAnyPermission(...permissions: string[]): boolean {
    return permissions.some(p => auth.permissions.includes(p))
  }

  return {
    // Legacy convenience — still useful for broad UI gating
    isAdmin: computed(() => auth.role === 'Admin'),
    isOperator: computed(() => auth.role === 'Operator' || auth.role === 'Admin'),
    isViewer: computed(() => true),

    // Permission-based checks (preferred)
    hasPermission,
    hasAnyPermission,
    can: hasPermission,
  }
}
