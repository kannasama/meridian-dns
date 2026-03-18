-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Workstream 2: Extend system_config for DB-backed settings
-- Create system_config if it was not already created by the application bootstrap step
-- (needed when migrations are applied via raw psql rather than the C++ migration runner)
CREATE TABLE IF NOT EXISTS system_config (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

ALTER TABLE system_config ADD COLUMN IF NOT EXISTS description TEXT;
ALTER TABLE system_config ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ DEFAULT now();
