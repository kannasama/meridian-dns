-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v017: Dynamic Variables + Tags vocabulary

-- Variable kind enum
CREATE TYPE variable_kind AS ENUM ('static', 'dynamic');

-- Add kind and dynamic_format to variables; existing rows default to 'static'
ALTER TABLE variables
  ADD COLUMN variable_kind variable_kind NOT NULL DEFAULT 'static',
  ADD COLUMN dynamic_format TEXT;

-- Dynamic variables must have a format string
ALTER TABLE variables
  ADD CONSTRAINT variables_dynamic_format_check
    CHECK (variable_kind = 'static' OR dynamic_format IS NOT NULL);

-- Index for efficient kind-filtered queries
CREATE INDEX ON variables (variable_kind);

-- Tags vocabulary table (for Group 3 zone tags; zones.tags TEXT[] already exists from v016)
CREATE TABLE tags (
  id         BIGSERIAL PRIMARY KEY,
  name       TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Serial counter keys in existing system_config table
INSERT INTO system_config (key, value, description)
VALUES
  ('serial_counter_date', '',  'Last UTC date a SOA serial was issued (YYYYMMDD)'),
  ('serial_counter_seq',  '0', 'Two-digit serial suffix for current date (0-99)')
ON CONFLICT (key) DO NOTHING;
