-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v020: System Logs — persistent technical logging for deployments, provider
-- operations, and general application events.

CREATE TABLE system_logs (
  id            BIGSERIAL    PRIMARY KEY,
  category      TEXT         NOT NULL,   -- 'deployment', 'provider', 'system'
  severity      TEXT         NOT NULL,   -- 'info', 'warn', 'error'
  zone_id       BIGINT       REFERENCES zones(id) ON DELETE SET NULL,
  provider_id   BIGINT       REFERENCES providers(id) ON DELETE SET NULL,
  operation     TEXT,                    -- 'create_record', 'update_record', 'delete_record', etc.
  record_name   TEXT,
  record_type   TEXT,
  success       BOOLEAN,
  status_code   INTEGER,                -- HTTP status from provider (nullable)
  message       TEXT         NOT NULL,   -- human-readable summary
  detail        TEXT,                    -- raw response body / technical detail
  created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_system_logs_created_at ON system_logs(created_at DESC);
CREATE INDEX idx_system_logs_category ON system_logs(category, created_at DESC);
CREATE INDEX idx_system_logs_zone_id ON system_logs(zone_id, created_at DESC);
CREATE INDEX idx_system_logs_severity ON system_logs(severity, created_at DESC);
