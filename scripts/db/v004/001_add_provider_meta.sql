-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- 001_add_provider_meta.sql
-- Adds provider-specific metadata column to records table.

ALTER TABLE records ADD COLUMN provider_meta JSONB;
