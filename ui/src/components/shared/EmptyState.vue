<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

<script setup lang="ts">
defineProps<{
  icon: string
  message: string
}>()
</script>

<template>
  <div class="empty-state">
    <i :class="icon" class="empty-icon" />
    <p class="empty-message">{{ message }}</p>
    <slot />
  </div>
</template>

<style scoped>
.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 3rem 1rem;
  text-align: center;
}

.empty-icon {
  font-size: 2.5rem;
  color: var(--p-surface-500);
  margin-bottom: 1rem;
}

.empty-message {
  font-size: 0.95rem;
  color: var(--p-surface-400);
  margin: 0 0 1rem;
}
</style>
