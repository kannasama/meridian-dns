<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

onMounted(async () => {
  const token = route.query.token as string
  if (!token) {
    router.push('/login')
    return
  }

  // Store the token and hydrate the auth store
  localStorage.setItem('jwt', token)
  auth.token = token
  const valid = await auth.hydrate()

  if (valid) {
    router.push('/')
  } else {
    router.push('/login')
  }
})
</script>

<template>
  <div class="callback-page">
    <p>Signing in...</p>
  </div>
</template>

<style scoped>
.callback-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  color: var(--p-surface-400);
}
</style>
