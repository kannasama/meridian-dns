-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Seed permissions for snippets, SOA presets, and zone templates

-- Admin: all 12 new permissions
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY[
  'snippets.view', 'snippets.create', 'snippets.edit', 'snippets.delete',
  'soa_presets.view', 'soa_presets.create', 'soa_presets.edit', 'soa_presets.delete',
  'templates.view', 'templates.create', 'templates.edit', 'templates.delete'
]) AS p(perm)
WHERE r.name = 'Admin';

-- Operator: view/create/edit for all three groups (no delete)
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY[
  'snippets.view', 'snippets.create', 'snippets.edit',
  'soa_presets.view', 'soa_presets.create', 'soa_presets.edit',
  'templates.view', 'templates.create', 'templates.edit'
]) AS p(perm)
WHERE r.name = 'Operator';

-- Viewer: view-only for all three groups
INSERT INTO role_permissions (role_id, permission)
SELECT r.id, p.perm
FROM roles r,
UNNEST(ARRAY[
  'snippets.view',
  'soa_presets.view',
  'templates.view'
]) AS p(perm)
WHERE r.name = 'Viewer';
