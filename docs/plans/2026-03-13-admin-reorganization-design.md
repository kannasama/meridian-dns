# Admin Reorganization & Auto-Generated IdP URLs

**Date:** 2026-03-13
**Status:** Implemented

## Summary

Reorganized the admin/system configuration pages under a unified "Administration" section in the sidebar, added auto-generated OIDC redirect URI and SAML ACS URL based on a system `app.base_url` setting, and improved the Settings page layout to use horizontal screen space more effectively.

## Changes

### 1. Sidebar Reorganization

The sidebar previously listed admin pages (Users, Groups, Roles, Identity Providers, Settings) as flat items mixed with the main navigation. These have been reorganized under a collapsible **Administration** section header with three items:

- **Auth** — Tabbed page containing Users, Groups, and Permissions (renamed from Roles)
- **Identity Providers** — Standalone page for OIDC/SAML provider configuration
- **Settings** — System configuration with improved two-column layout

The Administration section is only visible to admin users and features a collapsible header with expand/collapse toggle.

### 2. Admin Auth View (Tabbed)

New `AdminAuthView.vue` consolidates three previously separate pages into a single tabbed interface:

| Tab | Content | Previously |
|-----|---------|-----------|
| Users | User management (CRUD, password reset, group assignment) | `UsersView.vue` |
| Groups | Group management with expandable member details | `GroupsView.vue` |
| Permissions | Role management with permission category checkboxes | `RolesView.vue` (renamed) |

Tab state is persisted in the URL query parameter (`?tab=users|groups|permissions`) for deep-linking and browser history support.

### 3. Auto-Generated IdP Callback URLs

#### New System Setting

Added `app.base_url` to `SettingsDef.hpp`:
- **Key:** `app.base_url`
- **Env var:** `DNS_BASE_URL`
- **Default:** empty string
- **Description:** Application base URL for generating callback URLs

#### URL Generation

When `app.base_url` is configured:
- **OIDC Redirect URI:** `{base_url}/auth/callback`
- **SAML ACS URL:** `{base_url}/auth/saml/acs`

When not configured, the UI falls back to deriving the base URL from `window.location` and displays a warning message directing the user to configure `app.base_url` in Settings or via the `DNS_BASE_URL` environment variable.

#### UI Changes

- Redirect URI and ACS URL fields are now **read-only display fields** with copy-to-clipboard buttons
- The auto-generated URLs are shown prominently in the IdP create/edit dialog with helper text
- The IdP list table now shows the callback URL for each provider
- The dialog uses a two-column grid layout for better horizontal space usage

### 4. Settings Page Layout Improvement

The Settings page now uses a **two-column grid layout** (matching the Profile page pattern):

- **Left column:** Application, Session & Security, Deployment sections
- **Right column:** Sync, Audit sections
- **Full-width row:** Paths & Infrastructure section

The new "Application" section at the top of the left column contains the `app.base_url` setting.

### 5. Route Changes

| Old Route | New Route | Behavior |
|-----------|-----------|----------|
| `/users` | `/admin/auth?tab=users` | Redirect |
| `/groups` | `/admin/auth?tab=groups` | Redirect |
| `/roles` | `/admin/auth?tab=permissions` | Redirect |
| `/identity-providers` | `/admin/identity-providers` | Redirect |
| `/settings` | `/admin/settings` | Redirect |

All old routes have backward-compatible redirects to prevent broken bookmarks.

## Files Modified

| File | Change |
|------|--------|
| `include/common/SettingsDef.hpp` | Added `app.base_url` setting (array size 13→14) |
| `ui/src/components/layout/AppSidebar.vue` | Collapsible Administration section with 3 admin nav items |
| `ui/src/views/AdminAuthView.vue` | **New** — Tabbed auth management (Users/Groups/Permissions) |
| `ui/src/views/IdentityProvidersView.vue` | Auto-generated URLs, base_url warning, two-column form layout |
| `ui/src/views/SettingsView.vue` | Two-column layout, new Application section with `app.base_url` |
| `ui/src/router/index.ts` | New `/admin/*` routes, backward-compatible redirects |

## Files Retained (Unused)

The following views are no longer directly routed but are retained for reference:
- `ui/src/views/UsersView.vue`
- `ui/src/views/GroupsView.vue`
- `ui/src/views/RolesView.vue`

These can be removed in a future cleanup pass.
