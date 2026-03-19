-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.
-- v016: Templates & Snippets (Group 1)

-- SOA presets: named SOA timing configurations, support {{var}} in mname/rname
CREATE TABLE soa_presets (
  id             BIGSERIAL PRIMARY KEY,
  name           TEXT NOT NULL UNIQUE,
  mname_template TEXT NOT NULL,
  rname_template TEXT NOT NULL,
  refresh        INTEGER NOT NULL DEFAULT 3600 CHECK (refresh > 0),
  retry          INTEGER NOT NULL DEFAULT 900 CHECK (retry > 0),
  expire         INTEGER NOT NULL DEFAULT 604800 CHECK (expire > 0),
  minimum        INTEGER NOT NULL DEFAULT 300 CHECK (minimum > 0),
  default_ttl    INTEGER NOT NULL DEFAULT 3600 CHECK (default_ttl > 0),
  created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Snippets: named reusable record sets
CREATE TABLE snippets (
  id          BIGSERIAL PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE,
  description TEXT NOT NULL DEFAULT '',
  created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Records belonging to a snippet (no provider_meta — snippets define DNS structure only)
CREATE TABLE snippet_records (
  id             BIGSERIAL PRIMARY KEY,
  snippet_id     BIGINT NOT NULL REFERENCES snippets(id) ON DELETE CASCADE,
  name           TEXT NOT NULL,
  type           TEXT NOT NULL,
  ttl            INTEGER NOT NULL DEFAULT 300 CHECK (ttl > 0),
  value_template TEXT NOT NULL,
  priority       INTEGER NOT NULL DEFAULT 0,
  sort_order     INTEGER NOT NULL DEFAULT 0,
  created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Zone templates: ordered collection of snippets + optional SOA preset
CREATE TABLE zone_templates (
  id            BIGSERIAL PRIMARY KEY,
  name          TEXT NOT NULL UNIQUE,
  description   TEXT NOT NULL DEFAULT '',
  soa_preset_id BIGINT REFERENCES soa_presets(id) ON DELETE SET NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Ordered snippet membership in a template
CREATE TABLE zone_template_snippets (
  template_id BIGINT NOT NULL REFERENCES zone_templates(id) ON DELETE CASCADE,
  snippet_id  BIGINT NOT NULL REFERENCES snippets(id) ON DELETE RESTRICT,
  sort_order  INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (template_id, snippet_id)
);

-- Extend zones with template link, compliance flag, SOA preset, and tags
ALTER TABLE zones
  ADD COLUMN template_id            BIGINT REFERENCES zone_templates(id) ON DELETE SET NULL,
  ADD COLUMN template_check_pending BOOLEAN NOT NULL DEFAULT FALSE,
  ADD COLUMN soa_preset_id          BIGINT REFERENCES soa_presets(id) ON DELETE SET NULL,
  ADD COLUMN tags                   TEXT[] NOT NULL DEFAULT '{}';

CREATE INDEX idx_zones_template_id ON zones(template_id) WHERE template_id IS NOT NULL;
CREATE INDEX idx_zones_tags ON zones USING GIN(tags);
CREATE INDEX idx_zones_template_check_pending ON zones(id) WHERE template_check_pending = TRUE;
CREATE INDEX idx_snippet_records_snippet_id ON snippet_records(snippet_id);
CREATE INDEX idx_zone_template_snippets_template ON zone_template_snippets(template_id, sort_order);
