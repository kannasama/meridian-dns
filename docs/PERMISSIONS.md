# Permissions Guide

## Overview

Meridian DNS uses a granular permission model:

1. **Permissions** — Discrete actions (e.g. `zones.create`, `records.delete`)
2. **Roles** — Named collections of permissions (e.g. "Admin", "Operator")
3. **Groups** — Collections of users, each group membership carries a role
4. **Scoping** — All permissions are **global** in v1.0. View/zone scoping is planned for a future release.

## Permission Strings

### Zones

| Permission | Description |
|------------|-------------|
| `zones.view` | View zone list and details |
| `zones.create` | Create new zones |
| `zones.edit` | Edit zone configuration |
| `zones.delete` | Delete zones |
| `zones.deploy` | Deploy zone changes to providers |
| `zones.rollback` | Roll back to previous deployment |

### Records

| Permission | Description |
|------------|-------------|
| `records.view` | View DNS records |
| `records.create` | Create new records |
| `records.edit` | Edit existing records |
| `records.delete` | Delete records |
| `records.import` | Import records (CSV, JSON, DNSControl) |

### Providers

| Permission | Description |
|------------|-------------|
| `providers.view` | View provider list and status |
| `providers.create` | Add new DNS providers |
| `providers.edit` | Edit provider configuration |
| `providers.delete` | Remove providers |

### Views

| Permission | Description |
|------------|-------------|
| `views.view` | View split-horizon views |
| `views.create` | Create new views |
| `views.edit` | Edit view configuration |
| `views.delete` | Delete views |

### Variables

| Permission | Description |
|------------|-------------|
| `variables.view` | View template variables |
| `variables.create` | Create new variables |
| `variables.edit` | Edit variable values |
| `variables.delete` | Delete variables |

### Git Repos

| Permission | Description |
|------------|-------------|
| `repos.view` | View Git repository list |
| `repos.create` | Add new Git repositories |
| `repos.edit` | Edit repository configuration |
| `repos.delete` | Remove repositories |

### Audit

| Permission | Description |
|------------|-------------|
| `audit.view` | View audit log entries |
| `audit.export` | Export audit logs (NDJSON) |
| `audit.purge` | Manually purge old audit entries |

### Users

| Permission | Description |
|------------|-------------|
| `users.view` | View user list |
| `users.create` | Create new users |
| `users.edit` | Edit user accounts |
| `users.delete` | Delete user accounts |

### Groups

| Permission | Description |
|------------|-------------|
| `groups.view` | View group list |
| `groups.create` | Create new groups |
| `groups.edit` | Edit groups and membership |
| `groups.delete` | Delete groups |

### Roles

| Permission | Description |
|------------|-------------|
| `roles.view` | View role list and permissions |
| `roles.create` | Create custom roles |
| `roles.edit` | Edit role permissions |
| `roles.delete` | Delete custom roles |

### Settings

| Permission | Description |
|------------|-------------|
| `settings.view` | View system settings |
| `settings.edit` | Modify system settings |

### Backup

| Permission | Description |
|------------|-------------|
| `backup.create` | Export system backup |
| `backup.restore` | Restore from backup |

## Default Roles

### Admin

All 44 permissions. Full system access.

### Operator

All permissions except administrative functions:
- Excludes: `users.*`, `groups.*`, `roles.*`, `settings.edit`, `backup.restore`

### Viewer

Read-only access:
- `*.view` permissions for all resource types
- `audit.export` for compliance reporting

## Custom Roles

1. Navigate to **Roles** in the admin UI
2. Click **Create Role**
3. Set a name and select permissions from the checkbox grid
4. Save — the role is available for group assignments

## Groups and Role Assignment

Users gain permissions through group membership:

1. Create a **Group** (e.g. "DNS Operators")
2. Add **Members** to the group, each with a **Role**
3. Members inherit all permissions from the assigned role

A user can belong to multiple groups with different roles. Permissions are
combined (union semantics).

## Permission Scoping

### v1.0: Global Permissions

In v1.0, all permissions are **global**. A user with `zones.edit` can edit all
zones regardless of view or zone assignment. The data model supports scoped
permissions (the `group_members` table has `scope_view_id` and `scope_zone_id`
columns), but enforcement is not yet implemented.

```json
{"user_id": 1, "group_id": 2, "role_id": 3}
```

The user has the role's permissions across all views and zones.

> **Planned Enhancement:** View-level and zone-level scoped permissions are
> planned for a future release. When implemented, group memberships will support
> optional `scope_view_id` and `scope_zone_id` fields to restrict where
> permissions apply.

## Resolution Logic

When checking if a user can perform an action:

1. Collect all group memberships for the user
2. For each membership, check if the assigned role includes the required permission
3. **Union semantics** — if any membership grants the permission, access is allowed

## Common Patterns

### DNS Operator Team

1. Create role "Zone Operator" with: `zones.*`, `records.*`, `variables.view`
2. Create group "DNS Operators"
3. Add users with "Zone Operator" role

### Read-Only Auditor

1. Use the built-in "Viewer" role
2. Create group "Auditors"
3. Add users with "Viewer" role
