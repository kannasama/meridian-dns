-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Add last_used_at column to api_keys table
ALTER TABLE api_keys ADD COLUMN last_used_at TIMESTAMPTZ;
