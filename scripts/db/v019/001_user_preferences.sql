-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v019: User Preferences

-- User preferences: per-user key-value settings stored as JSONB
CREATE TABLE user_preferences (
  user_id   BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  key       TEXT NOT NULL,
  value     JSONB NOT NULL DEFAULT '""',
  PRIMARY KEY (user_id, key)
);
