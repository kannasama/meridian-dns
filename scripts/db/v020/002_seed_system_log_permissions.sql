-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Seed system_logs.view permission for Admin role (admin-only feature)

INSERT INTO role_permissions (role_id, permission)
SELECT r.id, 'system_logs.view'
FROM roles r
WHERE r.name = 'Admin'
  AND NOT EXISTS (
    SELECT 1 FROM role_permissions rp
    WHERE rp.role_id = r.id AND rp.permission = 'system_logs.view'
  );
