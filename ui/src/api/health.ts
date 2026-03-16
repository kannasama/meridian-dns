// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get } from './client'

export interface HealthResponse {
  status: string
}

export function getHealth(): Promise<HealthResponse> {
  return get('/health')
}
