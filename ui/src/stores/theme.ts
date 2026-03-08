import { defineStore } from 'pinia'
import { ref, watch } from 'vue'
import { updatePreset } from '@primevue/themes'

export type AccentColor =
  | 'noir' | 'emerald' | 'green' | 'lime'
  | 'orange' | 'amber' | 'yellow' | 'cyan'
  | 'sky' | 'blue' | 'indigo' | 'violet'
  | 'purple' | 'fuchsia' | 'pink' | 'rose'

export const useThemeStore = defineStore('theme', () => {
  const darkMode = ref(localStorage.getItem('theme-dark') !== 'false')
  const accent = ref<AccentColor>(
    (localStorage.getItem('theme-accent') as AccentColor) || 'indigo',
  )

  function applyDarkMode() {
    if (darkMode.value) {
      document.documentElement.classList.add('app-dark')
    } else {
      document.documentElement.classList.remove('app-dark')
    }
  }

  function applyAccent(color: AccentColor) {
    const palette = color === 'noir' ? 'zinc' : color
    updatePreset({
      semantic: {
        primary: {
          50: `{${palette}.50}`,
          100: `{${palette}.100}`,
          200: `{${palette}.200}`,
          300: `{${palette}.300}`,
          400: `{${palette}.400}`,
          500: `{${palette}.500}`,
          600: `{${palette}.600}`,
          700: `{${palette}.700}`,
          800: `{${palette}.800}`,
          900: `{${palette}.900}`,
          950: `{${palette}.950}`,
        },
      },
    })
  }

  function toggleDarkMode() {
    darkMode.value = !darkMode.value
  }

  function setAccent(color: AccentColor) {
    accent.value = color
  }

  watch(darkMode, (val) => {
    localStorage.setItem('theme-dark', String(val))
    applyDarkMode()
  })

  watch(accent, (val) => {
    localStorage.setItem('theme-accent', val)
    applyAccent(val)
  })

  // Apply on init
  applyDarkMode()
  applyAccent(accent.value)

  return { darkMode, accent, toggleDarkMode, setAccent }
})
