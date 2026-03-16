// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { useConfirm as usePrimeConfirm } from 'primevue/useconfirm'

export function useConfirmAction() {
  const confirm = usePrimeConfirm()

  function confirmDelete(message: string, onAccept: () => void) {
    confirm.require({
      message,
      header: 'Confirm Delete',
      icon: 'pi pi-trash',
      rejectLabel: 'Cancel',
      acceptLabel: 'Delete',
      rejectClass: 'p-button-secondary p-button-text',
      acceptClass: 'p-button-danger',
      accept: onAccept,
    })
  }

  function confirmAction(message: string, header: string, onAccept: () => void) {
    confirm.require({
      message,
      header,
      icon: 'pi pi-exclamation-triangle',
      rejectLabel: 'Cancel',
      acceptLabel: 'Confirm',
      rejectClass: 'p-button-secondary p-button-text',
      accept: onAccept,
    })
  }

  return { confirmDelete, confirmAction }
}
