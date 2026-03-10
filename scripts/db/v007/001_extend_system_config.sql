-- Workstream 2: Extend system_config for DB-backed settings
ALTER TABLE system_config ADD COLUMN IF NOT EXISTS description TEXT;
ALTER TABLE system_config ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ DEFAULT now();
