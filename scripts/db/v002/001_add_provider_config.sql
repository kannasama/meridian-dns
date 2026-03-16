-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- 001_add_provider_config.sql
-- Adds encrypted_config column for provider-specific parameters.

ALTER TABLE providers ADD COLUMN encrypted_config TEXT NOT NULL DEFAULT '';
