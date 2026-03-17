# Code Review Remediation Plan — Meridian DNS 1.0

> **Date:** 2026-03-17
> **Source:** `docs/internal/CODE_REVIEW_1.0.md` and `docs/internal/CODE_REVIEW_2_1.0.md`
> **Scope:** Selected findings from both pre-release code reviews
> **Approach:** Grouped by change type — security fixes first, then defense-in-depth, code quality, and documentation

---

## Table of Contents

1. [Security Code Fixes](#1-security-code-fixes)
2. [Defense-in-Depth Code Changes](#2-defense-in-depth-code-changes)
3. [Code Quality Improvements](#3-code-quality-improvements)
4. [Documentation Updates](#4-documentation-updates)
5. [Files Changed Summary](#5-files-changed-summary)
6. [Implementation Order](#6-implementation-order)

---

## 1. Security Code Fixes

### 1.1 SEC-I4 — Logout session invalidation

**File:** `src/api/routes/AuthRoutes.cpp:58`
**Finding:** The logout handler authenticates the request but never deletes the server-side session. The JWT remains valid until natural expiration.

**Fix:**
1. Add `SessionRepository&` member to `AuthRoutes` (alongside existing `AuthService&`, `AuthMiddleware&`, `UserRepository&`)
2. Update `AuthRoutes` constructor and header to accept `SessionRepository&`
3. Update `ApiServer.cpp` constructor/registration and `main.cpp` wiring to pass `SessionRepository`
4. In the logout handler:
   - Extract the raw JWT from the `Authorization` header (strip `Bearer ` prefix)
   - Compute SHA-256 hash via `CryptoService::sha256Hex(token)`
   - Call `_srRepo.deleteByHash(hash)` to immediately revoke the session

**Files modified:**
- `include/api/routes/AuthRoutes.hpp` — add `SessionRepository&` member
- `src/api/routes/AuthRoutes.cpp` — update constructor, implement session deletion in logout
- `include/api/ApiServer.hpp` — add `AuthRoutes` dependency if needed
- `src/api/ApiServer.cpp` — pass `SessionRepository` through
- `src/main.cpp` — update wiring

### 1.2 SEC-I3 — Password minimum length

**File:** `src/api/RequestValidator.cpp:33`
**Finding:** `validatePassword()` only checks non-empty + max 1024 chars. No minimum length. The CLI enforces 8 chars but the API does not.

**Fix:**
Add minimum length check in `validatePassword()`:
```cpp
void RequestValidator::validatePassword(const std::string& s) {
  if (s.size() < 8)
    throw common::ValidationError("PASSWORD_TOO_SHORT",
        "Password must be at least 8 characters");
  validateStringLength(s, "password", 1024);
}
```

**Files modified:**
- `src/api/RequestValidator.cpp` — add minimum length check

### 1.3 SEC-I2 — Rate limiter on change-password

**File:** `src/api/routes/AuthRoutes.cpp:138`
**Finding:** The `g_rlLogin` rate limiter only protects the login endpoint. The change-password endpoint performs password verification but is not rate-limited.

**Fix:**
Reuse the existing `g_rlLogin` rate limiter instance. In the change-password handler, extract client IP (same pattern as login) and call `g_rlLogin.allow(sClientIp)` before processing. The same 5 req/60s per IP limit applies.

```cpp
// In the change-password handler, before authentication:
std::string sClientIp = req.get_header_value("X-Forwarded-For");
if (sClientIp.empty()) sClientIp = req.remote_ip_address;
if (!g_rlLogin.allow(sClientIp))
  throw common::RateLimitedError("RATE_LIMITED",
                                 "Too many attempts. Try again later.");
```

**Files modified:**
- `src/api/routes/AuthRoutes.cpp` — add rate limit check to change-password handler

### 1.4 CR-API-01 — Max request body size

**File:** `src/api/ApiServer.cpp:59`
**Finding:** No maximum request body size configured. Crow defaults to ~4 MB. SECURITY_PLAN.md M-3 specifies 64 KB.

**Fix:**
Investigate Crow v1.3.1 API for body size limits. Options in order of preference:
1. If Crow supports `app.max_payload(65536)` or equivalent — use it in `ApiServer::start()`
2. Otherwise, add a body size check utility in `RouteHelpers` and call it at the start of every route handler that parses JSON
3. As a last resort, implement a Crow `before_handle` middleware that checks `Content-Length`

Whichever approach is chosen, requests exceeding 64 KB should receive a `413 Payload Too Large` response.

**Files modified:**
- `src/api/ApiServer.cpp` — add max payload configuration
- Possibly `src/api/RouteHelpers.cpp` + `include/api/RouteHelpers.hpp` — body size guard utility
- Possibly `include/common/Errors.hpp` — add `PayloadTooLargeError` if needed

---

## 2. Defense-in-Depth Code Changes

### 2.1 M4 — Security response headers

**File:** `src/api/RouteHelpers.cpp:14`
**Finding:** `applySecurityHeaders()` is a no-op. The reverse proxy is expected to set headers, but the application should provide baseline defense-in-depth.

**Fix:**
Implement the function with three baseline headers:
```cpp
void applySecurityHeaders(crow::response& resp) {
  resp.set_header("X-Content-Type-Options", "nosniff");
  resp.set_header("X-Frame-Options", "DENY");
  resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
}
```

This function is already called by `jsonResponse()`, `errorResponse()`, and `invalidJsonResponse()`, so all API responses automatically get the headers. Also apply to `StaticFileHandler.cpp` for HTML/JS/CSS responses served to the browser.

Update the comment in `RouteHelpers.cpp` to reflect that baseline headers are now set in-application, with the reverse proxy providing additional headers (CSP, HSTS, Permissions-Policy).

**Files modified:**
- `src/api/RouteHelpers.cpp` — implement `applySecurityHeaders()`
- `src/api/StaticFileHandler.cpp` — apply security headers to static file responses

### 2.2 M6 — ConnectionPool URL logging

**File:** `src/dal/ConnectionPool.cpp:52`
**Finding:** `_sDbUrl.substr(0, _sDbUrl.find('@'))` logs `postgresql://user:password` — leaking credentials to logs.

**Fix:**
Extract and log only the host:port/dbname portion (after `@`):
```cpp
std::string sSafeUrl = _sDbUrl;
auto nAt = sSafeUrl.find('@');
if (nAt != std::string::npos) {
  sSafeUrl = sSafeUrl.substr(nAt + 1);
}
spLog->info("Initializing connection pool: size={}, host={}",
            _iPoolSize, sSafeUrl);
```

**Files modified:**
- `src/dal/ConnectionPool.cpp` — fix log line to show only host portion

### 2.3 SEC-I1 — X-Forwarded-For trust model (documentation + code comment)

**Files:** `src/api/RouteHelpers.cpp:23`, `docs/DEPLOYMENT.md`, `docs/ARCHITECTURE.md`
**Finding:** `X-Forwarded-For` is trusted unconditionally. An attacker sending direct requests can spoof the header.

**Fix (documentation-first):**
1. Add a comment in `RouteHelpers.cpp` explaining the trust model and referencing DEPLOYMENT.md
2. Update `docs/DEPLOYMENT.md` with a "Security: Reverse Proxy Requirement" section stating:
   - A trusted reverse proxy is **required** for production deployments
   - The application trusts `X-Forwarded-For` unconditionally
   - Without a reverse proxy, IP-based rate limiting and audit log IP attribution are unreliable
   - Direct exposure of port 8080 to untrusted networks is not supported
3. Add a note about a future `DNS_TRUSTED_PROXIES` configuration variable
4. Update `docs/ARCHITECTURE.md` §12.1 with this trust model

**Files modified:**
- `src/api/RouteHelpers.cpp` — add explanatory comment
- `docs/DEPLOYMENT.md` — add reverse proxy trust section
- `docs/ARCHITECTURE.md` — update §12.1

---

## 3. Code Quality Improvements

### 3.1 M1 — Consolidate base64url encoding

**Files:** `src/security/HmacJwtSigner.cpp:24`, `src/security/OidcService.cpp:32`, `src/security/SamlService.cpp:46`, `src/security/CryptoService.cpp:118`
**Finding:** Four separate base64url encoding implementations in anonymous namespaces.

**Fix:**
1. Add a `std::string` overload to `CryptoService`:
   ```cpp
   static std::string base64UrlEncode(const std::string& sData);
   ```
   This wraps the existing `base64UrlEncode(const std::vector<unsigned char>&)`.
2. Remove the anonymous namespace `base64UrlEncode` from:
   - `HmacJwtSigner.cpp` (also has `base64UrlDecode` — add that to `CryptoService` too)
   - `OidcService.cpp`
3. `SamlService.cpp` has `base64EncodeBytes()` which is standard base64 (not URL-safe) — leave it as-is since it serves a different purpose (SAML uses standard base64).
4. Update all call sites to use `CryptoService::base64UrlEncode()` / `CryptoService::base64UrlDecode()`.

**Files modified:**
- `include/security/CryptoService.hpp` — add string overload + `base64UrlDecode()` declaration
- `src/security/CryptoService.cpp` — implement string overload + `base64UrlDecode()`
- `src/security/HmacJwtSigner.cpp` — remove anonymous namespace functions, use `CryptoService`
- `src/security/OidcService.cpp` — remove anonymous namespace function, use `CryptoService`

### 3.2 M2 — Replace std::gmtime with gmtime_r

**Files:** 7 call sites across 6 files
**Finding:** `std::gmtime()` returns a pointer to static storage, not thread-safe. Used in a multi-threaded Crow server.

**Fix:**
1. Create `include/common/TimeUtils.hpp` and `src/common/TimeUtils.cpp` with:
   ```cpp
   namespace dns::common {
     /// Format current time as ISO 8601 UTC: 2026-03-17T01:30:00Z
     std::string nowIso8601();
     /// Format a time_t as ISO 8601 UTC
     std::string toIso8601(std::time_t tt);
     /// Compact format for filenames: 2026-03-17T013000Z
     std::string toIso8601Compact(std::time_t tt);
   }
   ```
   All use `gmtime_r()` internally.
2. Replace all 7 call sites:
   - `src/core/DeploymentEngine.cpp:96` and `:132` — use `toIso8601()`
   - `src/gitops/GitRepoManager.cpp:175` — use `toIso8601()`
   - `src/gitops/GitOpsMirror.cpp:268` — use `toIso8601()`
   - `src/core/BackupService.cpp:38` — promote existing `nowIso8601()` → use shared utility
   - `src/api/routes/BackupRoutes.cpp:32` — use `toIso8601Compact()` (different format for filenames)
   - `src/api/routes/AuditRoutes.cpp:34` — use `nowIso8601()`
3. Update `src/CMakeLists.txt` to include the new source file.

**Files modified:**
- `include/common/TimeUtils.hpp` — new file
- `src/common/TimeUtils.cpp` — new file
- `src/CMakeLists.txt` — add TimeUtils.cpp
- `src/core/DeploymentEngine.cpp` — replace 2 call sites
- `src/gitops/GitRepoManager.cpp` — replace 1 call site
- `src/gitops/GitOpsMirror.cpp` — replace 1 call site
- `src/core/BackupService.cpp` — replace local function with shared utility
- `src/api/routes/BackupRoutes.cpp` — replace 1 call site
- `src/api/routes/AuditRoutes.cpp` — replace 1 call site

### 3.3 M7 — Email format validation

**Files:** `src/api/routes/AuthRoutes.cpp:119`, `src/api/routes/SetupRoutes.cpp:84`
**Finding:** Email fields only checked for non-empty. No format validation.

**Fix:**
Add `RequestValidator::validateEmail()` to `RequestValidator`:
```cpp
void RequestValidator::validateEmail(const std::string& sEmail) {
  validateStringLength(sEmail, "email", 254);
  auto nAt = sEmail.find('@');
  if (nAt == std::string::npos || nAt == 0 || nAt == sEmail.size() - 1)
    throw common::ValidationError("INVALID_EMAIL", "Invalid email format");
  auto sDomain = sEmail.substr(nAt + 1);
  if (sDomain.find('.') == std::string::npos)
    throw common::ValidationError("INVALID_EMAIL", "Invalid email format");
}
```
Apply to all email acceptance points:
- `AuthRoutes.cpp` profile update (line 119)
- `SetupRoutes.cpp` initial admin creation (line 84)
- Any user creation/update routes in `UserRoutes.cpp` (if email field exists)

**Files modified:**
- `include/api/RequestValidator.hpp` — add `validateEmail()` declaration
- `src/api/RequestValidator.cpp` — implement `validateEmail()`
- `src/api/routes/AuthRoutes.cpp` — use `validateEmail()` for profile update
- `src/api/routes/SetupRoutes.cpp` — use `validateEmail()` for setup

### 3.4 M8 — auto-capture system user

**File:** `src/main.cpp:421`
**Finding:** `depEngine.capture(zone.iId, 1, acSystem, "auto-capture")` hardcodes user ID 1. If user 1 is deleted, this breaks with a FK violation.

**Fix:**
1. Create a database migration (`scripts/db/v015/001_system_user.sql`) that inserts a dedicated system user row:
   ```sql
   INSERT INTO users (username, email, password_hash, is_active, auth_method)
   VALUES ('_system', 'system@localhost', '', true, 'system')
   ON CONFLICT (username) DO NOTHING;
   ```
   Use `auth_method = 'system'` to prevent login. The `password_hash` is empty (no login possible). The `is_active = true` ensures FK constraints are satisfied.
2. At startup in `main.cpp`, after migrations run, look up the system user:
   ```cpp
   auto oSystemUser = urRepo.findByUsername("_system");
   int64_t iSystemUserId = oSystemUser ? oSystemUser->iId : 1; // fallback
   ```
3. Use `iSystemUserId` in the auto-capture call instead of hardcoded `1`.

**Files modified:**
- `scripts/db/v015/001_system_user.sql` — new migration
- `src/main.cpp` — look up system user, use its ID for auto-capture

---

## 4. Documentation Updates

### 4.1 ARCH-I3 + CR-AUTH-02 — RBAC scoping

**Files:** `docs/PERMISSIONS.md`, `README.md`
**Finding:** Documentation claims view-level and zone-level scoping, but enforcement is global-only.

**Changes:**
- `docs/PERMISSIONS.md` §"Hierarchical Scoping" (line 155): Rewrite to state that v1.0 permissions are **global only**. The data model supports scoping (columns exist) but enforcement is not yet implemented. Add a clear "Planned Enhancement" callout for view/zone scoped RBAC.
- `README.md:43`: Change "view-level and zone-level scoping" to "global RBAC with 44 discrete permissions" and note that view/zone scoping is planned.

### 4.2 CR-AUTH-03 — Rate limiter & security headers documentation reconciliation

**Files:** `docs/ARCHITECTURE.md`, `docs/plans/SECURITY_PLAN.md`
**Finding:** ARCHITECTURE.md §12.4 says "The application does not implement in-process rate limiting" — but it does. SECURITY_PLAN.md L-1 says rate limiting is delegated — but it was implemented in-process.

**Changes:**
- `docs/ARCHITECTURE.md` §12.4 (line 1841): Rewrite section title to "Rate Limiting" (remove "Reverse Proxy" qualifier). State that the application implements in-process rate limiting on authentication endpoints (login, change-password) at 5 req/60s per IP as defense-in-depth. The reverse proxy provides additional rate limiting capabilities and should be configured for production. Keep the nginx/Caddy/Traefik examples.
- `docs/plans/SECURITY_PLAN.md` L-1 (line 390): Add addendum: "**v1.0 Update:** In-process rate limiting was implemented for login and change-password endpoints via `RateLimiter.cpp`. The reverse proxy still provides additional rate limiting as documented."
- `docs/ARCHITECTURE.md` §4.6.4 (line 625): Update to state that baseline security headers (`X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy`) are set in-application. The reverse proxy provides additional headers (CSP, HSTS, Permissions-Policy). Remove "no-op" language.
- `docs/plans/SECURITY_PLAN.md` M-2 (line 294): Add addendum: "**v1.0 Update:** Baseline headers implemented in `applySecurityHeaders()`. Full header set (CSP, HSTS) delegated to reverse proxy."

### 4.3 CR-API-01 — Max payload SECURITY_PLAN.md addendum

**File:** `docs/plans/SECURITY_PLAN.md`
**Finding:** M-3 specified 64 KB max body, now being implemented.

**Changes:**
- `docs/plans/SECURITY_PLAN.md` M-3 (line 324): Add addendum: "**v1.0 Update:** 64 KB maximum request body size implemented."

### 4.4 CR-FED-01 — OIDC state storage documentation

**Files:** `docs/ARCHITECTURE.md`, `docs/plans/SECURITY_PLAN.md`
**Finding:** SECURITY_PLAN.md C-2 specified HMAC-signed cookie for OIDC state. Actual implementation uses in-memory map.

**Changes:**
- `docs/plans/SECURITY_PLAN.md` C-2 (line 91): Add addendum: "**v1.0 Implementation Note:** The `state` parameter is stored in an in-memory map with 10-minute TTL and mutex protection, rather than the cookie-based approach described above. This provides equivalent security for single-instance deployments. The cookie-based approach is considered a future enhancement for multi-instance compatibility."
- `docs/ARCHITECTURE.md` §12.5 multi-instance note (line 1905): Extend the existing SAML note to also mention OIDC state: "The OIDC state map (`OidcService._mAuthStates`) is also process-local. Multi-instance deployments require externalization of both the SAML replay cache and OIDC state store."

### 4.5 CR-FED-02 — OIDC nonce documentation

**Files:** `docs/AUTHENTICATION.md`, `docs/plans/SECURITY_PLAN.md`
**Finding:** SECURITY_PLAN.md C-2 specified nonce validation. The nonce parameter was not implemented; PKCE provides equivalent protection.

**Changes:**
- `docs/plans/SECURITY_PLAN.md` C-2: Include in the addendum from §4.4 above: "The `nonce` parameter is not implemented. PKCE (S256) provides equivalent protection against authorization code injection. Adding `nonce` is planned as a future defense-in-depth measure."
- `docs/AUTHENTICATION.md`: Add a note in the OIDC section that nonce is not implemented, with PKCE providing equivalent protection.

### 4.6 ARCH-I1 — SamlReplayCache persistence documentation

**File:** `docs/ARCHITECTURE.md`
**Finding:** The in-memory SamlReplayCache is lost on restart, creating a brief replay window.

**Changes:**
- `docs/ARCHITECTURE.md` §12.5 multi-instance note area: Add a new subsection or extend the existing note: "**Known limitation:** The `SamlReplayCache` is in-memory only. On application restart, previously seen assertion IDs are lost, creating a replay window bounded by the assertion's `NotOnOrAfter` timestamp (typically 5 minutes). **Potential enhancement:** Write the cache to local storage on clean shutdown and reload on startup to eliminate the restart replay window."

### 4.7 CR-INFRA-02 — PostgreSQL network isolation

**Files:** `docs/DEPLOYMENT.md`, `docker-compose.yml`
**Finding:** PostgreSQL port not exposed but network isolation could be more explicit.

**Changes:**
- `docker-compose.yml`: Add explicit Docker network definitions:
  ```yaml
  networks:
    internal:
      driver: bridge
      internal: true
  
  services:
    db:
      networks:
        - internal
    app:
      networks:
        - internal
  ```
  The `db` service is only on the internal network. The `app` service connects to internal (for DB) and exposes its port via `ports`.
- `docs/DEPLOYMENT.md`: Add a "Network Isolation" subsection after the Docker Compose section explaining the network topology and why PostgreSQL should not be accessible from untrusted networks.

### 4.8 AUTHENTICATION.md JWT structure fix

**File:** `docs/AUTHENTICATION.md:159`
**Finding:** Documents `sid` (session ID) in JWT claims, but the actual JWT payload does not contain `sid`.

**Changes:**
- `docs/AUTHENTICATION.md` line 159: Remove `sid` from the documented JWT claims. The actual payload contains: `sub` (user ID), `username`, `role`, `auth_method`, `iat`, `exp`.

### 4.9 .env.example expansion

**File:** `.env.example`
**Finding:** Only lists 5 of 20+ environment variables.

**Changes:**
- Add commonly needed seed variables with comments:
  ```env
  # Database connection (set automatically in Docker Compose)
  # DNS_DB_URL=postgresql://user:pass@host:5432/meridian_dns
  
  # Session configuration
  # DNS_SESSION_ABSOLUTE_TTL_SECONDS=86400
  
  # Audit
  # DNS_AUDIT_RETENTION_DAYS=365
  # DNS_AUDIT_STDOUT=false
  
  # OIDC/SAML base URL (required for federated auth)
  # DNS_BASE_URL=https://dns.example.com
  
  # HTTP server
  # DNS_HTTP_THREADS=4
  # DNS_DB_POOL_SIZE=10
  ```
- Add a reference to `docs/CONFIGURATION.md` for the complete list.

### 4.10 Section 5 remaining inconsistencies from Code Review 1

**File:** `docs/ARCHITECTURE.md`
**Additional fixes from the Section 5 table in CODE_REVIEW_1.0.md:**
- §12.4 and SECURITY_PLAN.md L-1 — covered by §4.2 above
- SECURITY_PLAN.md C-2 — covered by §4.4 and §4.5 above
- SECURITY_PLAN.md M-3 — covered by §4.3 above
- PERMISSIONS.md scoping — covered by §4.1 above
- AUTHENTICATION.md JWT `sid` — covered by §4.8 above

---

## 5. Files Changed Summary

### New Files
| File | Purpose |
|------|---------|
| `include/common/TimeUtils.hpp` | Thread-safe ISO 8601 timestamp utilities |
| `src/common/TimeUtils.cpp` | Implementation |
| `scripts/db/v015/001_system_user.sql` | System user for automated operations |

### Modified Source Files
| File | Changes |
|------|---------|
| `src/api/RequestValidator.cpp` | SEC-I3: min password length; M7: email validation |
| `include/api/RequestValidator.hpp` | M7: `validateEmail()` declaration |
| `src/api/routes/AuthRoutes.cpp` | SEC-I4: logout session delete; SEC-I2: rate limit change-password |
| `include/api/routes/AuthRoutes.hpp` | SEC-I4: add `SessionRepository&` member |
| `src/api/ApiServer.cpp` | SEC-I4: pass SessionRepository; CR-API-01: max payload |
| `include/api/ApiServer.hpp` | SEC-I4: updated constructor signature (if needed) |
| `src/main.cpp` | SEC-I4: wiring; M8: system user lookup |
| `src/api/RouteHelpers.cpp` | M4: security headers; SEC-I1: trust model comment |
| `src/api/StaticFileHandler.cpp` | M4: apply security headers to static responses |
| `src/dal/ConnectionPool.cpp` | M6: mask DB URL in logs |
| `src/security/CryptoService.cpp` | M1: add string overload + base64UrlDecode |
| `include/security/CryptoService.hpp` | M1: declarations |
| `src/security/HmacJwtSigner.cpp` | M1: remove local base64url, use CryptoService |
| `src/security/OidcService.cpp` | M1: remove local base64url, use CryptoService |
| `src/core/DeploymentEngine.cpp` | M2: use TimeUtils |
| `src/gitops/GitRepoManager.cpp` | M2: use TimeUtils |
| `src/gitops/GitOpsMirror.cpp` | M2: use TimeUtils |
| `src/core/BackupService.cpp` | M2: use TimeUtils |
| `src/api/routes/BackupRoutes.cpp` | M2: use TimeUtils |
| `src/api/routes/AuditRoutes.cpp` | M2: use TimeUtils |
| `src/api/routes/SetupRoutes.cpp` | M7: use validateEmail |
| `src/CMakeLists.txt` | M2: add TimeUtils.cpp |
| `include/common/Errors.hpp` | CR-API-01: possibly add PayloadTooLargeError |

### Modified Documentation Files
| File | Changes |
|------|---------|
| `docs/PERMISSIONS.md` | ARCH-I3: rewrite scoping as global-only, planned enhancement |
| `README.md` | ARCH-I3: update RBAC description |
| `docs/ARCHITECTURE.md` | CR-AUTH-03: §12.4 rate limiter; §4.6.4 security headers; SEC-I1: §12.1 trust model; ARCH-I1: replay cache note; CR-FED-01: multi-instance OIDC note |
| `docs/plans/SECURITY_PLAN.md` | Addenda for L-1, M-2, M-3, C-2 |
| `docs/AUTHENTICATION.md` | JWT claims fix; CR-FED-02: nonce note |
| `docs/DEPLOYMENT.md` | SEC-I1: reverse proxy requirement; CR-INFRA-02: network isolation |
| `docker-compose.yml` | CR-INFRA-02: explicit network definitions |
| `.env.example` | Expand with seed variables |

---

## 6. Implementation Order

Tasks are ordered to minimize conflicts and allow incremental testing. Security fixes first, then quality improvements, then documentation.

### Phase A — Security Fixes
1. SEC-I4: Logout session invalidation
2. SEC-I3: Password minimum length
3. SEC-I2: Rate limiter on change-password
4. CR-API-01: Max request body size
5. M4: Security response headers

### Phase B — Code Quality
6. M2: TimeUtils — shared gmtime_r utility (new files, then replace call sites)
7. M1: Consolidate base64url encoding into CryptoService
8. M6: ConnectionPool URL logging fix
9. M7: Email format validation
10. M8: System user for auto-capture

### Phase C — Documentation
11. ARCH-I3: PERMISSIONS.md + README.md RBAC scoping
12. CR-AUTH-03: ARCHITECTURE.md §12.4 + §4.6.4 + SECURITY_PLAN.md L-1, M-2
13. CR-FED-01 + CR-FED-02: OIDC state/nonce documentation
14. ARCH-I1: SamlReplayCache persistence note
15. SEC-I1: Reverse proxy trust model (DEPLOYMENT.md + ARCHITECTURE.md + code comment)
16. CR-API-01: SECURITY_PLAN.md M-3 addendum
17. CR-INFRA-02: docker-compose.yml networks + DEPLOYMENT.md
18. AUTHENTICATION.md JWT claims fix
19. .env.example expansion
