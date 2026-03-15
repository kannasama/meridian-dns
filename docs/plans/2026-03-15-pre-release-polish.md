# Pre-Release Polish — Design Document

**Date:** 2026-03-15
**Status:** Draft
**Scope:** Seven UI/backend improvements prior to v1.0 release

---

## Table of Contents

1. [GitOps Backup Repo Chooser](#1-gitops-backup-repo-chooser)
2. [Action Button Tooltips](#2-action-button-tooltips)
3. [Rename Identity Providers to SSO](#3-rename-identity-providers-to-sso)
4. [IdP Attribute Mapping](#4-idp-attribute-mapping)
5. [Display Name Field](#5-display-name-field)
6. [Audit Log Display Name](#6-audit-log-display-name)
7. [Audit Log Auth & IP Population](#7-audit-log-auth--ip-population)
8. [Schema Changes Summary](#8-schema-changes-summary)

---

## 1. GitOps Backup Repo Chooser

### Problem

The Backup & Restore page has a "Commit to GitOps repository" checkbox, but no way to select
which repo to commit to. The backend reads `backup.git_repo_id` and `backup.git_path` from
`system_config`, but the UI never provides a way to set these values.

### Solution

Add a persistent repo chooser and auto-backup configuration to the Backup & Restore page.

### UI Changes (BackupRestoreView.vue)

Add a **Backup Settings** card at the top of the page, above the three existing sections:

```
┌──────────────────────────────────────────────────────────┐
│ ⚙  Backup Settings                                      │
│                                                          │
│ Git Repository   [▼ Select repository...          ]      │
│                  Backup file committed here on export.    │
│                                                          │
│ Auto-Backup      [▼ Disabled                      ]      │
│                  Options: Disabled, Every 6 hours,       │
│                  Every 12 hours, Every 24 hours           │
│                                                          │
│                                     [ Save Settings ]    │
└──────────────────────────────────────────────────────────┘
```

**Behavior:**
- On mount, fetch `listGitRepos()` to populate the dropdown and `listSettings()` to read
  current `backup.git_repo_id` and `backup.auto_interval_seconds` values
- Save button calls `updateSettings()` with the selected repo ID and interval
- The "Commit to GitOps repository" checkbox in the Export section is **enabled only** when a
  backup repo is configured. When no repo is set, the checkbox is disabled with helper text
  "Select a backup repository in settings above"
- The "Restore from Git Repository" section shows a message when no repo is configured
- `backup.git_path` is **not exposed** in the UI — the default
  `_system/config-backup.json` is sufficient

**Auto-backup interval options:**

| Label | Value (seconds) |
|-------|----------------|
| Disabled | 0 |
| Every 6 hours | 21600 |
| Every 12 hours | 43200 |
| Every 24 hours | 86400 |

### Backend Changes

None — the backend already reads `backup.git_repo_id` and `backup.auto_interval_seconds`
from `system_config` and acts on them. The settings API already supports reading and writing
these values.

### Files Changed

| File | Change |
|------|--------|
| `ui/src/views/BackupRestoreView.vue` | Add settings card, repo dropdown, auto-backup select, conditional enable of commit checkbox |

---

## 2. Action Button Tooltips

### Problem

Icon-only action buttons in DataTables have `v-tooltip` directives in the source code, but
tooltips are not rendering on any page.

### Investigation Required

The directives exist in code (e.g., `v-tooltip.top="'Edit'"`) across all views. The issue is
likely one of:

1. **PrimeVue Tooltip directive not registered globally** — check `ui/src/main.ts` for
   `app.directive('tooltip', Tooltip)` registration
2. **CSS z-index/overflow issue** — DataTable cells may clip tooltips
3. **Missing Tooltip CSS import** — PrimeVue tooltip styles not included

### Fix

**Step 1:** Check `ui/src/main.ts` for tooltip directive registration. PrimeVue 4 requires
explicit registration:

```typescript
import Tooltip from 'primevue/tooltip'
app.directive('tooltip', Tooltip)
```

If missing, add it. This single fix should enable tooltips globally across all views.

**Step 2:** If registration exists, check for CSS conflicts — DataTable column overflow
settings or scoped styles hiding the tooltip overlay.

### Files Changed

| File | Change |
|------|--------|
| `ui/src/main.ts` | Add tooltip directive registration (if missing) |

---

## 3. Rename Identity Providers to SSO

### Scope

UI labels only. API endpoints (`/api/v1/identity-providers`), file names, component names,
and route paths remain unchanged for backward compatibility.

### Changes

| Location | Current | New |
|----------|---------|-----|
| `AppSidebar.vue:22` | "Identity Providers" | "SSO" |
| `IdentityProvidersView.vue` PageHeader | "Identity Providers" | "SSO Providers" |
| `IdentityProvidersView.vue` dialog titles | "Add Identity Provider" / "Edit Identity Provider" | "Add SSO Provider" / "Edit SSO Provider" |
| `IdentityProvidersView.vue` empty state | "No identity providers configured" | "No SSO providers configured" |
| `IdentityProvidersView.vue` delete confirm | References to "identity provider" | "SSO provider" |

### Files Changed

| File | Change |
|------|--------|
| `ui/src/components/layout/AppSidebar.vue` | Rename sidebar label |
| `ui/src/views/IdentityProvidersView.vue` | Rename all user-facing strings |

---

## 4. IdP Attribute Mapping

### Problem

OIDC and SAML attribute extraction is currently hardcoded:

**OIDC (OidcRoutes.cpp:151-154):**
```cpp
std::string sEmail = jPayload.value("email", "");
std::string sUsername = jPayload.value("preferred_username", "");
if (sUsername.empty()) sUsername = jPayload.value("email", sSub);
```

**SAML (SamlRoutes.cpp:183-191):**
```cpp
// email extracted from jAttributes["email"][0]
// username = sNameId, fallback to sEmail
```

Admins with non-standard IdPs cannot map their claim names to Meridian fields.

### Solution

Add per-IdP attribute mapping configuration stored in the `config` JSONB column.

### Mapping Schema (in `config` JSONB)

```json
{
  "attribute_mapping": {
    "username": "preferred_username",
    "email": "email",
    "display_name": "name"
  }
}
```

**Mappable fields:**

| Meridian Field | OIDC Default | SAML Default | Required |
|---------------|-------------|-------------|----------|
| `username` | `preferred_username` | NameID | Yes (falls back to email, then sub/NameID) |
| `email` | `email` | `email` | No |
| `display_name` | `name` | `displayName` | No |

### Backend Changes

**OidcRoutes.cpp** — Replace hardcoded claim extraction with mapping lookup:

```cpp
auto jMapping = oIdp->jConfig.value("attribute_mapping", nlohmann::json::object());
std::string sEmailClaim = jMapping.value("email", "email");
std::string sUsernameClaim = jMapping.value("username", "preferred_username");
std::string sDisplayNameClaim = jMapping.value("display_name", "name");

std::string sEmail = jPayload.value(sEmailClaim, "");
std::string sUsername = jPayload.value(sUsernameClaim, "");
std::string sDisplayName = jPayload.value(sDisplayNameClaim, "");
if (sUsername.empty()) sUsername = jPayload.value("email", sSub);
```

**SamlRoutes.cpp** — Same pattern using SAML attribute names:

```cpp
auto jMapping = oIdp->jConfig.value("attribute_mapping", nlohmann::json::object());
std::string sEmailAttr = jMapping.value("email", "email");
std::string sUsernameAttr = jMapping.value("username", "");  // empty = use NameID
std::string sDisplayNameAttr = jMapping.value("display_name", "displayName");
```

**FederatedAuthService** — Add `sDisplayName` parameter to `processFederatedLogin()`,
populate on user record (see item 5).

### UI Changes (IdentityProvidersView.vue)

Add an **Attribute Mapping** section to the IdP form dialog, below the type-specific
configuration fields:

```
┌──────────────────────────────────────────────────────────┐
│ Attribute Mapping                                        │
│                                                          │
│ Username   [ preferred_username  ]  OIDC claim / SAML    │
│ Email      [ email               ]  attribute name that  │
│ Display    [ name                ]  maps to each field    │
│ Name                                                     │
│                                                          │
│ Defaults shown are standard OIDC claims.                 │
│ Use the Test button to see your IdP's actual claims.     │
└──────────────────────────────────────────────────────────┘
```

- Pre-populated with OIDC or SAML defaults based on type selection
- Empty fields use the default mapping
- Test authentication result panel already shows `all_claims` — admins reference this to
  fill in correct claim names

### Files Changed

| File | Change |
|------|--------|
| `src/api/routes/OidcRoutes.cpp` | Use configurable claim names from `attribute_mapping` |
| `src/api/routes/SamlRoutes.cpp` | Use configurable attribute names from `attribute_mapping` |
| `src/security/FederatedAuthService.hpp` | Add `sDisplayName` parameter |
| `src/security/FederatedAuthService.cpp` | Pass display name to user creation/update |
| `ui/src/views/IdentityProvidersView.vue` | Add attribute mapping form fields |

---

## 5. Display Name Field

### Problem

Users are identified only by `username` throughout the UI. There is no display name for
showing a human-friendly label (especially important for federated users whose username may
be a UUID or email).

### Schema Change

```sql
ALTER TABLE users ADD COLUMN display_name VARCHAR(200);
```

Nullable — when NULL, UI falls back to `username`.

### Backend Changes

**UserRow struct** (`include/dal/UserRepository.hpp`):
```cpp
std::optional<std::string> osDisplayName;
```

**UserRepository:**
- `findById()`, `findByUsername()`, `listAll()` — include `display_name` in SELECT
- `create()` — accept optional display_name
- `update()` — accept optional display_name
- New: `updateDisplayName(iUserId, sDisplayName)`
- `createFederated()` — accept optional display_name

**FederatedAuthService:**
- `processFederatedLogin()` gains `sDisplayName` parameter
- On user creation: set `display_name` from IdP claim
- On subsequent login: update `display_name` if changed (same as email sync)

**API responses** — all user-related endpoints include `display_name`:
- `GET /me` — add `display_name`
- `GET /users` — add `display_name`
- `GET/PUT /profile` — add `display_name` (editable for local users)

### UI Changes

**AppTopBar.vue:**
```
Current:  [mjhill@mjhnosekai.com]  [admin]
New:      [Matt Hill]              [admin]
                                          (falls back to username if no display name)
```

Show `auth.user?.display_name || auth.user?.username` in the top bar and user menu label.

**ProfileView.vue:**
- Add "Display Name" field in the Account Information card
- Editable for all users (local and federated)

**User type** (`ui/src/types/index.ts`):
```typescript
export interface User {
  // ... existing fields
  display_name: string | null
}
```

**Auth store** — `hydrate()` populates `display_name` from `/me` response.

### Files Changed

| File | Change |
|------|--------|
| `scripts/db/v0XX/001_add_display_name.sql` | Add `display_name` column |
| `include/dal/UserRepository.hpp` | Add `osDisplayName` to UserRow |
| `src/dal/UserRepository.cpp` | Include display_name in CRUD |
| `include/security/FederatedAuthService.hpp` | Add display_name parameter |
| `src/security/FederatedAuthService.cpp` | Set/update display_name on federated login |
| `src/api/routes/AuthRoutes.cpp` | Include display_name in `/me` response |
| `src/api/routes/UserRoutes.cpp` | Include display_name in user CRUD |
| `src/api/routes/ProfileRoutes.cpp` | Editable display_name in profile |
| `ui/src/types/index.ts` | Add display_name to User interface |
| `ui/src/stores/auth.ts` | Populate display_name from /me |
| `ui/src/components/layout/AppTopBar.vue` | Show display name with username fallback |
| `ui/src/views/ProfileView.vue` | Add display name field |
| `ui/src/views/UsersView.vue` | Show display name in user management |

---

## 6. Audit Log Display Name

### Problem

Audit log shows `identity` (username) in the User column. With display names added, audit
entries should show a more readable user reference.

### Solution — Snapshot at Insert Time

Store a formatted identity string at audit insert time so the log reflects who the user was
at the time of the action, not who they are now.

### Backend Changes

**Identity format:** `Display Name (username)` when display name is available, otherwise
just `username`.

**AuditRepository::insert()** — no signature change needed. The caller constructs the
identity string.

**Route helpers** — add a utility function:

```cpp
std::string formatAuditIdentity(const RequestContext& rcCtx) {
  if (rcCtx.sDisplayName.empty()) return rcCtx.sUsername;
  return rcCtx.sDisplayName + " (" + rcCtx.sUsername + ")";
}
```

**RequestContext** — add `sDisplayName` field, populated by `AuthMiddleware` from the JWT
(which will include display_name after item 5 is implemented).

**All audit insert call sites** — use `formatAuditIdentity(rcCtx)` instead of
`rcCtx.sUsername` for the identity parameter.

### UI Changes

None needed — the User column already displays the `identity` field as-is.

### Files Changed

| File | Change |
|------|--------|
| `include/common/Types.hpp` | Add `sDisplayName` to RequestContext |
| `include/api/RouteHelpers.hpp` | Add `formatAuditIdentity()` |
| `src/api/RouteHelpers.cpp` | Implement `formatAuditIdentity()` |
| `src/api/AuthMiddleware.cpp` | Populate `sDisplayName` from JWT |
| `src/security/AuthService.cpp` | Include `display_name` in JWT payload |
| All route files with audit inserts | Use `formatAuditIdentity(rcCtx)` |

---

## 7. Audit Log Auth & IP Population

### Problem

The `audit_log` table has `auth_method` and `ip_address` columns, and the UI renders them
in expanded audit rows, but all backend audit insert calls pass `std::nullopt` for both.

### Solution

Thread auth method and client IP through `RequestContext` to all audit insert calls.

### Backend Changes

**RequestContext** (`include/common/Types.hpp`):
```cpp
struct RequestContext {
  int64_t iUserId = 0;
  std::string sUsername;
  std::string sDisplayName;       // Added in item 6
  std::string sRole;
  std::string sAuthMethod;        // Already exists
  std::string sIpAddress;         // NEW
  std::unordered_set<std::string> vPermissions;
};
```

**AuthMiddleware** — populate `sIpAddress` from the request:

```cpp
// X-Forwarded-For aware IP resolution
std::string sIp = req.get_header_value("X-Forwarded-For");
if (!sIp.empty()) {
  // Take first IP in chain (original client)
  auto pos = sIp.find(',');
  if (pos != std::string::npos) sIp = sIp.substr(0, pos);
  // Trim whitespace
  sIp.erase(0, sIp.find_first_not_of(' '));
  sIp.erase(sIp.find_last_not_of(' ') + 1);
} else {
  sIp = req.remote_ip_address;
}
rcCtx.sIpAddress = sIp;
```

**All audit insert call sites** — replace `std::nullopt, std::nullopt` with
`rcCtx.sAuthMethod, rcCtx.sIpAddress`:

| File | Lines | Count |
|------|-------|-------|
| `src/api/routes/RecordRoutes.cpp` | 116, 188, 222, 251, 524 | 5 |
| `src/core/DeploymentEngine.cpp` | 296, 367 | 2 |
| `src/core/RollbackEngine.cpp` | 95 | 1 |

**Note on engine call sites:** `DeploymentEngine` and `RollbackEngine` receive `sActor`
(username string) but not a `RequestContext`. Two options:

- **Option A:** Pass `RequestContext` (or a struct with auth_method + ip) to engine methods
- **Option B:** Add `sAuthMethod` and `sIpAddress` parameters to engine methods alongside
  `sActor`

**Recommended: Option A** — pass the full `RequestContext` or a lightweight
`AuditContext { sIdentity, sAuthMethod, sIpAddress }` struct. This avoids proliferating
string parameters and makes it easy to add more audit context in the future.

```cpp
struct AuditContext {
  std::string sIdentity;    // Formatted display name (username) string
  std::string sAuthMethod;
  std::string sIpAddress;
};
```

Engine methods accept `const AuditContext& auCtx` instead of `const std::string& sActor`.

### UI Changes

**AuditView.vue** — the expanded row detail-meta section already renders `auth_method` and
`ip_address`. No changes needed — once populated, the values will appear automatically.

Consider adding fallback display for empty values:

```vue
<span>Auth: {{ data.auth_method || '—' }}</span>
<span>IP: {{ data.ip_address || '—' }}</span>
```

### Files Changed

| File | Change |
|------|--------|
| `include/common/Types.hpp` | Add `sIpAddress` to RequestContext, add `AuditContext` struct |
| `src/api/AuthMiddleware.cpp` | Populate `sIpAddress` with X-Forwarded-For awareness |
| `src/api/routes/RecordRoutes.cpp` | Pass auth_method and ip_address to audit inserts |
| `src/api/routes/DeploymentRoutes.cpp` | Build AuditContext and pass to engines |
| `src/core/DeploymentEngine.hpp` | Accept AuditContext instead of sActor |
| `src/core/DeploymentEngine.cpp` | Use AuditContext for audit inserts |
| `src/core/RollbackEngine.hpp` | Accept AuditContext instead of sActor |
| `src/core/RollbackEngine.cpp` | Use AuditContext for audit inserts |
| `ui/src/views/AuditView.vue` | Add dash fallback for empty auth/IP values |

---

## 8. Schema Changes Summary

Single migration script (next version number after current latest):

```sql
-- Display name on users
ALTER TABLE users ADD COLUMN display_name VARCHAR(200);
```

No other schema changes required — attribute mapping is stored in the existing `config` JSONB
column on `identity_providers`, and all other changes are code-only.

---

## Implementation Order

Items are loosely independent but have some dependencies:

1. **Item 5 (Display Name)** — schema change, backend plumbing — do first
2. **Item 4 (Attribute Mapping)** — depends on display name field existing
3. **Item 6 (Audit Display Name)** — depends on display name in RequestContext
4. **Item 7 (Audit Auth & IP)** — can be done alongside item 6 (same files)
5. **Item 2 (Tooltips)** — independent, quick fix
6. **Item 3 (SSO Rename)** — independent, string replacements
7. **Item 1 (Backup Repo Chooser)** — independent, UI only

Items 2, 3, and 1 can be done in parallel with items 5-7.
