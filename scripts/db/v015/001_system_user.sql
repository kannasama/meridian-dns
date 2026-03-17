-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.
-- System user for automated operations (auto-capture, maintenance tasks).
-- Uses auth_method='local' with an empty password_hash, which prevents login
-- (Argon2id verification fails against an empty/non-PHC-format string).
INSERT INTO users (username, email, password_hash, is_active, auth_method)
VALUES ('_system', 'system@localhost', '', true, 'local')
ON CONFLICT (username) DO NOTHING;
