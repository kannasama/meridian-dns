-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Display name on users
ALTER TABLE users ADD COLUMN IF NOT EXISTS display_name VARCHAR(200);
