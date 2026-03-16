-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v011: Move role_id from group_members to groups
-- Each group has exactly one role; all members inherit that role's permissions.

-- 1. Add role_id column to groups (nullable initially)
ALTER TABLE groups ADD COLUMN role_id INTEGER REFERENCES roles(id);

-- 2. Populate from the most common role_id in each group's members
UPDATE groups g
SET role_id = sub.role_id
FROM (
  SELECT gm.group_id,
         gm.role_id,
         ROW_NUMBER() OVER (PARTITION BY gm.group_id ORDER BY COUNT(*) DESC, gm.role_id) AS rn
  FROM group_members gm
  GROUP BY gm.group_id, gm.role_id
) sub
WHERE sub.group_id = g.id AND sub.rn = 1;

-- 3. For groups with no members, default to the Viewer role
UPDATE groups
SET role_id = (SELECT id FROM roles WHERE name = 'Viewer')
WHERE role_id IS NULL;

-- 4. Make role_id NOT NULL
ALTER TABLE groups ALTER COLUMN role_id SET NOT NULL;

-- 5. Drop indexes that reference role_id/scope columns on group_members
DROP INDEX IF EXISTS idx_group_members_role_scope;
DROP INDEX IF EXISTS idx_group_members_unique_scope;

-- 6. Drop the old composite primary key (user_id, group_id, role_id)
ALTER TABLE group_members DROP CONSTRAINT group_members_pkey;

-- 7. Remove duplicate rows before adding new PK (keep one row per user+group)
DELETE FROM group_members gm
WHERE ctid NOT IN (
  SELECT MIN(ctid)
  FROM group_members
  GROUP BY user_id, group_id
);

-- 8. Drop columns from group_members
ALTER TABLE group_members DROP COLUMN role_id;
ALTER TABLE group_members DROP COLUMN scope_type;
ALTER TABLE group_members DROP COLUMN scope_id;

-- 9. Restore simple (user_id, group_id) primary key
ALTER TABLE group_members ADD PRIMARY KEY (user_id, group_id);
