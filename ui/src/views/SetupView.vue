<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import InputText from 'primevue/inputtext'
import Password from 'primevue/password'
import Button from 'primevue/button'
import { useAuthStore } from '../stores/auth'
import { completeSetup } from '../api/setup'
import { ApiRequestError } from '../api/client'
import { markSetupComplete } from '../router'

declare global {
  interface Window {
    __SETUP_TOKEN__?: string
  }
}

const router = useRouter()
const auth = useAuthStore()

const username = ref('')
const email = ref('')
const password = ref('')
const confirmPassword = ref('')
const errorMessage = ref('')
const loading = ref(false)

const setupToken = window.__SETUP_TOKEN__ || ''

async function handleSetup() {
  errorMessage.value = ''

  if (!setupToken) {
    errorMessage.value = 'Setup token not available. The setup window may have expired. Please restart the server.'
    return
  }

  if (password.value !== confirmPassword.value) {
    errorMessage.value = 'Passwords do not match'
    return
  }

  loading.value = true
  try {
    await completeSetup({
      setup_token: setupToken,
      username: username.value,
      email: email.value,
      password: password.value,
    })
    // Mark setup complete before login to prevent redirect loop
    markSetupComplete()
    await auth.login(username.value, password.value)
    router.push('/')
  } catch (err) {
    if (err instanceof ApiRequestError) {
      errorMessage.value = err.body.message
    } else {
      errorMessage.value = 'An unexpected error occurred'
    }
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="setup-page">
    <div class="setup-card">
      <h1 class="setup-wordmark">Meridian DNS</h1>
      <p class="setup-subtitle">Initial Setup</p>
      <form @submit.prevent="handleSetup" class="setup-form">
        <div class="field">
          <label for="username">Username</label>
          <InputText
            id="username"
            v-model="username"
            autocomplete="username"
            :disabled="loading"
            required
            class="w-full"
          />
        </div>
        <div class="field">
          <label for="email">Email</label>
          <InputText
            id="email"
            v-model="email"
            type="email"
            autocomplete="email"
            :disabled="loading"
            required
            class="w-full"
          />
        </div>
        <div class="field">
          <label for="password">Password</label>
          <Password
            id="password"
            v-model="password"
            :feedback="true"
            toggleMask
            autocomplete="new-password"
            :disabled="loading"
            required
            class="w-full"
            inputClass="w-full"
          />
        </div>
        <div class="field">
          <label for="confirmPassword">Confirm Password</label>
          <Password
            id="confirmPassword"
            v-model="confirmPassword"
            :feedback="false"
            toggleMask
            autocomplete="new-password"
            :disabled="loading"
            required
            class="w-full"
            inputClass="w-full"
          />
        </div>
        <p v-if="errorMessage" class="setup-error">{{ errorMessage }}</p>
        <Button
          type="submit"
          label="Complete Setup"
          :loading="loading"
          class="w-full"
        />
      </form>
    </div>
  </div>
</template>

<style scoped>
.setup-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: var(--p-surface-950);
}

:root:not(.app-dark) .setup-page {
  background: var(--p-surface-100);
}

.setup-card {
  width: 100%;
  max-width: 24rem;
  padding: 2.5rem 2rem;
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.5rem;
}

:root:not(.app-dark) .setup-card {
  background: var(--p-surface-0);
  border-color: var(--p-surface-200);
}

.setup-wordmark {
  text-align: center;
  font-size: 1.5rem;
  font-weight: 700;
  color: var(--p-primary-400);
  margin: 0 0 0.5rem;
}

:root:not(.app-dark) .setup-wordmark {
  color: var(--p-primary-600);
}

.setup-subtitle {
  text-align: center;
  font-size: 0.95rem;
  color: var(--p-surface-400);
  margin: 0 0 2rem;
}

:root:not(.app-dark) .setup-subtitle {
  color: var(--p-surface-500);
}

.setup-form {
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
  color: var(--p-surface-300);
}

:root:not(.app-dark) .field label {
  color: var(--p-surface-600);
}

.setup-error {
  color: var(--p-red-400);
  font-size: 0.85rem;
  margin: 0;
}

.w-full {
  width: 100%;
}
</style>
