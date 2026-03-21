-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Seed permissions for provider definitions (v018 pluggable providers)

-- Admin: all 4 provider definition permissions
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY[
  'provider_definitions.view', 'provider_definitions.create',
  'provider_definitions.edit', 'provider_definitions.delete'
]) AS p(perm)
WHERE r.name = 'Admin';

-- Operator: view/create/edit (no delete)
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY[
  'provider_definitions.view', 'provider_definitions.create',
  'provider_definitions.edit'
]) AS p(perm)
WHERE r.name = 'Operator';

-- Viewer: view-only
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY['provider_definitions.view']) AS p(perm)
WHERE r.name = 'Viewer';
