-- v005: Add zone sync status tracking and user force-password-change flag

ALTER TABLE zones ADD COLUMN sync_status VARCHAR(16) NOT NULL DEFAULT 'unknown';
ALTER TABLE zones ADD COLUMN sync_checked_at TIMESTAMP WITH TIME ZONE;

ALTER TABLE users ADD COLUMN force_password_change BOOLEAN NOT NULL DEFAULT FALSE;
