<script setup lang="ts">
import { ref } from 'vue'
import Button from 'primevue/button'
import Menu from 'primevue/menu'
import Popover from 'primevue/popover'
import Tag from 'primevue/tag'
import { useAuthStore } from '../../stores/auth'
import { useThemeStore, type AccentColor } from '../../stores/theme'
import { useRouter } from 'vue-router'

const auth = useAuthStore()
const theme = useThemeStore()
const router = useRouter()

const userMenu = ref()
const accentPopover = ref()

const userMenuItems = ref([
  {
    label: auth.user?.username ?? '',
    items: [
      {
        label: 'Profile',
        icon: 'pi pi-user',
        command: () => router.push('/profile'),
      },
      {
        separator: true,
      },
      {
        label: 'Logout',
        icon: 'pi pi-sign-out',
        command: async () => {
          await auth.logout()
          router.push('/login')
        },
      },
    ],
  },
])

const colorRows: { name: AccentColor; bg: string }[][] = [
  [
    { name: 'noir', bg: '#71717a' },
    { name: 'emerald', bg: '#10b981' },
    { name: 'green', bg: '#22c55e' },
    { name: 'lime', bg: '#84cc16' },
    { name: 'orange', bg: '#f97316' },
    { name: 'amber', bg: '#f59e0b' },
    { name: 'yellow', bg: '#eab308' },
    { name: 'cyan', bg: '#06b6d4' },
  ],
  [
    { name: 'sky', bg: '#0ea5e9' },
    { name: 'blue', bg: '#3b82f6' },
    { name: 'indigo', bg: '#6366f1' },
    { name: 'violet', bg: '#8b5cf6' },
    { name: 'purple', bg: '#a855f7' },
    { name: 'fuchsia', bg: '#d946ef' },
    { name: 'pink', bg: '#ec4899' },
    { name: 'rose', bg: '#f43f5e' },
  ],
]

function toggleUserMenu(event: Event) {
  userMenu.value.toggle(event)
}

function toggleAccentPopover(event: Event) {
  accentPopover.value.toggle(event)
}
</script>

<template>
  <header class="app-topbar">
    <div class="app-topbar-start">
      <span class="app-wordmark">Meridian DNS</span>
    </div>
    <div class="app-topbar-end">
      <Button
        :icon="theme.darkMode ? 'pi pi-sun' : 'pi pi-moon'"
        text
        rounded
        aria-label="Toggle theme"
        @click="theme.toggleDarkMode()"
      />
      <Button
        icon="pi pi-palette"
        text
        rounded
        aria-label="Accent color"
        @click="toggleAccentPopover"
      />
      <Popover ref="accentPopover">
        <div class="color-grid">
          <div v-for="(row, ri) in colorRows" :key="ri" class="color-row">
            <button
              v-for="c in row"
              :key="c.name"
              class="color-swatch"
              :class="{ active: theme.accent === c.name }"
              :style="{ backgroundColor: c.bg }"
              :title="c.name"
              @click="theme.setAccent(c.name)"
            >
              <i v-if="theme.accent === c.name" class="pi pi-check swatch-check" />
            </button>
          </div>
        </div>
      </Popover>
      <Button
        text
        rounded
        aria-label="User menu"
        @click="toggleUserMenu"
      >
        <span class="app-user-label">{{ auth.user?.username }}</span>
        <Tag :value="auth.role" severity="secondary" class="ml-2" />
      </Button>
      <Menu ref="userMenu" :model="userMenuItems" :popup="true" />
    </div>
  </header>
</template>

<style scoped>
.app-topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 3.5rem;
  padding: 0 1.5rem;
  background: var(--p-surface-900);
  border-bottom: 1px solid var(--p-surface-700);
}

:root:not(.app-dark) .app-topbar {
  background: var(--p-surface-50);
  border-bottom-color: var(--p-surface-200);
}

.app-topbar-start {
  display: flex;
  align-items: center;
}

.app-wordmark {
  font-size: 1.15rem;
  font-weight: 700;
  color: var(--p-primary-400);
  letter-spacing: -0.01em;
}

:root:not(.app-dark) .app-wordmark {
  color: var(--p-primary-600);
}

.app-topbar-end {
  display: flex;
  align-items: center;
  gap: 0.25rem;
}

.app-user-label {
  font-size: 0.875rem;
}

.ml-2 {
  margin-left: 0.5rem;
}

.color-grid {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  padding: 0.25rem;
}

.color-row {
  display: flex;
  gap: 0.5rem;
}

.color-swatch {
  width: 1.5rem;
  height: 1.5rem;
  border-radius: 50%;
  border: 2px solid transparent;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  transition: transform 0.15s;
}

.color-swatch:hover {
  transform: scale(1.2);
}

.color-swatch.active {
  border-color: var(--p-surface-0);
  box-shadow: 0 0 0 1px var(--p-surface-500);
}

.swatch-check {
  font-size: 0.625rem;
  color: white;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.5);
}
</style>
