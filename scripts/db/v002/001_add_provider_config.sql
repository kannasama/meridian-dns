-- 001_add_provider_config.sql
-- Adds encrypted_config column for provider-specific parameters.

ALTER TABLE providers ADD COLUMN encrypted_config TEXT NOT NULL DEFAULT '';
