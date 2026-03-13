# Group → Role Restructure Design

**Date:** 2026-03-13
**Status:** Approved

## Problem

The v008 permissions restructure placed `role_id` on the `group_members` table, creating a per-member role assignment model. This causes several issues:

1. **Federated auth hardcodes Viewer** — `FederatedAuthService::processFederatedLogin()` always assigns the Viewer role when adding users to groups via IdP mapping, because the mapping rules only specify `meridian_group_id` with no role.
2. **No UI for group-role assignment** — There is no UI component to manage the relationship between groups and roles.
3. **Conceptual mismatch** — Permission mapping should be between Group and Role, not between individual group memberships and roles. The `role_id` in `group_members` adds unnecessary complexity.

## Decision

Move `role_id` from `group_members` to `groups`. Each group has exactly one associated role. All members of a group inherit that role's permissions. Roles remain as reusable permission templates.

## Schema

### Target State

```sql
groups
  id            SERIAL PRIMARY KEY
  name          VARCHAR(100) UNIQUE NOT NULL
  description   TEXT
  role_id       INTEGER NOT NULL REFERENCES roles(id)
  is_system     BOOLEAN NOT NULL DEFAULT false
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()

group_members
  user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE
  group_id      BIGINT NOT NULL REFERENCES groups(id) ON DELETE CASCADE
  PRIMARY KEY (user_id, group_id)

roles              -- unchanged
role_permissions   -- unchanged
```

### Migration (v011)

1. Add `role_id` column to `groups` (nullable initially)
2. Populate from the most common `role_id` in each group's `group_members` rows; default to Viewer role for groups with no members
3. Make `role_id NOT NULL`
4. Drop indexes on `group_members` that reference `role_id`/`scope_type`/`scope_id`
5. Drop `role_id`, `scope_type`, `scope_id` columns from `group_members`
6. Drop old primary key, restore simple `(user_id, group_id)` primary key

## Backend Changes

### GroupRepository

- `GroupRow` gains `iRoleId` and `sRoleName` fields
- `GroupMemberRow` simplifies to `(iUserId, sUsername)` — loses role/scope fields
- `addMember(iGroupId, iUserId)` — no role/scope params
- `removeMember(iGroupId, iUserId)` — no role/scope params
- `create(sName, sDescription, iRoleId)` — gains role param
- `update(iGroupId, sName, sDescription, iRoleId)` — gains role param
- List/find queries JOIN `roles` to include role name

### RoleRepository

- `resolveUserPermissions(iUserId)` simplifies:
  ```sql
  SELECT DISTINCT rp.permission
  FROM group_members gm
  JOIN groups g ON g.id = gm.group_id
  JOIN role_permissions rp ON rp.role_id = g.role_id
  WHERE gm.user_id = $1
  ```
- `getHighestRoleName(iUserId)` similarly queries through `groups.role_id`
- Remove `iViewId`/`iZoneId` scope parameters

### FederatedAuthService

- `processFederatedLogin()` calls `addMember(groupId, userId)` — no role needed
- The group already carries its role; user inherits it automatically
- Remove Viewer role lookup and hardcoded assignment

### API Routes

- `POST /api/v1/groups` — body accepts `role_id` (required)
- `PUT /api/v1/groups/:id` — body accepts `role_id`
- `GET /api/v1/groups` — response includes `role_id` and `role_name`
- Group member add/remove endpoints lose role/scope parameters
- User create/update `group_memberships` simplifies to `group_ids: number[]`

## UI Changes

### AdminAuthView — Groups Tab

- Group create/edit form gains a **Role** dropdown (required, populated from roles list)
- Group list table shows the role name column
- Member management simplifies — add/remove is just selecting a user

### AdminAuthView — Permissions Tab

- Change role create/edit from **Drawer** (sidebar) to **Dialog** (modal) — the permissions grid with categories and checkboxes needs more horizontal space than a sidebar provides
- No other structural changes (roles list already shows permissions after Bug 4 fix)

### IdentityProvidersView — IdP Mapping Rules

- No changes needed — mapping rules stay as `idp_group` → `meridian_group_id`
- The group's role determines the federated user's permissions automatically
- Revert the partial `role_id` addition to `GroupMappingRule` type

### TypeScript Types

- `Group` gains `role_id: number` and `role_name: string`
- `GroupMappingRule` stays as `{ idp_group: string, meridian_group_id: number }`
- Remove role/scope fields from group member types

## Permission Resolution

A user's effective permissions are the **union** of all permissions from all groups they belong to:

```
User → group_members → groups → roles → role_permissions → permissions
```

The highest-privilege role name (for display in JWT/UI) is determined by a priority ordering: Admin > Operator > Viewer > custom roles.

## Test Impact

- Update `test_group_repository.cpp` — remove role/scope from member operations
- Update `test_role_repository.cpp` — update permission resolution tests
- Update `test_federated_auth_service.cpp` — remove Viewer role hardcoding
- Update `test_crud_routes.cpp` — update group API tests
- Update `test_role_routes.cpp` — if affected by permission resolution changes

## Also Fixed (Pre-requisite Commits)

These fixes were committed before this design:

1. **Bug 4 — Roles show 0 permissions:** `GET /api/v1/roles` now includes `permissions` array
2. **Revert partial `role_id` on `GroupMappingRule`:** The type change is unnecessary with this design
