import { defineStore } from 'pinia'
import { ref, watch } from 'vue'
import { updatePreset } from '@primevue/themes'

export type AccentColor = 'indigo' | 'blue' | 'teal' | 'green' | 'amber' | 'rose'

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
    updatePreset({
      semantic: {
        primary: {
          50: `{${color}.50}`,
          100: `{${color}.100}`,
          200: `{${color}.200}`,
          300: `{${color}.300}`,
          400: `{${color}.400}`,
          500: `{${color}.500}`,
          600: `{${color}.600}`,
          700: `{${color}.700}`,
          800: `{${color}.800}`,
          900: `{${color}.900}`,
          950: `{${color}.950}`,
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
