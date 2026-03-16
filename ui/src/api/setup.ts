// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post } from './client'

export interface SetupStatus {
  setup_required: boolean
}

export interface SetupRequest {
  setup_token: string
  username: string
  email: string
  password: string
}

export interface SetupResponse {
  message: string
  user_id: number
}

export function getSetupStatus(): Promise<SetupStatus> {
  return get('/setup/status')
}

export function completeSetup(data: SetupRequest): Promise<SetupResponse> {
  return post('/setup', data)
}
