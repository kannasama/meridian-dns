# Pre-Release Polish — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement seven UI/backend improvements prior to v1.0 release, covering display names, IdP attribute mapping, audit log population, tooltip fixes, SSO rename, and backup repo chooser.

**Architecture:** Schema adds `display_name` to `users` table. Backend threads display name and IP through `RequestContext` → JWT → `AuditContext`. IdP attribute mapping reads from existing `config` JSONB on `identity_providers`. UI changes are string renames, tooltip fix, and a new backup settings card.

**Tech Stack:** C++20 (Crow, libpqxx, nlohmann/json, spdlog), Vue 3 + TypeScript + PrimeVue, PostgreSQL

**Build Environment:**
- Local C++ builds are **not available** — use `sg docker 'docker buildx build ...'` for application builds
- Local UI builds **are available** — use `cd ui && npm run build` and `vue-tsc -b` for type checking
- Docker permission workaround: prefix docker commands with `sg docker '...'`
- Use `vue-tsc -b` (not `--noEmit`) for TypeScript verification to match Docker build strictness

**Design Document:** `docs/plans/2026-03-15-pre-release-polish.md`

---

## Task 1: Database Migration — Add `display_name` Column

**Files:**
- Create: `scripts/db/v014/001_add_display_name.sql`

**Step 1: Write the migration script**

```sql
-- Display name on users
ALTER TABLE users ADD COLUMN IF NOT EXISTS display_name VARCHAR(200);
```

**Step 2: Verify migration file is syntactically valid**

Run: `cat scripts/db/v014/001_add_display_name.sql`
Expected: The SQL statement above, no syntax errors

**Step 3: Commit**

```bash
git add scripts/db/v014/001_add_display_name.sql
git commit -m "feat: add display_name column to users (v014 migration)"
```

---

## Task 2: Backend — Add `display_name` to UserRow and UserRepository

**Files:**
- Modify: `include/dal/UserRepository.hpp`
- Modify: `src/dal/UserRepository.cpp` (not in tree listing but exists at this path)

**Step 1: Add `osDisplayName` to UserRow struct**

In `include/dal/UserRepository.hpp`, add to the `UserRow` struct after `bForcePasswordChange`:

```cpp
std::optional<std::string> osDisplayName;
```

Add `#include <optional>` if not already present (it is — line 4).

**Step 2: Update UserRepository methods to include display_name**

In `src/dal/UserRepository.cpp`, update all SELECT statements in:
- `findByUsername()` — add `display_name` to SELECT, populate `row.osDisplayName`
- `findById()` — same
- `findByOidcSub()` — same
- `findBySamlNameId()` — same
- `listAll()` — same

For each method, the pattern is:
```cpp
// Add to SELECT: ..., display_name
// After row population:
if (!r["display_name"].is_null()) {
  row.osDisplayName = r["display_name"].as<std::string>();
}
```

**Step 3: Add `osDisplayName` parameter to `create()` and `createFederated()`**

Update signatures:
```cpp
int64_t create(const std::string& sUsername, const std::string& sEmail,
               const std::string& sPasswordHash,
               const std::optional<std::string>& osDisplayName = std::nullopt);

int64_t createFederated(const std::string& sUsername, const std::string& sEmail,
                        const std::string& sAuthMethod,
                        const std::string& sOidcSub, const std::string& sSamlNameId,
                        const std::optional<std::string>& osDisplayName = std::nullopt);
```

In the implementations, include `display_name` in the INSERT if present.

**Step 4: Add `updateDisplayName()` method**

```cpp
// Header
void updateDisplayName(int64_t iUserId, const std::optional<std::string>& osDisplayName);

// Implementation
void UserRepository::updateDisplayName(int64_t iUserId,
                                        const std::optional<std::string>& osDisplayName) {
  auto cg = _cpPool.acquire();
  pqxx::work txn(cg.connection());
  if (osDisplayName.has_value()) {
    txn.exec_params("UPDATE users SET display_name = $1 WHERE id = $2",
                    osDisplayName.value(), iUserId);
  } else {
    txn.exec_params("UPDATE users SET display_name = NULL WHERE id = $2",
                    iUserId);
  }
  txn.commit();
}
```

**Step 5: Update `update()` to accept optional display_name**

```cpp
void update(int64_t iUserId, const std::string& sEmail, bool bIsActive,
            const std::optional<std::string>& osDisplayName = std::nullopt);
```

Include `display_name = $N` in the UPDATE if provided.

**Step 6: Add `updateFederatedDisplayName()` for federated login sync**

Create a method that updates display_name during federated login (same as `updateFederatedEmail` pattern):

```cpp
void updateFederatedDisplayName(int64_t iUserId, const std::string& sDisplayName);
```

**Step 7: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 8: Commit**

```bash
git add include/dal/UserRepository.hpp src/dal/UserRepository.cpp
git commit -m "feat: add display_name to UserRow and UserRepository"
```

---

## Task 3: Backend — Add `display_name` to FederatedAuthService

**Files:**
- Modify: `include/security/FederatedAuthService.hpp`
- Modify: `src/security/FederatedAuthService.cpp`

**Step 1: Add `sDisplayName` parameter to `processFederatedLogin()`**

In the header, update the signature:
```cpp
LoginResult processFederatedLogin(
    const std::string& sAuthMethod,
    const std::string& sFederatedId,
    const std::string& sUsername,
    const std::string& sEmail,
    const std::string& sDisplayName,
    const std::vector<std::string>& vIdpGroups,
    const nlohmann::json& jGroupMappings,
    int64_t iDefaultGroupId);
```

**Step 2: Update the implementation**

In `src/security/FederatedAuthService.cpp`:

- On user creation: pass `sDisplayName` to `createFederated()`:
  ```cpp
  std::optional<std::string> osDisplayName =
      sDisplayName.empty() ? std::nullopt : std::optional<std::string>(sDisplayName);
  int64_t iUserId = _urRepo.createFederated(sUsername, sEmail, sAuthMethod,
                                             sOidcSub, sSamlNameId, osDisplayName);
  ```

- On existing user login: update display_name if changed:
  ```cpp
  if (!sDisplayName.empty() &&
      (!oUser->osDisplayName.has_value() || oUser->osDisplayName.value() != sDisplayName)) {
    _urRepo.updateFederatedDisplayName(oUser->iId, sDisplayName);
  }
  ```

**Step 3: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds (callers will need fixing — they pass wrong arg count; fix in Task 4)

**Step 4: Commit**

```bash
git add include/security/FederatedAuthService.hpp src/security/FederatedAuthService.cpp
git commit -m "feat: thread display_name through FederatedAuthService"
```

---

## Task 4: Backend — Add IdP Attribute Mapping + Wire Display Name in OIDC/SAML Routes

**Files:**
- Modify: `src/api/routes/OidcRoutes.cpp`
- Modify: `src/api/routes/SamlRoutes.cpp`

**Step 1: Update OidcRoutes.cpp — configurable claim extraction**

At `src/api/routes/OidcRoutes.cpp:149-155`, replace the hardcoded claim extraction with:

```cpp
// Read attribute mapping from IdP config
auto jMapping = oIdp->jConfig.value("attribute_mapping", nlohmann::json::object());
std::string sEmailClaim = jMapping.value("email", "email");
std::string sUsernameClaim = jMapping.value("username", "preferred_username");
std::string sDisplayNameClaim = jMapping.value("display_name", "name");

// Extract claims using configurable mapping
std::string sSub = jPayload.value("sub", "");
std::string sEmail = jPayload.value(sEmailClaim, "");
std::string sUsername = jPayload.value(sUsernameClaim, "");
std::string sDisplayName = jPayload.value(sDisplayNameClaim, "");
if (sUsername.empty()) {
  sUsername = jPayload.value("email", sSub);
}
```

Update the `processFederatedLogin` call at line ~178 to include `sDisplayName`:

```cpp
auto lr = _fasService.processFederatedLogin(
    "oidc", sSub, sUsername, sEmail, sDisplayName, vGroups,
    oIdp->jGroupMappings, oIdp->iDefaultGroupId);
```

Also update the test mode JSON result to include display_name:
```cpp
nlohmann::json jResult = {
    {"subject", sSub},
    {"email", sEmail},
    {"username", sUsername},
    {"display_name", sDisplayName},
    {"groups", vGroups},
    {"all_claims", jPayload},
};
```

**Step 2: Update SamlRoutes.cpp — configurable attribute extraction**

At `src/api/routes/SamlRoutes.cpp:182-191`, replace with:

```cpp
// Read attribute mapping from IdP config
auto jMapping = oIdp->jConfig.value("attribute_mapping", nlohmann::json::object());
std::string sEmailAttr = jMapping.value("email", "email");
std::string sUsernameAttr = jMapping.value("username", "");  // empty = use NameID
std::string sDisplayNameAttr = jMapping.value("display_name", "displayName");

// Extract email using configurable mapping
std::string sEmail;
if (jAttributes.contains(sEmailAttr) && jAttributes[sEmailAttr].is_array() &&
    !jAttributes[sEmailAttr].empty()) {
  sEmail = jAttributes[sEmailAttr][0].get<std::string>();
}

// Extract display name
std::string sDisplayName;
if (jAttributes.contains(sDisplayNameAttr) && jAttributes[sDisplayNameAttr].is_array() &&
    !jAttributes[sDisplayNameAttr].empty()) {
  sDisplayName = jAttributes[sDisplayNameAttr][0].get<std::string>();
}

// Username from mapping, then NameID, then email
std::string sUsername;
if (!sUsernameAttr.empty() && jAttributes.contains(sUsernameAttr) &&
    jAttributes[sUsernameAttr].is_array() && !jAttributes[sUsernameAttr].empty()) {
  sUsername = jAttributes[sUsernameAttr][0].get<std::string>();
}
if (sUsername.empty()) sUsername = sNameId;
if (sUsername.empty()) sUsername = sEmail;
```

Update the `processFederatedLogin` call at line ~216:

```cpp
auto lr = _fasService.processFederatedLogin(
    "saml", sNameId, sUsername, sEmail, sDisplayName, vGroups,
    oIdp->jGroupMappings, oIdp->iDefaultGroupId);
```

Also update test mode output to include display_name.

**Step 3: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/api/routes/OidcRoutes.cpp src/api/routes/SamlRoutes.cpp
git commit -m "feat: configurable IdP attribute mapping + display_name extraction"
```

---

## Task 5: Backend — Add `display_name` to JWT, RequestContext, AuthMiddleware, and API Responses

**Files:**
- Modify: `include/common/Types.hpp`
- Modify: `src/security/AuthService.cpp`
- Modify: `src/security/FederatedAuthService.cpp`
- Modify: `src/api/AuthMiddleware.cpp`
- Modify: `src/api/routes/AuthRoutes.cpp`
- Modify: `src/api/routes/UserRoutes.cpp`

**Step 1: Add `sDisplayName` to RequestContext**

In `include/common/Types.hpp`, add to `RequestContext` struct after `sUsername`:

```cpp
struct RequestContext {
  int64_t iUserId = 0;
  std::string sUsername;
  std::string sDisplayName;       // Display name (empty if not set)
  std::string sRole;
  std::string sAuthMethod;
  std::string sIpAddress;         // Client IP (populated in Task 8)
  std::unordered_set<std::string> vPermissions;
};
```

**Step 2: Include `display_name` in local login JWT payload**

In `src/security/AuthService.cpp` `authenticateLocal()`, after resolving the role (line ~50), look up display_name:

```cpp
std::string sDisplayName;
if (oUser->osDisplayName.has_value()) {
  sDisplayName = oUser->osDisplayName.value();
}
```

Add to JWT payload (line ~60):
```cpp
nlohmann::json jPayload = {
    {"sub", std::to_string(oUser->iId)},
    {"username", oUser->sUsername},
    {"display_name", sDisplayName},
    {"role", sRole},
    {"auth_method", "local"},
    {"iat", iNow},
    {"exp", iNow + _iJwtTtlSeconds},
};
```

In `validateToken()`, extract display_name (after line 100):
```cpp
rcCtx.sDisplayName = jPayload.value("display_name", "");
```

**Step 3: Include `display_name` in federated JWT payload**

In `src/security/FederatedAuthService.cpp`, after resolving the role (line ~136), look up display_name from the freshly read/updated user:

```cpp
std::string sDisplayNameForJwt;
// Re-read user to get potentially updated display_name
auto oFreshUser = _urRepo.findById(oUser->iId);
if (oFreshUser && oFreshUser->osDisplayName.has_value()) {
  sDisplayNameForJwt = oFreshUser->osDisplayName.value();
}
```

Add to JWT payload:
```cpp
nlohmann::json jPayload = {
    {"sub", std::to_string(oUser->iId)},
    {"username", oUser->sUsername},
    {"display_name", sDisplayNameForJwt},
    {"role", sRole},
    {"auth_method", sAuthMethod},
    {"iat", iNow},
    {"exp", iNow + _iJwtTtlSeconds},
};
```

**Step 4: Extract `display_name` from JWT in AuthMiddleware**

In `src/api/AuthMiddleware.cpp`, in `validateJwt()` (around line 77):

```cpp
rcCtx.sDisplayName = jPayload.value("display_name", "");
```

**Step 5: Add `display_name` to `/me` endpoint response**

In `src/api/routes/AuthRoutes.cpp`, in the `/auth/me` handler (line ~87):

```cpp
std::string sDisplayName;
if (oUser && oUser->osDisplayName.has_value()) {
  sDisplayName = oUser->osDisplayName.value();
}

return jsonResponse(200, {
    {"user_id", rcCtx.iUserId},
    {"username", rcCtx.sUsername},
    {"display_name", sDisplayName},
    {"email", sEmail},
    {"role", rcCtx.sRole},
    {"permissions", jPerms},
    {"auth_method", rcCtx.sAuthMethod},
    {"force_password_change", bForcePasswordChange},
});
```

**Step 6: Add `display_name` to profile update endpoint**

In `src/api/routes/AuthRoutes.cpp`, in the `PUT /auth/profile` handler:

```cpp
std::string sEmail = jBody.value("email", "");
RequestValidator::validateRequired(sEmail, "email");
std::optional<std::string> osDisplayName;
if (jBody.contains("display_name")) {
  osDisplayName = jBody.value("display_name", "");
}

auto oUser = _urRepo.findById(rcCtx.iUserId);
if (!oUser) throw common::NotFoundError("USER_NOT_FOUND", "User not found");

_urRepo.update(rcCtx.iUserId, sEmail, oUser->bIsActive, osDisplayName);
```

**Step 7: Add `display_name` to user list and CRUD in UserRoutes.cpp**

Include `display_name` in the user JSON serialization helper. In user list responses and user detail, add:
```cpp
{"display_name", row.osDisplayName.value_or("")},
```

For user create/update endpoints, accept optional `display_name` from the request body.

**Step 8: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 9: Commit**

```bash
git add include/common/Types.hpp src/security/AuthService.cpp \
  src/security/FederatedAuthService.cpp src/api/AuthMiddleware.cpp \
  src/api/routes/AuthRoutes.cpp src/api/routes/UserRoutes.cpp
git commit -m "feat: display_name in JWT, RequestContext, and API responses"
```

---

## Task 6: Backend — Add AuditContext Struct and formatAuditIdentity()

**Files:**
- Modify: `include/common/Types.hpp`
- Modify: `include/api/RouteHelpers.hpp`
- Modify: `src/api/RouteHelpers.cpp`

**Step 1: Add `AuditContext` struct to Types.hpp**

```cpp
/// Lightweight audit metadata passed to engine methods.
/// Class abbreviation: ac
struct AuditContext {
  std::string sIdentity;     // Formatted "Display Name (username)" or just "username"
  std::string sAuthMethod;
  std::string sIpAddress;
};
```

**Step 2: Add `formatAuditIdentity()` to RouteHelpers**

In `include/api/RouteHelpers.hpp`:
```cpp
/// Format a display identity for audit logging.
/// Returns "Display Name (username)" when display_name is set, otherwise just "username".
std::string formatAuditIdentity(const common::RequestContext& rcCtx);

/// Build an AuditContext from a RequestContext.
common::AuditContext buildAuditContext(const common::RequestContext& rcCtx);
```

In `src/api/RouteHelpers.cpp`:
```cpp
std::string formatAuditIdentity(const common::RequestContext& rcCtx) {
  if (rcCtx.sDisplayName.empty()) return rcCtx.sUsername;
  return rcCtx.sDisplayName + " (" + rcCtx.sUsername + ")";
}

common::AuditContext buildAuditContext(const common::RequestContext& rcCtx) {
  common::AuditContext ac;
  ac.sIdentity = formatAuditIdentity(rcCtx);
  ac.sAuthMethod = rcCtx.sAuthMethod;
  ac.sIpAddress = rcCtx.sIpAddress;
  return ac;
}
```

**Step 3: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add include/common/Types.hpp include/api/RouteHelpers.hpp src/api/RouteHelpers.cpp
git commit -m "feat: add AuditContext struct and formatAuditIdentity()"
```

---

## Task 7: Backend — Populate IP Address in RouteHelpers::authenticate()

**Files:**
- Modify: `src/api/RouteHelpers.cpp`

**Step 1: Populate `sIpAddress` from the request in `authenticate()`**

In `src/api/RouteHelpers.cpp`, the current `authenticate()` function is:

```cpp
common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  return amMiddleware.authenticate(req.get_header_value("Authorization"),
                                   req.get_header_value("X-API-Key"));
}
```

Update to:

```cpp
common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  auto rcCtx = amMiddleware.authenticate(req.get_header_value("Authorization"),
                                          req.get_header_value("X-API-Key"));

  // Populate client IP with X-Forwarded-For awareness
  std::string sIp = req.get_header_value("X-Forwarded-For");
  if (!sIp.empty()) {
    // Take first IP in chain (original client)
    auto pos = sIp.find(',');
    if (pos != std::string::npos) sIp = sIp.substr(0, pos);
    // Trim whitespace
    auto start = sIp.find_first_not_of(' ');
    auto end = sIp.find_last_not_of(' ');
    if (start != std::string::npos) sIp = sIp.substr(start, end - start + 1);
  } else {
    sIp = req.remote_ip_address;
  }
  rcCtx.sIpAddress = sIp;

  return rcCtx;
}
```

**Step 2: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/api/RouteHelpers.cpp
git commit -m "feat: populate client IP address in authenticate()"
```

---

## Task 8: Backend — Wire Audit Identity and IP in RecordRoutes

**Files:**
- Modify: `src/api/routes/RecordRoutes.cpp`

**Step 1: Replace `rcCtx.sUsername` with formatted identity and auth/IP in all audit inserts**

There are 5 audit insert call sites in `RecordRoutes.cpp`. For each, replace:
```cpp
_arRepo.insert("record", ..., rcCtx.sUsername, std::nullopt, std::nullopt);
```
with:
```cpp
_arRepo.insert("record", ..., formatAuditIdentity(rcCtx), rcCtx.sAuthMethod, rcCtx.sIpAddress);
```

Ensure `#include "api/RouteHelpers.hpp"` is present (it is — line 7 area).

The 5 call sites are at approximately lines:
- 116 (create)
- 188 (update)
- 222 (delete)
- 251 (restore)
- 524 (batch_update)

For each, change the last three arguments from `rcCtx.sUsername, std::nullopt, std::nullopt` to `formatAuditIdentity(rcCtx), rcCtx.sAuthMethod, rcCtx.sIpAddress`.

**Step 2: Update push/capture calls to pass AuditContext**

The push call at line ~355:
```cpp
_depEngine.push(iZoneId, vDriftActions, rcCtx.iUserId, rcCtx.sUsername);
```

And capture call at line ~370:
```cpp
_depEngine.capture(iZoneId, rcCtx.iUserId, rcCtx.sUsername, "manual-capture");
```

Both will be updated in Task 9 when the engine signatures change. **Leave these for now** — they only pass `sUsername` as actor identity. We'll convert them when updating the engine APIs.

**Step 3: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/api/routes/RecordRoutes.cpp
git commit -m "feat: populate audit identity, auth_method, ip_address in RecordRoutes"
```

---

## Task 9: Backend — Wire AuditContext Through DeploymentEngine and RollbackEngine

**Files:**
- Modify: `include/core/DeploymentEngine.hpp`
- Modify: `src/core/DeploymentEngine.cpp`
- Modify: `include/core/RollbackEngine.hpp`
- Modify: `src/core/RollbackEngine.cpp`
- Modify: `src/api/routes/RecordRoutes.cpp` (push/capture call sites)
- Modify: `src/api/routes/DeploymentRoutes.cpp` (rollback call site)

**Step 1: Update `DeploymentEngine::push()` signature**

In `include/core/DeploymentEngine.hpp`, change:
```cpp
void push(int64_t iZoneId,
          const std::vector<common::DriftAction>& vDriftActions,
          int64_t iActorUserId, const std::string& sActor);
```
to:
```cpp
void push(int64_t iZoneId,
          const std::vector<common::DriftAction>& vDriftActions,
          int64_t iActorUserId, const common::AuditContext& acCtx);
```

**Step 2: Update `DeploymentEngine::capture()` signature**

```cpp
int64_t capture(int64_t iZoneId, int64_t iActorUserId,
                const common::AuditContext& acCtx, const std::string& sGeneratedBy);
```

**Step 3: Update DeploymentEngine.cpp implementation**

- `push()`: Change all uses of `sActor` to `acCtx.sIdentity`
- At audit insert (line ~296): replace `sActor, std::nullopt, std::nullopt` with `acCtx.sIdentity, acCtx.sAuthMethod, acCtx.sIpAddress`
- Update `buildSnapshot()` calls to use `acCtx.sIdentity`

- `capture()`: Same pattern
- At audit insert (line ~367): replace with `acCtx.sIdentity, acCtx.sAuthMethod, acCtx.sIpAddress`
- Update `buildCaptureSnapshot()` calls to use `acCtx.sIdentity`

Also update `buildSnapshot()` and `buildCaptureSnapshot()` signatures from `const std::string& sActor` to `const std::string& sIdentity`.

**Step 4: Update `RollbackEngine::apply()` signature**

In `include/core/RollbackEngine.hpp`, change:
```cpp
void apply(int64_t iZoneId, int64_t iDeploymentId,
           const std::vector<int64_t>& vCherryPickIds,
           int64_t iActorUserId, const std::string& sActor);
```
to:
```cpp
void apply(int64_t iZoneId, int64_t iDeploymentId,
           const std::vector<int64_t>& vCherryPickIds,
           int64_t iActorUserId, const common::AuditContext& acCtx);
```

**Step 5: Update RollbackEngine.cpp implementation**

- At audit insert (line ~95): replace `sActor, std::nullopt, std::nullopt` with `acCtx.sIdentity, acCtx.sAuthMethod, acCtx.sIpAddress`
- Replace all uses of `sActor` with `acCtx.sIdentity`

**Step 6: Update callers in RecordRoutes.cpp**

Push call (~line 355):
```cpp
auto acCtx = buildAuditContext(rcCtx);
_depEngine.push(iZoneId, vDriftActions, rcCtx.iUserId, acCtx);
```

Capture call (~line 369):
```cpp
auto acCtx = buildAuditContext(rcCtx);
int64_t iDeploymentId =
    _depEngine.capture(iZoneId, rcCtx.iUserId, acCtx, "manual-capture");
```

**Step 7: Update caller in DeploymentRoutes.cpp**

Rollback call (~line 178):
```cpp
auto acCtx = buildAuditContext(rcCtx);
_reEngine.apply(iZoneId, iDeployId, vCherryPickIds, rcCtx.iUserId, acCtx);
```

Add `#include "api/RouteHelpers.hpp"` if not already present (it is at line 7).

**Step 8: Check for any other callers of push/capture/apply**

Run: `grep -rn 'depEngine\.\|_depEngine\.\|reEngine\.\|_reEngine\.' src/ --include='*.cpp' | grep -E 'push\(|capture\(|apply\('`

Update any remaining callers (e.g. MaintenanceScheduler auto-capture if present).

**Step 9: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 10: Commit**

```bash
git add include/core/DeploymentEngine.hpp src/core/DeploymentEngine.cpp \
  include/core/RollbackEngine.hpp src/core/RollbackEngine.cpp \
  src/api/routes/RecordRoutes.cpp src/api/routes/DeploymentRoutes.cpp
git commit -m "feat: thread AuditContext through DeploymentEngine and RollbackEngine"
```

---

## Task 10: Backend — Wire Audit Identity/IP in Remaining Route Files

**Files:**
- Search all route files for `_arRepo.insert` calls using `std::nullopt`

**Step 1: Find all remaining audit insert calls with nullopt**

Run: `grep -rn 'std::nullopt, std::nullopt' src/api/routes/ --include='*.cpp'`

For each hit, replace:
- `rcCtx.sUsername` → `formatAuditIdentity(rcCtx)`
- `std::nullopt, std::nullopt` → `rcCtx.sAuthMethod, rcCtx.sIpAddress`

Known files with audit inserts beyond RecordRoutes/engines:
- `ZoneRoutes.cpp`
- `ProviderRoutes.cpp`
- `ViewRoutes.cpp`
- `VariableRoutes.cpp`
- `GroupRoutes.cpp`
- `UserRoutes.cpp`
- `SettingsRoutes.cpp`
- `GitRepoRoutes.cpp`
- `BackupRoutes.cpp`
- `AuditRoutes.cpp` (purge self-audit)

**Step 2: Update each file**

For each file that has `_arRepo.insert(...)`:
1. Ensure `RouteHelpers.hpp` is included (it should be, since `authenticate()` comes from there)
2. Replace identity argument with `formatAuditIdentity(rcCtx)`
3. Replace last two `std::nullopt` args with `rcCtx.sAuthMethod, rcCtx.sIpAddress`

Some files may use a different pattern (e.g., BackupRoutes may pass a string directly). Adapt as needed — the key is that all audit inserts populate auth_method and ip_address.

**Step 3: Verify the build compiles**

Run: `sg docker 'docker buildx build --target builder -t meridian-build-check .'`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/api/routes/*.cpp
git commit -m "feat: populate audit auth_method and ip_address in all route files"
```

---

## Task 11: UI — Add `display_name` to Types and Auth Store

**Files:**
- Modify: `ui/src/types/index.ts`
- Modify: `ui/src/stores/auth.ts`
- Modify: `ui/src/api/auth.ts`

**Step 1: Add `display_name` to User interface**

In `ui/src/types/index.ts`, update the `User` interface:

```typescript
export interface User {
  user_id: number
  username: string
  email: string
  display_name: string | null
  role: string
  permissions: string[]
  auth_method: string
  force_password_change: boolean
}
```

Add `display_name` to `UserDetail` as well:
```typescript
export interface UserDetail {
  id: number
  username: string
  email: string
  display_name: string | null
  auth_method: string
  is_active: boolean
  force_password_change: boolean
  groups: { id: number; name: string }[]
}
```

**Step 2: Update profile API to accept display_name**

In `ui/src/api/auth.ts`:
```typescript
export function updateProfile(data: { email: string; display_name?: string }): Promise<{ message: string }> {
  return put('/auth/profile', data)
}
```

**Step 3: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 4: Commit**

```bash
git add ui/src/types/index.ts ui/src/stores/auth.ts ui/src/api/auth.ts
git commit -m "feat(ui): add display_name to User type and profile API"
```

---

## Task 12: UI — Show Display Name in AppTopBar

**Files:**
- Modify: `ui/src/components/layout/AppTopBar.vue`

**Step 1: Update the user label to show display_name with username fallback**

In `ui/src/components/layout/AppTopBar.vue`, change line 64:

```vue
<span class="app-user-label">{{ auth.user?.display_name || auth.user?.username }}</span>
```

Also update the user menu label (line 19):
```typescript
const userMenuItems = ref([
  {
    label: auth.user?.display_name || auth.user?.username || '',
    items: [
```

**Step 2: Verify it renders in dev server**

Run: `cd ui && npm run dev`
Visit `http://localhost:5173` — verify top bar shows display name or username.

**Step 3: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 4: Commit**

```bash
git add ui/src/components/layout/AppTopBar.vue
git commit -m "feat(ui): show display_name in top bar with username fallback"
```

---

## Task 13: UI — Add Display Name Field to ProfileView

**Files:**
- Modify: `ui/src/views/ProfileView.vue`

**Step 1: Add `displayName` ref and populate on mount**

In the `<script setup>` section, add:
```typescript
const displayName = ref('')
```

In the `onMounted` callback, after `email.value`:
```typescript
displayName.value = user.display_name ?? ''
```

**Step 2: Add Display Name field to the form**

In the template, in the Profile section form (after the Username field, before Email):

```vue
<div class="field">
  <label>Display Name</label>
  <InputText v-model="displayName" class="w-full" placeholder="How your name appears to others" />
</div>
```

**Step 3: Include display_name in profile save**

Update `handleProfileSave()`:
```typescript
async function handleProfileSave() {
  try {
    await updateProfile({
      email: email.value,
      display_name: displayName.value || undefined,
    })
    notify.success('Profile updated')
    await auth.hydrate()
  } catch (e: any) {
    notify.error(e.message || 'Failed to update profile')
  }
}
```

**Step 4: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 5: Commit**

```bash
git add ui/src/views/ProfileView.vue
git commit -m "feat(ui): add display name field to profile page"
```

---

## Task 14: UI — Show Display Name in UsersView

**Files:**
- Modify: `ui/src/views/UsersView.vue`

**Step 1: Add display_name column to DataTable**

After the Username column, add:
```vue
<Column field="display_name" header="Display Name" sortable />
```

**Step 2: Add display_name field to user create/edit form (if form exists)**

If UsersView has a create/edit dialog, add a Display Name InputText field.

**Step 3: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 4: Commit**

```bash
git add ui/src/views/UsersView.vue
git commit -m "feat(ui): show display_name in user management table"
```

---

## Task 15: UI — Fix Tooltip Directive Registration

**Files:**
- Modify: `ui/src/main.ts`

**Step 1: Check if Tooltip directive is registered**

Current `ui/src/main.ts` (lines 1-21) does NOT register the Tooltip directive. The file imports PrimeVue, ConfirmationService, ToastService but no Tooltip.

**Step 2: Add Tooltip directive registration**

Add after the imports:
```typescript
import Tooltip from 'primevue/tooltip'
```

After `app.use(ToastService)`, add:
```typescript
app.directive('tooltip', Tooltip)
```

The full file becomes:
```typescript
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import PrimeVue from 'primevue/config'
import ConfirmationService from 'primevue/confirmationservice'
import ToastService from 'primevue/toastservice'
import Tooltip from 'primevue/tooltip'
import { themeConfig } from './theme'
import router from './router'
import App from './App.vue'

import 'primeicons/primeicons.css'
import './style.css'

const app = createApp(App)

app.use(createPinia())
app.use(router)
app.use(PrimeVue, { theme: themeConfig })
app.use(ConfirmationService)
app.use(ToastService)
app.directive('tooltip', Tooltip)

app.mount('#app')
```

**Step 3: Verify tooltips work**

Run: `cd ui && npm run dev`
Visit any page with icon-only action buttons (e.g. Providers, Zones). Hover over an edit/delete button — tooltip should appear.

**Step 4: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 5: Commit**

```bash
git add ui/src/main.ts
git commit -m "fix(ui): register PrimeVue Tooltip directive globally"
```

---

## Task 16: UI — Rename Identity Providers to SSO

**Files:**
- Modify: `ui/src/components/layout/AppSidebar.vue`
- Modify: `ui/src/views/IdentityProvidersView.vue`

**Step 1: Rename sidebar label**

In `ui/src/components/layout/AppSidebar.vue`, line 22:

Change:
```typescript
{ label: 'Identity Providers', icon: 'pi pi-key', to: '/admin/identity-providers' },
```
To:
```typescript
{ label: 'SSO', icon: 'pi pi-key', to: '/admin/identity-providers' },
```

**Step 2: Rename all user-facing strings in IdentityProvidersView.vue**

In `ui/src/views/IdentityProvidersView.vue`:

- PageHeader title (line 302): `"Identity Providers"` → `"SSO Providers"`
- PageHeader subtitle: `"Configure external authentication"` → `"Configure SSO authentication"`
- Dialog header (line ~368): `"Edit Identity Provider"` / `"Add Identity Provider"` → `"Edit SSO Provider"` / `"Add SSO Provider"`
- Add button label: `"Add Provider"` → `"Add SSO Provider"`
- Delete confirm (line ~244): `"Delete identity provider"` → `"Delete SSO provider"`
- Toast messages: `"Identity provider created"` → `"SSO provider created"`, etc.
- All other occurrences of "identity provider" → "SSO provider" in user-facing strings

**Important:** Do NOT rename the route path (`/admin/identity-providers`), component filename, or API endpoints.

**Step 3: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 4: Commit**

```bash
git add ui/src/components/layout/AppSidebar.vue ui/src/views/IdentityProvidersView.vue
git commit -m "feat(ui): rename Identity Providers to SSO in UI labels"
```

---

## Task 17: UI — Add Attribute Mapping Fields to IdP Form

**Files:**
- Modify: `ui/src/views/IdentityProvidersView.vue`

**Step 1: Add attribute mapping fields to form state**

In the `form` ref (around line 55), add:
```typescript
attr_username: '',
attr_email: '',
attr_display_name: '',
```

**Step 2: Populate from existing config on edit**

In `openEdit()` (line ~148), extract from config:
```typescript
const attrMapping = (cfg.attribute_mapping as Record<string, string>) ?? {}
form.value.attr_username = attrMapping.username ?? ''
form.value.attr_email = attrMapping.email ?? ''
form.value.attr_display_name = attrMapping.display_name ?? ''
```

In `openCreate()`, set defaults based on type:
```typescript
attr_username: '',
attr_email: '',
attr_display_name: '',
```

**Step 3: Include attribute_mapping in buildConfig()**

In `buildConfig()`, add to both OIDC and SAML config objects:

```typescript
function buildConfig() {
  const attrMapping: Record<string, string> = {}
  if (form.value.attr_username) attrMapping.username = form.value.attr_username
  if (form.value.attr_email) attrMapping.email = form.value.attr_email
  if (form.value.attr_display_name) attrMapping.display_name = form.value.attr_display_name

  if (form.value.type === 'oidc') {
    return {
      issuer_url: form.value.issuer_url,
      client_id: form.value.client_id,
      redirect_uri: oidcRedirectUri(form.value.id),
      scopes: form.value.scopes.split(/\s+/).filter(Boolean),
      groups_claim: form.value.groups_claim,
      ...(Object.keys(attrMapping).length > 0 && { attribute_mapping: attrMapping }),
    }
  }
  // SAML config similarly includes attribute_mapping
  return {
    // ... existing SAML fields ...
    ...(Object.keys(attrMapping).length > 0 && { attribute_mapping: attrMapping }),
  }
}
```

**Step 4: Add Attribute Mapping section to the dialog template**

After the type-specific configuration sections and before the Group Mappings section, add:

```vue
<!-- Attribute Mapping -->
<h4 class="form-section-title">Attribute Mapping</h4>
<p class="section-hint">
  Map your IdP's claim/attribute names to Meridian fields.
  Leave empty to use defaults{{ form.type === 'oidc' ? ' (preferred_username, email, name)' : ' (NameID, email, displayName)' }}.
</p>
<div class="form-grid">
  <div class="field">
    <label>Username {{ form.type === 'oidc' ? 'Claim' : 'Attribute' }}</label>
    <InputText
      v-model="form.attr_username"
      class="w-full"
      :placeholder="form.type === 'oidc' ? 'preferred_username' : '(uses NameID)'"
    />
  </div>
  <div class="field">
    <label>Email {{ form.type === 'oidc' ? 'Claim' : 'Attribute' }}</label>
    <InputText
      v-model="form.attr_email"
      class="w-full"
      placeholder="email"
    />
  </div>
  <div class="field">
    <label>Display Name {{ form.type === 'oidc' ? 'Claim' : 'Attribute' }}</label>
    <InputText
      v-model="form.attr_display_name"
      class="w-full"
      :placeholder="form.type === 'oidc' ? 'name' : 'displayName'"
    />
  </div>
</div>
```

**Step 5: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 6: Commit**

```bash
git add ui/src/views/IdentityProvidersView.vue
git commit -m "feat(ui): add attribute mapping fields to SSO provider form"
```

---

## Task 18: UI — Add Audit Log Dash Fallback for Empty Values

**Files:**
- Modify: `ui/src/views/AuditView.vue`

**Step 1: Add dash fallback for auth_method and ip_address**

Find the detail-meta section (line ~198):
```vue
<span>Auth: {{ data.auth_method }}</span>
<span>IP: {{ data.ip_address }}</span>
```

Replace with:
```vue
<span>Auth: {{ data.auth_method || '—' }}</span>
<span>IP: {{ data.ip_address || '—' }}</span>
```

**Step 2: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 3: Commit**

```bash
git add ui/src/views/AuditView.vue
git commit -m "fix(ui): show dash for empty auth_method/ip_address in audit log"
```

---

## Task 19: UI — Add Backup Settings Card with Repo Chooser

**Files:**
- Modify: `ui/src/views/BackupRestoreView.vue`

**Step 1: Add imports for new components and APIs**

At the top of `<script setup>`, add:

```typescript
import { ref, computed, onMounted } from 'vue'
import Select from 'primevue/select'
import { listGitRepos } from '../api/gitRepos'
import { listSettings, updateSettings } from '../api/settings'
import type { GitRepo, SystemSetting } from '../types'
```

(Remove existing `import { ref } from 'vue'` — merge into the onMounted import.)

**Step 2: Add settings state and data fetching**

```typescript
const repos = ref<GitRepo[]>([])
const selectedRepoId = ref<number | null>(null)
const autoInterval = ref(0)
const settingsLoading = ref(false)
const settingsSaving = ref(false)

const repoOptions = computed(() => [
  { label: 'None', value: null },
  ...repos.value.filter(r => r.is_enabled).map(r => ({ label: r.name, value: r.id })),
])

const intervalOptions = [
  { label: 'Disabled', value: 0 },
  { label: 'Every 6 hours', value: 21600 },
  { label: 'Every 12 hours', value: 43200 },
  { label: 'Every 24 hours', value: 86400 },
]

const hasBackupRepo = computed(() => selectedRepoId.value !== null && selectedRepoId.value > 0)

onMounted(async () => {
  settingsLoading.value = true
  try {
    const [allRepos, settings] = await Promise.all([
      listGitRepos(),
      listSettings(),
    ])
    repos.value = allRepos

    const repoSetting = settings.find((s: SystemSetting) => s.key === 'backup.git_repo_id')
    if (repoSetting && repoSetting.value) {
      selectedRepoId.value = parseInt(repoSetting.value, 10) || null
    }

    const intervalSetting = settings.find((s: SystemSetting) => s.key === 'backup.auto_interval_seconds')
    if (intervalSetting && intervalSetting.value) {
      autoInterval.value = parseInt(intervalSetting.value, 10) || 0
    }
  } catch { /* ignore */ } finally {
    settingsLoading.value = false
  }
})

async function saveBackupSettings() {
  settingsSaving.value = true
  try {
    await updateSettings({
      'backup.git_repo_id': selectedRepoId.value?.toString() ?? '0',
      'backup.auto_interval_seconds': autoInterval.value.toString(),
    })
    notify.success('Backup settings saved')
  } catch (e: unknown) {
    notify.error((e as Error).message || 'Failed to save settings')
  } finally {
    settingsSaving.value = false
  }
}
```

**Step 3: Add the Backup Settings card to the template**

Insert before the `<!-- Export Section -->` comment, inside the `sections-grid` div:

```vue
<!-- Backup Settings -->
<div class="section-card">
  <div class="section-header">
    <i class="pi pi-cog" />
    <h3>Backup Settings</h3>
  </div>
  <div class="settings-form">
    <div class="settings-field">
      <label>Git Repository</label>
      <Select
        v-model="selectedRepoId"
        :options="repoOptions"
        optionLabel="label"
        optionValue="value"
        placeholder="Select repository..."
        class="w-full"
        :loading="settingsLoading"
      />
      <small class="field-hint">Backup files are committed here on export.</small>
    </div>
    <div class="settings-field">
      <label>Auto-Backup</label>
      <Select
        v-model="autoInterval"
        :options="intervalOptions"
        optionLabel="label"
        optionValue="value"
        class="w-full"
      />
    </div>
    <Button
      label="Save Settings"
      icon="pi pi-save"
      :loading="settingsSaving"
      @click="saveBackupSettings"
      class="align-self-start"
    />
  </div>
</div>
```

**Step 4: Conditionally enable the "Commit to GitOps" checkbox**

Update the Export section checkbox:

```vue
<div class="checkbox-row">
  <Checkbox
    v-model="commitToGit"
    :binary="true"
    input-id="commit-git"
    :disabled="!hasBackupRepo"
  />
  <label for="commit-git">
    Commit to GitOps repository
    <small v-if="!hasBackupRepo" class="field-hint"> — select a repository in Backup Settings above</small>
  </label>
</div>
```

**Step 5: Show a message in the "Restore from Git Repository" section when no repo configured**

```vue
<div class="section-card">
  <div class="section-header">
    <i class="pi pi-github" />
    <h3>Restore from Git Repository</h3>
  </div>
  <template v-if="hasBackupRepo">
    <p class="section-desc">
      Pull the latest backup from the configured GitOps repository and restore.
    </p>
    <!-- ... existing buttons ... -->
  </template>
  <template v-else>
    <p class="section-desc">
      Configure a backup repository in Backup Settings above to enable restore from Git.
    </p>
  </template>
</div>
```

**Step 6: Add styles for the new settings form**

```css
.settings-form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.settings-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  max-width: 24rem;
}

.settings-field label {
  font-size: 0.875rem;
  font-weight: 500;
}

.field-hint {
  color: var(--p-surface-400);
  font-size: 0.8rem;
}

:root:not(.app-dark) .field-hint {
  color: var(--p-surface-500);
}

.align-self-start {
  align-self: flex-start;
}
```

**Step 7: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc -b --noEmit`
Expected: No type errors

**Step 8: Build the UI**

Run: `cd ui && npm run build`
Expected: Build succeeds, output in `ui/dist/`

**Step 9: Commit**

```bash
git add ui/src/views/BackupRestoreView.vue
git commit -m "feat(ui): add backup settings card with repo chooser and auto-backup"
```

---

## Task 20: Full Docker Build Verification

**Files:**
- No changes — verification only

**Step 1: Full Docker build**

Run: `sg docker 'docker buildx build -t meridian-dns:pre-release-polish .'`
Expected: All three stages (UI build, C++ build, runtime) succeed

**Step 2: Verify UI build inside Docker**

The Dockerfile runs `npm run build` for the UI stage — this includes `vue-tsc -b` and Vite build. If the Docker build passes, TypeScript is verified.

**Step 3: Commit any remaining changes**

```bash
git status
# If clean, skip. Otherwise:
git add -A
git commit -m "chore: pre-release polish final cleanup"
```

---

## Summary of All Changes

| Task | Item | Scope | Files Modified |
|------|------|-------|----------------|
| 1 | Display Name | Migration | `scripts/db/v014/001_add_display_name.sql` |
| 2 | Display Name | Backend | `include/dal/UserRepository.hpp`, `src/dal/UserRepository.cpp` |
| 3 | Display Name | Backend | `include/security/FederatedAuthService.hpp`, `src/security/FederatedAuthService.cpp` |
| 4 | Attribute Mapping | Backend | `src/api/routes/OidcRoutes.cpp`, `src/api/routes/SamlRoutes.cpp` |
| 5 | Display Name | Backend | `include/common/Types.hpp`, `src/security/AuthService.cpp`, `src/security/FederatedAuthService.cpp`, `src/api/AuthMiddleware.cpp`, `src/api/routes/AuthRoutes.cpp`, `src/api/routes/UserRoutes.cpp` |
| 6 | Audit Display Name | Backend | `include/common/Types.hpp`, `include/api/RouteHelpers.hpp`, `src/api/RouteHelpers.cpp` |
| 7 | Audit IP | Backend | `src/api/RouteHelpers.cpp` |
| 8 | Audit Population | Backend | `src/api/routes/RecordRoutes.cpp` |
| 9 | Audit Population | Backend | `include/core/DeploymentEngine.hpp`, `src/core/DeploymentEngine.cpp`, `include/core/RollbackEngine.hpp`, `src/core/RollbackEngine.cpp`, `src/api/routes/RecordRoutes.cpp`, `src/api/routes/DeploymentRoutes.cpp` |
| 10 | Audit Population | Backend | All remaining route files with `_arRepo.insert()` |
| 11 | Display Name | UI | `ui/src/types/index.ts`, `ui/src/stores/auth.ts`, `ui/src/api/auth.ts` |
| 12 | Display Name | UI | `ui/src/components/layout/AppTopBar.vue` |
| 13 | Display Name | UI | `ui/src/views/ProfileView.vue` |
| 14 | Display Name | UI | `ui/src/views/UsersView.vue` |
| 15 | Tooltips | UI | `ui/src/main.ts` |
| 16 | SSO Rename | UI | `ui/src/components/layout/AppSidebar.vue`, `ui/src/views/IdentityProvidersView.vue` |
| 17 | Attribute Mapping | UI | `ui/src/views/IdentityProvidersView.vue` |
| 18 | Audit Fallback | UI | `ui/src/views/AuditView.vue` |
| 19 | Backup Settings | UI | `ui/src/views/BackupRestoreView.vue` |
| 20 | Verification | Build | Full Docker build |
