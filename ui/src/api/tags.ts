// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, post, put, del } from './client'
import type { Tag } from '../types'

export function listTags(): Promise<Tag[]> {
  return get('/tags')
}

export function createTag(name: string): Promise<Tag> {
  return post('/tags', { name })
}

export function renameTag(id: number, name: string): Promise<Tag> {
  return put(`/tags/${id}`, { name })
}

export function deleteTag(id: number): Promise<void> {
  return del(`/tags/${id}`)
}
