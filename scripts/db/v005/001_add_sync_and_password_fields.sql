-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v005: Add zone sync status tracking and user force-password-change flag

ALTER TABLE zones ADD COLUMN sync_status VARCHAR(16) NOT NULL DEFAULT 'unknown';
ALTER TABLE zones ADD COLUMN sync_checked_at TIMESTAMP WITH TIME ZONE;

ALTER TABLE users ADD COLUMN force_password_change BOOLEAN NOT NULL DEFAULT FALSE;
