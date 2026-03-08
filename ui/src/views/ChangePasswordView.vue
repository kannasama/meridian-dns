<script setup lang="ts">
import { ref } from 'vue'
import Password from 'primevue/password'
import Button from 'primevue/button'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import { useNotificationStore } from '../stores/notification'
import { changePassword } from '../api/auth'

const router = useRouter()
const auth = useAuthStore()
const notify = useNotificationStore()

const currentPassword = ref('')
const newPassword = ref('')
const confirmPassword = ref('')
const submitting = ref(false)

async function handleSubmit() {
  if (newPassword.value !== confirmPassword.value) {
    notify.error('Passwords do not match')
    return
  }
  submitting.value = true
  try {
    await changePassword({
      current_password: currentPassword.value,
      new_password: newPassword.value,
    })
    notify.success('Password changed successfully')
    await auth.hydrate()
    router.push('/')
  } catch (e: any) {
    notify.error(e.message || 'Failed to change password')
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="change-password-page">
    <div class="change-password-card">
      <div class="card-header">
        <i class="pi pi-lock card-icon" />
        <h1 class="card-title">Password Change Required</h1>
        <p class="card-subtitle">Your administrator requires you to change your password before continuing.</p>
      </div>
      <form @submit.prevent="handleSubmit" class="card-form">
        <div class="field">
          <label>Current Password</label>
          <Password v-model="currentPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
        </div>
        <div class="field">
          <label>New Password</label>
          <Password v-model="newPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
        </div>
        <div class="field">
          <label>Confirm New Password</label>
          <Password v-model="confirmPassword" :feedback="false" toggleMask class="w-full" inputClass="w-full" />
        </div>
        <Button type="submit" label="Change Password" class="w-full" :loading="submitting" />
      </form>
    </div>
  </div>
</template>

<style scoped>
.change-password-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: var(--p-surface-950);
}

:root:not(.app-dark) .change-password-page {
  background: var(--p-surface-100);
}

.change-password-card {
  width: 100%;
  max-width: 24rem;
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.75rem;
  padding: 2rem;
}

:root:not(.app-dark) .change-password-card {
  background: white;
  border-color: var(--p-surface-200);
}

.card-header { text-align: center; margin-bottom: 1.5rem; }
.card-icon { font-size: 2rem; color: var(--p-primary-400); margin-bottom: 0.75rem; }
.card-title { font-size: 1.25rem; font-weight: 700; margin: 0 0 0.5rem; }
.card-subtitle { font-size: 0.875rem; color: var(--p-surface-400); margin: 0; }
.card-form { display: flex; flex-direction: column; gap: 1rem; }
.field { display: flex; flex-direction: column; gap: 0.375rem; }
.field label { font-size: 0.875rem; font-weight: 500; }
.w-full { width: 100%; }
</style>
