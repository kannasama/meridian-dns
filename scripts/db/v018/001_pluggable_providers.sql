-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v018: Pluggable Provider System

-- Extend provider_type ENUM with generic adapter types
ALTER TYPE provider_type ADD VALUE IF NOT EXISTS 'generic_rest';
ALTER TYPE provider_type ADD VALUE IF NOT EXISTS 'subprocess';

-- Provider definitions table: importable JSON driver documents
CREATE TABLE provider_definitions (
  id          BIGSERIAL PRIMARY KEY,
  name        TEXT NOT NULL,
  type_slug   TEXT NOT NULL UNIQUE,
  version     TEXT NOT NULL,
  definition  JSONB NOT NULL,
  source_url  TEXT,
  imported_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Link providers to their definition (NULL for compiled providers)
ALTER TABLE providers
  ADD COLUMN definition_id BIGINT REFERENCES provider_definitions(id)
    ON DELETE RESTRICT;
