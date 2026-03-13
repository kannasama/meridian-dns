<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import InputText from 'primevue/inputtext'
import Password from 'primevue/password'
import Button from 'primevue/button'
import { useAuthStore } from '../stores/auth'
import { ApiRequestError } from '../api/client'
import { listEnabledIdps } from '../api/auth'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

const username = ref('')
const password = ref('')
const errorMessage = ref('')
const loading = ref(false)
const idps = ref<{ id: number; name: string; type: string }[]>([])

onMounted(async () => {
  // Show error from federated auth redirect
  if (route.query.error) {
    errorMessage.value = 'Authentication failed. Please try again.'
  }
  // Fetch enabled identity providers
  try {
    idps.value = await listEnabledIdps()
  } catch {
    // Silently ignore — federated buttons just won't show
  }
})

async function handleLogin() {
  errorMessage.value = ''
  loading.value = true
  try {
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

function federatedLogin(idp: { id: number; type: string }) {
  window.location.href = `/api/v1/auth/${idp.type}/${idp.id}/login`
}
</script>

<template>
  <div class="login-page">
    <div class="login-card">
      <h1 class="login-wordmark">Meridian DNS</h1>
      <form @submit.prevent="handleLogin" class="login-form">
        <div class="field">
          <label for="username">Username</label>
          <InputText
            id="username"
            v-model="username"
            autocomplete="username"
            :disabled="loading"
            class="w-full"
          />
        </div>
        <div class="field">
          <label for="password">Password</label>
          <Password
            id="password"
            v-model="password"
            :feedback="false"
            toggleMask
            autocomplete="current-password"
            :disabled="loading"
            class="w-full"
            inputClass="w-full"
          />
        </div>
        <p v-if="errorMessage" class="login-error">{{ errorMessage }}</p>
        <Button
          type="submit"
          label="Sign in"
          :loading="loading"
          class="w-full"
        />
      </form>

      <template v-if="idps.length > 0">
        <div class="login-divider"><span>or</span></div>
        <div class="federated-buttons">
          <Button
            v-for="idp in idps"
            :key="idp.id"
            :label="`Sign in with ${idp.name}`"
            :icon="idp.type === 'saml' ? 'pi pi-shield' : 'pi pi-lock'"
            severity="secondary"
            outlined
            class="w-full"
            @click="federatedLogin(idp)"
          />
        </div>
      </template>
    </div>
  </div>
</template>

<style scoped>
.login-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: var(--p-surface-950);
}

:root:not(.app-dark) .login-page {
  background: var(--p-surface-100);
}

.login-card {
  width: 100%;
  max-width: 24rem;
  padding: 2.5rem 2rem;
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.5rem;
}

:root:not(.app-dark) .login-card {
  background: var(--p-surface-0);
  border-color: var(--p-surface-200);
}

.login-wordmark {
  text-align: center;
  font-size: 1.5rem;
  font-weight: 700;
  color: var(--p-primary-400);
  margin: 0 0 2rem;
}

:root:not(.app-dark) .login-wordmark {
  color: var(--p-primary-600);
}

.login-form {
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

.login-error {
  color: var(--p-red-400);
  font-size: 0.85rem;
  margin: 0;
}

.w-full {
  width: 100%;
}

.login-divider {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin: 1.25rem 0;
  color: var(--p-surface-500);
  font-size: 0.8rem;
}

.login-divider::before,
.login-divider::after {
  content: '';
  flex: 1;
  height: 1px;
  background: var(--p-surface-700);
}

:root:not(.app-dark) .login-divider::before,
:root:not(.app-dark) .login-divider::after {
  background: var(--p-surface-200);
}

.federated-buttons {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}
</style>
