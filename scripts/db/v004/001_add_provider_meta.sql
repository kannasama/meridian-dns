-- 001_add_provider_meta.sql
-- Adds provider-specific metadata column to records table.

ALTER TABLE records ADD COLUMN provider_meta JSONB;
