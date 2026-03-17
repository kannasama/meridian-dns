# Release Preparation — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prepare Meridian DNS for its first public v1.0 release: AGPL-3.0 dual licensing, contributor agreement, comprehensive documentation, security headers, health probes, build versioning, OpenAPI audit, test quality gate, and release pipeline design.

**Architecture:** Three phases — (A) code changes (version infrastructure, health probes, security headers), (B) licensing and legal files with automated header validation, (C) documentation and quality gates. Code changes are TDD where applicable. Documentation is authored from the full codebase context spanning WS1-7. The OpenAPI spec is audited by cross-referencing route registrations against the spec file.

**Tech Stack:** C++20, CMake, Crow HTTP, libpqxx, nlohmann/json, Google Test, Vue 3 + PrimeVue, Bash (license script)

---

## Context

Workstream 8 from `docs/plans/2026-03-08-v1.0-design.md`. This is the final workstream, with dependencies on all prior workstreams (WS1-7). It produces no new features — only release infrastructure, documentation, and quality assurance.

## Key Design Decisions

1. **Version from CMake, not hardcoded** — `CMakeLists.txt` `project(VERSION 1.0.0)` is the single source of truth. A `configure_file()` step generates `Version.hpp` with `MERIDIAN_VERSION` injected at build time. All runtime references (main.cpp, BackupRoutes.cpp, health endpoints) use this constant.
2. **Security headers via enhanced `applySecurityHeaders()`** — The design specifies a Crow middleware struct, but this would require changing `crow::SimpleApp` to `crow::App<Middleware>` across 42+ file references (21 route headers, 21 implementations, ApiServer, StaticFileHandler, main.cpp). The practical approach: enhance the existing `applySecurityHeaders()` function in `RouteHelpers.cpp` with the design-specified headers, and also apply them in `StaticFileHandler` for static responses. The middleware refactor is deferred as a future improvement — the headers themselves are identical either way.
3. **Readiness probe uses DB-cached status** — `/health/ready` checks DB connectivity via `SELECT 1`, queries `git_repos.last_sync_status` counts, and queries total provider count. No live provider connectivity checks (too expensive for a probe called every 10-30s by orchestrators). Provider health checking is already available via the dashboard's `GET /providers/health` endpoint.
4. **HealthRoutes gains dependencies** — Currently a zero-dependency class. Readiness probe requires `ConnectionPool&` (for DB check), `GitRepoRepository&` (for sync status), and `ProviderRepository&` (for provider count). The liveness probe remains dependency-free.
5. **License headers applied via script + sed** — `scripts/check-license-headers.sh` validates presence; bulk application uses `sed` one-liners per file type (C++ `//`, Vue `<!-- -->`, TS `//`, SQL `--`).
6. **`docs/internal/` is git-ignored** — Contains operational docs (release pipeline) not intended for the public GitHub mirror. Added to `.gitignore`.
7. **Documentation files are standalone** — Each doc file (DEPLOYMENT.md, CONFIGURATION.md, etc.) is self-contained and written as a separate task. No cross-file dependency ordering.
8. **Smoke tests augment existing WS3-7 integration tests** — The existing test files (`test_permission_service.cpp`, `test_federated_auth.cpp`, `test_git_repo_manager.cpp`, `test_backup_restore.cpp`, `test_zone_capture.cpp`) are reviewed against the design's coverage requirements. Missing happy-path coverage is added; no new test files created unless gaps are found.

---

## Critical Files

### New files
| File | Purpose |
|------|---------|
| `include/common/Version.hpp.in` | CMake template for version constant |
| `LICENSE` | AGPL-3.0-or-later full text |
| `COMMERCIAL-LICENSE.md` | Commercial licensing terms |
| `CLA.md` | Individual Contributor License Agreement |
| `CONTRIBUTING.md` | Contribution guidelines |
| `SECURITY.md` | Responsible disclosure policy |
| `CHANGELOG.md` | Keep a Changelog format, v1.0.0 entry |
| `scripts/check-license-headers.sh` | SPDX header validation script |
| `docs/DEPLOYMENT.md` | Docker Compose, env vars, reverse proxy |
| `docs/CONFIGURATION.md` | Complete settings reference |
| `docs/AUTHENTICATION.md` | Local, OIDC, SAML auth guides |
| `docs/GITOPS.md` | Multi-repo GitOps setup |
| `docs/PERMISSIONS.md` | Permission model and roles |
| `docs/internal/RELEASE.md` | Release pipeline design (git-ignored) |
| `docs/screenshots/` | README screenshot directory (images added manually) |

### Modified files
| File | Change |
|------|--------|
| `CMakeLists.txt` | Version → `1.0.0`, add `configure_file()` for Version.hpp |
| `src/main.cpp` | Replace `kVersion` with generated `MERIDIAN_VERSION`, update startup log |
| `src/api/routes/BackupRoutes.cpp` | Replace hardcoded `"0.1.0"` with `MERIDIAN_VERSION` |
| `include/api/routes/HealthRoutes.hpp` | Add constructor deps for readiness probe |
| `src/api/routes/HealthRoutes.cpp` | Add `/health/live`, `/health/ready`, version in `/health` |
| `src/api/RouteHelpers.cpp` | Update security headers per design |
| `src/api/StaticFileHandler.cpp` | Apply security headers to static responses |
| `Dockerfile` | HEALTHCHECK → `/api/v1/health/live` |
| `.gitignore` | Add `docs/internal/` |
| `docs/openapi.yaml` | Version → `1.0.0`, add missing WS3-7 endpoints |
| `README.md` | Complete rewrite for public audience |
| `docs/ARCHITECTURE.md` | Refresh for WS1-7 changes |
| All `src/**/*.cpp`, `include/**/*.hpp` | Add SPDX license header |
| All `ui/src/**/*.ts`, `ui/src/**/*.vue` | Add SPDX license header |
| All `scripts/db/**/*.sql` | Add SPDX license header |

### No CMakeLists.txt (src/) changes needed
`src/CMakeLists.txt` uses `GLOB_RECURSE` — no new `.cpp` source files are created.

---

## Reusable Patterns & Utilities

| Pattern | Source | Reuse |
|---------|--------|-------|
| Route registration | `src/api/routes/HealthRoutes.cpp:13-17` | `CROW_ROUTE(app, "/path").methods(...)` |
| JSON response | `include/api/RouteHelpers.hpp` | `jsonResponse(status, json)` |
| Security headers | `src/api/RouteHelpers.cpp:6-11` | `applySecurityHeaders(resp)` |
| Health endpoint | `src/api/routes/HealthRoutes.cpp:14-17` | Existing `/health` pattern |
| DB connection | `include/dal/ConnectionPool.hpp` | `ConnectionGuard cg(_cpPool); auto& conn = cg.connection();` |
| Repo listAll/listEnabled | `include/dal/GitRepoRepository.hpp:52-55` | `listAll()`, `listEnabled()` |
| Config load | `src/main.cpp:131` | `Config::load()` for DB URL access |
| Route auth pattern | `src/api/routes/AuditRoutes.cpp:66-67` | `authenticate()` + `requirePermission()` |
| Integration test fixture | `tests/integration/test_crud_routes.cpp` | Crow `handle_full()` pattern |

---

## Tasks

### Task 1: Version Infrastructure — Template and CMake

**Files:**
- Create: `include/common/Version.hpp.in`
- Modify: `CMakeLists.txt`

**Step 1:** Create the version header template.

```cpp
// include/common/Version.hpp.in
#pragma once

#define MERIDIAN_VERSION "@PROJECT_VERSION@"
#define MERIDIAN_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define MERIDIAN_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define MERIDIAN_VERSION_PATCH @PROJECT_VERSION_PATCH@
```

**Step 2:** Update `CMakeLists.txt` — bump version to `1.0.0` and add `configure_file()`.

In the `project()` call at line 2, change `VERSION 0.1.0` to `VERSION 1.0.0`.

After the `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` line (~17), add:

```cmake
# ── Version header generation ──────────────────────────────────────────────
configure_file(
  ${CMAKE_SOURCE_DIR}/include/common/Version.hpp.in
  ${CMAKE_BINARY_DIR}/generated/common/Version.hpp
)
```

**Step 3:** Update `src/CMakeLists.txt` — add the generated header to the include path.

After the existing `target_include_directories` that adds `${CMAKE_SOURCE_DIR}/include`, add:

```cmake
target_include_directories(meridian-core PUBLIC ${CMAKE_BINARY_DIR}/generated)
```

**Step 4:** Build to verify generation: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`

Verify the file exists: `cat build/generated/common/Version.hpp` — should show `#define MERIDIAN_VERSION "1.0.0"`.

**Step 5:** Commit: `git add -A && git commit -m "build: add Version.hpp.in with CMake configure_file"`

---

### Task 2: Replace Hardcoded Version References

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/api/routes/BackupRoutes.cpp`

**Step 1:** In `src/main.cpp`, add the version include and remove the hardcoded constant.

Add after the existing includes (~line 1-30):

```cpp
#include "common/Version.hpp"
```

Remove lines 88-89 (the `kVersion` constant):

```cpp
// NOLINTNEXTLINE(cert-err58-cpp) — version string must be available before Config::load()
constexpr std::string_view kVersion = "0.1.0";
```

Replace `kVersion` usage at line 124 with `MERIDIAN_VERSION`:

```cpp
std::cout << "meridian-dns " << MERIDIAN_VERSION << "\n";
```

Update the startup ready log at line 540 to include version:

```cpp
spLog->info("meridian-dns {} ready", MERIDIAN_VERSION);
```

**Step 2:** In `src/api/routes/BackupRoutes.cpp`, add version include and replace hardcoded version.

Add include:

```cpp
#include "common/Version.hpp"
```

At line 222, replace:

```cpp
jBackup["meridian_version"] = "0.1.0";
```

with:

```cpp
jBackup["meridian_version"] = MERIDIAN_VERSION;
```

**Step 3:** Build to verify: `cmake --build build --parallel`

**Step 4:** Verify --version output: `build/src/meridian-dns --version`

Expected: `meridian-dns 1.0.0`

**Step 5:** Commit: `git add -A && git commit -m "build: replace hardcoded version with MERIDIAN_VERSION"`

---

### Task 3: Security Headers Enhancement

**Files:**
- Modify: `src/api/RouteHelpers.cpp`
- Modify: `src/api/StaticFileHandler.cpp`

**Step 1:** Update `applySecurityHeaders()` in `src/api/RouteHelpers.cpp` (lines 6-11).

Replace the current function body with:

```cpp
void applySecurityHeaders(crow::response& resp) {
  resp.set_header("X-Content-Type-Options", "nosniff");
  resp.set_header("X-Frame-Options", "DENY");
  resp.set_header("X-XSS-Protection", "0");
  resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
  resp.set_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
}
```

Note: The existing `Content-Security-Policy: default-src 'self'` header is **removed** — CSP is delegated to the reverse proxy per the design (deployment-specific, depends on allowed origins).

**Step 2:** In `src/api/StaticFileHandler.cpp`, apply security headers to static file responses. Find every `crow::response` construction/return and call `applySecurityHeaders()` on the response before returning. Add the include:

```cpp
#include "api/RouteHelpers.hpp"
```

For each response in the static file handler (SPA fallback, file serve), add before returning:

```cpp
dns::api::applySecurityHeaders(resp);
```

**Step 3:** Build to verify: `cmake --build build --parallel`

**Step 4:** Commit: `git add -A && git commit -m "security: update response headers per OWASP recommendations"`

---

### Task 4: Health Liveness Probe — Write Failing Test

**Files:**
- Modify: `tests/integration/test_crud_routes.cpp` (or create `tests/integration/test_health_routes.cpp`)

**Step 1:** Write a test for `GET /api/v1/health/live`. Check the existing `test_crud_routes.cpp` to see if health tests exist there. If so, add to it. Otherwise create a new test file.

The test should:
1. Register HealthRoutes on a Crow app
2. Call `handle_full()` for `GET /api/v1/health/live`
3. Expect 200 with JSON body containing `"status": "alive"` and `"version"` field

```cpp
TEST_F(HealthRoutesTest, LiveProbeReturnsAliveAndVersion) {
  auto [resp, body] = handle("GET", "/api/v1/health/live");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(body);
  EXPECT_EQ(j["status"], "alive");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
}
```

**Step 2:** Build and run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="*LiveProbe*"`

Expected: FAIL (route not registered yet).

**Step 3:** Commit: `git add -A && git commit -m "test: add liveness probe test (red)"`

---

### Task 5: Health Liveness Probe — Implement

**Files:**
- Modify: `src/api/routes/HealthRoutes.cpp`

**Step 1:** Add the version include and the liveness route.

```cpp
#include "common/Version.hpp"
```

In `registerRoutes()`, after the existing `/api/v1/health` route, add:

```cpp
CROW_ROUTE(app, "/api/v1/health/live")
    .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
      return jsonResponse(200, {
        {"status", "alive"},
        {"version", MERIDIAN_VERSION}
      });
    });
```

**Step 2:** Build and run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="*LiveProbe*"`

Expected: PASS.

**Step 3:** Commit: `git add -A && git commit -m "feat(health): add GET /health/live liveness probe"`

---

### Task 6: Health Readiness Probe — Update HealthRoutes Dependencies

**Files:**
- Modify: `include/api/routes/HealthRoutes.hpp`
- Modify: `src/api/routes/HealthRoutes.cpp`
- Modify: `src/main.cpp`

**Step 1:** Update `include/api/routes/HealthRoutes.hpp` to accept dependencies.

```cpp
#pragma once

#include <crow.h>
#include <string>

namespace dns::dal {
class ConnectionPool;
class GitRepoRepository;
class ProviderRepository;
}  // namespace dns::dal

namespace dns::api::routes {

/// Handlers for GET /api/v1/health, /health/live, /health/ready
class HealthRoutes {
 public:
  /// Liveness probe requires no dependencies.
  /// Readiness probe requires DB pool, repo access.
  HealthRoutes(dns::dal::ConnectionPool& cpPool,
               dns::dal::GitRepoRepository& grRepo,
               dns::dal::ProviderRepository& prRepo);
  ~HealthRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ConnectionPool& _cpPool;
  dns::dal::GitRepoRepository& _grRepo;
  dns::dal::ProviderRepository& _prRepo;
};

}  // namespace dns::api::routes
```

**Step 2:** Update HealthRoutes constructor in `src/api/routes/HealthRoutes.cpp`:

```cpp
HealthRoutes::HealthRoutes(dns::dal::ConnectionPool& cpPool,
                           dns::dal::GitRepoRepository& grRepo,
                           dns::dal::ProviderRepository& prRepo)
    : _cpPool(cpPool), _grRepo(grRepo), _prRepo(prRepo) {}
```

**Step 3:** Update construction in `src/main.cpp` (~line 477). Change:

```cpp
auto healthRoutes = std::make_unique<dns::api::routes::HealthRoutes>();
```

to:

```cpp
auto healthRoutes = std::make_unique<dns::api::routes::HealthRoutes>(
    *cpPool, *gitRepoRepo, *prRepo);
```

Verify the variable names match the actual pool/repo variables in main.cpp's scope. The ConnectionPool variable is likely named `cpPool` or similar — check around lines 250-280 of main.cpp.

**Step 4:** Build to verify: `cmake --build build --parallel`

**Step 5:** Commit: `git add -A && git commit -m "refactor(health): inject DB pool and repos into HealthRoutes"`

---

### Task 7: Health Readiness Probe — Write Failing Test

**Files:**
- Modify: test file from Task 4

**Step 1:** Write tests for `GET /api/v1/health/ready`. These are DB integration tests (need `DNS_DB_URL`).

```cpp
TEST_F(HealthRoutesTest, ReadyProbeReturns200WhenDbHealthy) {
  auto [resp, body] = handle("GET", "/api/v1/health/ready");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(body);
  EXPECT_EQ(j["status"], "healthy");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
  EXPECT_TRUE(j.contains("components"));
  EXPECT_EQ(j["components"]["database"], "healthy");
  EXPECT_TRUE(j["components"].contains("git_repos"));
  EXPECT_TRUE(j["components"].contains("providers"));
}
```

**Step 2:** Build and run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="*ReadyProbe*"`

Expected: FAIL (route not registered).

**Step 3:** Commit: `git add -A && git commit -m "test: add readiness probe test (red)"`

---

### Task 8: Health Readiness Probe — Implement

**Files:**
- Modify: `src/api/routes/HealthRoutes.cpp`

**Step 1:** Add the necessary includes:

```cpp
#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/ProviderRepository.hpp"
```

**Step 2:** Add the readiness route in `registerRoutes()`. The handler must capture `this`.

```cpp
CROW_ROUTE(app, "/api/v1/health/ready")
    .methods(crow::HTTPMethod::GET)([this](const crow::request& /*req*/) {
      nlohmann::json jComponents;

      // 1. Database check
      std::string sDbStatus = "unhealthy";
      try {
        auto cg = _cpPool.acquire();
        pqxx::work txn(cg.connection());
        txn.exec1("SELECT 1");
        txn.commit();
        sDbStatus = "healthy";
      } catch (...) {
        // DB unreachable
      }
      jComponents["database"] = sDbStatus;

      // 2. Git repos sync status
      int iGitHealthy = 0, iGitFailed = 0;
      try {
        auto vRepos = _grRepo.listEnabled();
        for (const auto& repo : vRepos) {
          if (repo.sLastSyncStatus == "success") ++iGitHealthy;
          else if (repo.sLastSyncStatus == "failed") ++iGitFailed;
          else ++iGitHealthy;  // never synced = not failed
        }
      } catch (...) {
        // If we can't query, don't crash the probe
      }
      jComponents["git_repos"] = {{"healthy", iGitHealthy}, {"failed", iGitFailed}};

      // 3. Providers count
      int iProviders = 0;
      try {
        auto vProviders = _prRepo.listAll();
        iProviders = static_cast<int>(vProviders.size());
      } catch (...) {}
      jComponents["providers"] = {{"healthy", iProviders}, {"degraded", 0}};

      // Overall status
      std::string sOverall = "healthy";
      if (sDbStatus == "unhealthy") {
        sOverall = "unhealthy";
      } else if (iGitFailed > 0) {
        sOverall = "degraded";
      }

      int iHttpCode = (sDbStatus == "healthy") ? 200 : 503;
      return jsonResponse(iHttpCode, {
        {"status", sOverall},
        {"version", MERIDIAN_VERSION},
        {"components", jComponents}
      });
    });
```

**Step 3:** Build and run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="*ReadyProbe*"`

Expected: PASS.

**Step 4:** Commit: `git add -A && git commit -m "feat(health): add GET /health/ready readiness probe"`

---

### Task 9: Update Existing Health Endpoint + Dockerfile

**Files:**
- Modify: `src/api/routes/HealthRoutes.cpp`
- Modify: `Dockerfile`

**Step 1:** Update the existing `/api/v1/health` route to include version:

```cpp
CROW_ROUTE(app, "/api/v1/health")
    .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
      return jsonResponse(200, {{"status", "ok"}, {"version", MERIDIAN_VERSION}});
    });
```

**Step 2:** Update `Dockerfile` HEALTHCHECK (line 57-58). Change:

```dockerfile
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD curl -sf http://localhost:8080/api/v1/health || exit 1
```

to:

```dockerfile
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD curl -sf http://localhost:8080/api/v1/health/live || exit 1
```

**Step 3:** Build to verify: `cmake --build build --parallel`

**Step 4:** Commit: `git add -A && git commit -m "feat(health): add version to /health, use /health/live for Docker HEALTHCHECK"`

---

### Task 10: License File — AGPL-3.0

**Files:**
- Create: `LICENSE`

**Step 1:** Create the `LICENSE` file with the full AGPL-3.0-or-later text.

Download or copy the canonical AGPL-3.0 text:

```bash
curl -sL https://www.gnu.org/licenses/agpl-3.0.txt > LICENSE
```

If offline, use the standard GNU AGPL-3.0-or-later text. The file should start with:

```
                    GNU AFFERO GENERAL PUBLIC LICENSE
                       Version 3, 19 November 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
```

**Step 2:** Verify file exists and is not empty: `wc -l LICENSE` (should be ~661 lines).

**Step 3:** Commit: `git add LICENSE && git commit -m "legal: add AGPL-3.0-or-later license"`

---

### Task 11: Legal Documents — COMMERCIAL-LICENSE, CLA, CONTRIBUTING, SECURITY

**Files:**
- Create: `COMMERCIAL-LICENSE.md`
- Create: `CLA.md`
- Create: `CONTRIBUTING.md`
- Create: `SECURITY.md`

**Step 1:** Create `COMMERCIAL-LICENSE.md`:

```markdown
# Meridian DNS — Commercial License

Meridian DNS is dual-licensed under the GNU Affero General Public License v3.0
(AGPL-3.0-or-later) and a commercial license.

## When You Need a Commercial License

If the AGPL-3.0 terms do not meet your deployment requirements — for example, if you
cannot comply with the source-code disclosure obligations, or if you need to embed
Meridian DNS in proprietary infrastructure — a commercial license is available.

## What the Commercial License Provides

- Freedom from AGPL-3.0 copyleft obligations
- Permission to modify and deploy without source disclosure
- Priority support options (separate agreement)

## Contact

For commercial licensing inquiries, contact:

**Email:** licensing@meridiandns.io

Please include:
- Your organization name
- Intended use case (internal tooling, SaaS, OEM, etc.)
- Approximate deployment scale

---

© 2026 Meridian DNS Contributors. All rights reserved.
```

**Step 2:** Create `CLA.md`:

```markdown
# Meridian DNS — Individual Contributor License Agreement

Thank you for your interest in contributing to Meridian DNS. This Contributor License
Agreement ("Agreement") documents the rights granted by contributors to the project
maintainer.

## 1. Definitions

- **"Contribution"** means any original work of authorship submitted to the project,
  including modifications or additions to existing work.
- **"Maintainer"** means the Meridian DNS project maintainer(s).
- **"You"** means the individual submitting a Contribution.

## 2. Grant of Copyright License

You hereby grant to the Maintainer a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable copyright license to reproduce, prepare derivative works of,
publicly display, publicly perform, sublicense, and distribute Your Contributions and
such derivative works.

## 3. Grant of Patent License

You hereby grant to the Maintainer a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable patent license to make, have made, use, offer to sell, sell,
import, and otherwise transfer the Contribution, where such license applies only to
those patent claims licensable by You that are necessarily infringed by Your
Contribution(s) alone or by combination of Your Contribution(s) with the project.

## 4. Representations

You represent that:
- You are legally entitled to grant the above licenses.
- Each Contribution is Your original creation.
- Your Contribution does not violate any third party's rights.

## 5. Dual Licensing

You understand and agree that the Maintainer may distribute the project (including
Your Contributions) under both the AGPL-3.0-or-later license and a separate
commercial license. This CLA enables the dual-license model.

## 6. No Obligation

Nothing in this Agreement obligates the Maintainer to accept or include any
Contribution.

---

By submitting a pull request to the Meridian DNS repository, you signify agreement
to these terms.
```

**Step 3:** Create `CONTRIBUTING.md`:

```markdown
# Contributing to Meridian DNS

Thank you for considering a contribution to Meridian DNS! This document explains the
process for contributing.

## Contributor License Agreement

All contributions require signing the [Contributor License Agreement](CLA.md). By
submitting a pull request, you agree to its terms. The CLA enables the project's
dual-license model (AGPL-3.0 + commercial).

## Getting Started

1. Fork the repository
2. Create a feature branch from `main`
3. Set up the [build environment](docs/BUILD_ENVIRONMENT.md)
4. Make your changes following the [code standards](docs/CODE_STANDARDS.md)

## Code Standards

- **C++20** with `-Wall -Wextra -Wpedantic -Werror`
- Hungarian notation naming conventions (see `docs/CODE_STANDARDS.md`)
- 2-space indentation, 100-character line limit
- `#pragma once` for header guards
- SPDX license headers on all source files

## Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(scope): add new feature
fix(scope): correct a bug
docs: update documentation
test: add or update tests
refactor(scope): code change that neither fixes a bug nor adds a feature
build: changes to build system or dependencies
```

## Pull Request Process

1. Ensure the build succeeds with no warnings: `cmake --build build --parallel`
2. All tests pass: `build/tests/dns-tests`
3. SPDX headers present: `scripts/check-license-headers.sh`
4. Update relevant documentation if behavior changes
5. Open a PR against `main` with a clear description

## Reporting Issues

Use GitHub Issues for bug reports and feature requests. Include:
- Steps to reproduce (for bugs)
- Expected vs actual behavior
- Meridian DNS version (`meridian-dns --version`)
- Database version and OS

## Security Vulnerabilities

**Do not** open a public issue for security vulnerabilities. See [SECURITY.md](SECURITY.md)
for the responsible disclosure process.
```

**Step 4:** Create `SECURITY.md`:

```markdown
# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Meridian DNS, please report it responsibly.

**Email:** security@meridiandns.io

**Do not** open a public GitHub issue for security vulnerabilities.

## What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact assessment
- Suggested fix (if any)

## Response Timeline

- **Acknowledgment:** within 72 hours of report
- **Assessment:** within 7 days
- **Fix target:** within 90 days of confirmed vulnerability

## Scope

The following are in scope:
- Meridian DNS server application (`meridian-dns` binary)
- Web UI (Vue 3 frontend)
- API endpoints and authentication system
- Database schema and migration scripts
- Docker image and deployment configuration

The following are **out of scope:**
- Third-party DNS providers (Cloudflare, DigitalOcean, PowerDNS) — report to those vendors
- Issues in upstream dependencies (Crow, libpqxx, libgit2, etc.) — report to those projects
- Social engineering attacks

## Supported Versions

| Version | Supported |
|---------|-----------|
| 1.0.x   | ✅        |
| < 1.0   | ❌        |

## Credits

We credit reporters in the changelog (with permission) when a fix is released.
```

**Step 5:** Commit: `git add COMMERCIAL-LICENSE.md CLA.md CONTRIBUTING.md SECURITY.md && git commit -m "legal: add commercial license, CLA, contributing guide, and security policy"`

---

### Task 12: License Header Validation Script

**Files:**
- Create: `scripts/check-license-headers.sh`

**Step 1:** Create the script:

```bash
#!/usr/bin/env bash
# scripts/check-license-headers.sh — Validate SPDX license headers on all source files.
# Exit 0 if all files have headers, exit 1 if any are missing.

set -euo pipefail

SPDX_ID="SPDX-License-Identifier: AGPL-3.0-or-later"
MISSING=0

check_files() {
  local pattern="$1"
  local comment_marker="$2"

  while IFS= read -r -d '' file; do
    if ! head -5 "$file" | grep -qF "$SPDX_ID"; then
      echo "MISSING: $file"
      MISSING=$((MISSING + 1))
    fi
  done < <(find . -path ./build -prune -o -path ./ui/node_modules -prune -o \
    -name "$pattern" -print0)
}

echo "Checking SPDX headers..."

# C++ source and headers
check_files "*.cpp" "//"
check_files "*.hpp" "//"

# TypeScript
check_files "*.ts" "//"

# Vue SFCs
check_files "*.vue" "<!--"

# SQL migrations
check_files "*.sql" "--"

if [ "$MISSING" -gt 0 ]; then
  echo ""
  echo "ERROR: $MISSING file(s) missing SPDX license header."
  exit 1
else
  echo "OK: All source files have SPDX headers."
  exit 0
fi
```

**Step 2:** Make it executable: `chmod +x scripts/check-license-headers.sh`

**Step 3:** Run it to verify it detects missing headers: `scripts/check-license-headers.sh`

Expected: Non-zero exit with a list of ~246 files missing headers (145 C++, 86 TS/Vue, 15 SQL).

**Step 4:** Commit: `git add scripts/check-license-headers.sh && git commit -m "build: add SPDX license header validation script"`

---

### Task 13: Apply License Headers — C++ Files

**Files:**
- Modify: All `src/**/*.cpp` and `include/**/*.hpp` files (~145 files)

**Step 1:** Apply the SPDX header to all C++ source files. For files with `#pragma once` (headers), insert after it. For `.cpp` files, insert at line 1.

For `.hpp` files (insert after `#pragma once`):

```bash
find include src -name '*.hpp' -print0 | xargs -0 -I{} sed -i '/#pragma once/{
a\
// SPDX-License-Identifier: AGPL-3.0-or-later\
// Copyright (c) 2026 Meridian DNS Contributors\
// This file is part of Meridian DNS. See LICENSE for details.
}' {}
```

For `.cpp` files (insert at line 1):

```bash
find src -name '*.cpp' -print0 | xargs -0 -I{} sed -i '1i\
// SPDX-License-Identifier: AGPL-3.0-or-later\n// Copyright (c) 2026 Meridian DNS Contributors\n// This file is part of Meridian DNS. See LICENSE for details.\n' {}
```

Also apply to test files:

```bash
find tests -name '*.cpp' -print0 | xargs -0 -I{} sed -i '1i\
// SPDX-License-Identifier: AGPL-3.0-or-later\n// Copyright (c) 2026 Meridian DNS Contributors\n// This file is part of Meridian DNS. See LICENSE for details.\n' {}
```

**Step 2:** Build to verify headers don't break compilation: `cmake --build build --parallel`

**Step 3:** Spot-check a few files to confirm correct placement:
- `head -5 include/api/routes/HealthRoutes.hpp` — header should be after `#pragma once`
- `head -5 src/main.cpp` — header should be at line 1

**Step 4:** Commit: `git add -A && git commit -m "legal: add SPDX headers to all C++ source files"`

---

### Task 14: Apply License Headers — Frontend & SQL Files

**Files:**
- Modify: All `ui/src/**/*.ts` (~40 files)
- Modify: All `ui/src/**/*.vue` (~30 files)
- Modify: All `scripts/db/**/*.sql` (~15 files)

**Step 1:** Apply to TypeScript files (insert at line 1):

```bash
find ui/src -name '*.ts' -print0 | xargs -0 -I{} sed -i '1i\
// SPDX-License-Identifier: AGPL-3.0-or-later\n// Copyright (c) 2026 Meridian DNS Contributors\n// This file is part of Meridian DNS. See LICENSE for details.\n' {}
```

**Step 2:** Apply to Vue SFCs (insert at line 1 using HTML comment):

```bash
find ui/src -name '*.vue' -print0 | xargs -0 -I{} sed -i '1i\
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->\n<!-- Copyright (c) 2026 Meridian DNS Contributors -->\n<!-- This file is part of Meridian DNS. See LICENSE for details. -->\n' {}
```

**Step 3:** Apply to SQL migrations (insert at line 1):

```bash
find scripts/db -name '*.sql' -print0 | xargs -0 -I{} sed -i '1i\
-- SPDX-License-Identifier: AGPL-3.0-or-later\n-- Copyright (c) 2026 Meridian DNS Contributors\n-- This file is part of Meridian DNS. See LICENSE for details.\n' {}
```

**Step 4:** Run the validation script: `scripts/check-license-headers.sh`

Expected: `OK: All source files have SPDX headers.` (exit 0).

**Step 5:** Commit: `git add -A && git commit -m "legal: add SPDX headers to frontend and SQL files"`

---

### Task 15: .gitignore Update and docs/internal/RELEASE.md

**Files:**
- Modify: `.gitignore`
- Create: `docs/internal/RELEASE.md`

**Step 1:** Add `docs/internal/` to `.gitignore`. Append to the `# ── Custom Ignores ──` section:

```
docs/internal/
```

**Step 2:** Create `docs/internal/RELEASE.md` (this file will be git-ignored, so it stays local only):

```markdown
# Meridian DNS — Release Pipeline Design

> This document is git-ignored (operational, not for public distribution).

## Container Image Strategy

- **Docker Hub:** `kannasama/meridian-dns` — primary public distribution
- **GHCR:** `ghcr.io/meridiandns/meridian-dns` — secondary, alongside GitHub mirror
- **Tags:** `latest`, semver (`1.0.0`), major (`1`), major.minor (`1.0`)
- **Architecture:** amd64-only for v1.0; arm64 planned for v1.1+

## CI/CD Pipeline Design

### Release Workflow (triggered by `v*` tag push)

1. **Build stage:**
   - Multi-stage Docker build (Fedora 43 builder, runtime image)
   - `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
   - UI build: `npm ci && npm run build`
2. **Test stage:**
   - Unit tests: `build/tests/dns-tests --gtest_filter=-*Integration*`
   - DB integration tests: requires PostgreSQL service container
   - License header check: `scripts/check-license-headers.sh`
3. **Publish stage:**
   - Build and push to Docker Hub + GHCR
   - Multi-registry push: `docker buildx build --push --tag kannasama/meridian-dns:1.0.0 --tag ghcr.io/meridiandns/meridian-dns:1.0.0 .`
   - Tag variants: `latest`, `1`, `1.0`, `1.0.0`
4. **Release stage:**
   - Create GitLab release entry (primary repo)
   - Create GitHub release entry (public mirror)

### Docker Hub Organization

- Organization: `kannasama`
- Repository: `kannasama/meridian-dns`
- Maintainer access: project maintainers only
- Automated builds: disabled (CI/CD pushes images)

### GHCR Publishing

- Via GitHub token (`GITHUB_TOKEN` or personal access token)
- Package visibility: public
- Linked to repository for discoverability

### Multi-Registry Push Commands

```bash
# Build for amd64
docker buildx build \
  --platform linux/amd64 \
  --push \
  --tag kannasama/meridian-dns:1.0.0 \
  --tag kannasama/meridian-dns:1.0 \
  --tag kannasama/meridian-dns:1 \
  --tag kannasama/meridian-dns:latest \
  --tag ghcr.io/meridiandns/meridian-dns:1.0.0 \
  --tag ghcr.io/meridiandns/meridian-dns:1.0 \
  --tag ghcr.io/meridiandns/meridian-dns:1 \
  --tag ghcr.io/meridiandns/meridian-dns:latest \
  .
```

## Release Checklist

1. [ ] All tests pass (unit + DB integration + smoke)
2. [ ] Clean build with no warnings (`-Wall -Wextra -Wpedantic -Werror`)
3. [ ] Version bumped in `CMakeLists.txt` (`project(VERSION X.Y.Z)`)
4. [ ] `CHANGELOG.md` updated with release date
5. [ ] OpenAPI spec version matches (`docs/openapi.yaml` → `info.version`)
6. [ ] All SPDX headers present (`scripts/check-license-headers.sh`)
7. [ ] Screenshots captured and added to `docs/screenshots/`
8. [ ] Documentation reviewed for accuracy
9. [ ] Git tag `vX.Y.Z` created and pushed
10. [ ] Docker images built and pushed to Docker Hub + GHCR
11. [ ] Release entries created on GitLab and GitHub
12. [ ] Verify image pullable: `docker pull kannasama/meridian-dns:X.Y.Z`
```

**Step 3:** Note: After committing `.gitignore`, the `docs/internal/` directory won't be tracked. To confirm the ignore works: `echo "test" > docs/internal/test.txt && git status` — `docs/internal/test.txt` should NOT appear as untracked. Then remove the test file.

**Step 4:** Commit: `git add .gitignore && git commit -m "build: add docs/internal/ to .gitignore, create RELEASE.md"`

Note: `docs/internal/RELEASE.md` is **not** committed (it's git-ignored). This is intentional per the design — operational docs stay local.

---

### Task 16: CHANGELOG.md

**Files:**
- Create: `CHANGELOG.md`

**Step 1:** Create `CHANGELOG.md` in [Keep a Changelog](https://keepachangelog.com/) format:

```markdown
# Changelog

All notable changes to Meridian DNS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-XX-XX

### Added

- **Multi-provider DNS management** — Orchestrate DNS records across PowerDNS,
  Cloudflare, and DigitalOcean through an extensible provider interface
- **Split-horizon views** — Define internal and external views for the same domain
  with strict isolation
- **Variable template engine** — Store records with `{{var_name}}` placeholders;
  update a variable once and propagate to all affected records
- **Preview-before-deploy pipeline** — Diff staged changes against live provider
  state, detect and resolve drift before pushing
- **Deployment snapshots and rollback** — Every push captures a full zone snapshot;
  roll back to any previous state (full zone or cherry-picked records)
- **Multi-repo GitOps** — Zone snapshots committed to configured Git repositories
  with SSH and HTTPS authentication, branch-per-zone support
- **Zone capture** — Auto-capture baseline snapshots for zones never deployed through
  Meridian; manual capture button for on-demand snapshots
- **Config backup and restore** — Full system export (JSON) and transactional restore
  via file upload or GitOps pull; zone-level export/import
- **Named theme presets** — 14 dark themes (Catppuccin Mocha, Dracula, Nord, Tokyo
  Night, etc.) and 9 light themes with independent accent color customization
- **Granular RBAC** — 40+ discrete permissions collected into customizable roles with
  view-level and zone-level scoping
- **OIDC and SAML authentication** — Federated login with auto-provisioning, IdP
  group-to-Meridian-group mapping, claim/attribute test diagnostic
- **User and group management** — Full CRUD, forced password change, self-service
  profile editing
- **API key authentication** — Create, list, and revoke API keys with one-time display
- **Database-backed configuration** — Runtime settings stored in DB with env-var
  seeding on first run
- **Batch record import** — CSV, JSON, DNSControl, and provider import with preview
- **Audit trail** — Every mutation logged with before/after state, actor identity,
  NDJSON export, configurable retention
- **Health probes** — Liveness (`/health/live`) and readiness (`/health/ready`)
  endpoints for container orchestrators
- **OpenAPI 3.1 specification** — Complete API documentation covering all endpoints
- **Web UI** — Vue 3 + PrimeVue with dark mode, responsive layout, deployment diff
  viewer, variable autocomplete

### Security

- AGPL-3.0-or-later + commercial dual licensing
- AES-256-GCM encryption for provider tokens, Git credentials, and IdP secrets
- Argon2id password hashing
- HMAC-SHA256 JWT sessions with sliding + absolute TTL
- SAML replay cache with TTL eviction
- PKCE for OIDC authorization code flow
- Security response headers (X-Content-Type-Options, X-Frame-Options,
  Referrer-Policy, Permissions-Policy)
- Rate limiting on authentication endpoints

### Infrastructure

- Multi-stage Docker image (Fedora 43, ~180MB runtime)
- Docker Compose with PostgreSQL 16
- Container health checks via liveness probe
- CMake build system with C++20 and strict compiler warnings
```

**Step 2:** Commit: `git add CHANGELOG.md && git commit -m "docs: add CHANGELOG.md with v1.0.0 entry"`

---

### Task 17: docs/DEPLOYMENT.md

**Files:**
- Create: `docs/DEPLOYMENT.md`

**Step 1:** Write the deployment guide. Content should cover:

1. **Prerequisites** — Docker 24+, Docker Compose v2, PostgreSQL 15+ (if not using bundled)
2. **Quick start with Docker Compose** — pull image, create `.env`, `docker compose up -d`
3. **Environment variables** — the 5 required env-only vars (`DNS_DB_URL`, `DNS_MASTER_KEY`/`_FILE`, `DNS_JWT_SECRET`/`_FILE`, `DNS_LOG_LEVEL`, `DNS_HTTP_PORT`) with descriptions
4. **First-run setup** — setup wizard walkthrough (create admin account, configure first provider)
5. **Database backup/restore** — `pg_dump`/`pg_restore` commands for raw DB backup (distinguished from Meridian config backup), recommended cron schedule
6. **Reverse proxy — nginx** — Example config with TLS, proxy_pass, WebSocket (if needed), recommended security headers (`Content-Security-Policy`, `Strict-Transport-Security`, `X-Robots-Tag`)
7. **Reverse proxy — Traefik** — Example docker-compose labels with TLS via Let's Encrypt
8. **Container registry** — Docker Hub (`kannasama/meridian-dns`) and GHCR pull commands with tag explanation
9. **Upgrading** — pull new image, run migrations (`meridian-dns --migrate`), restart
10. **Health checks** — `/api/v1/health/live` (liveness) and `/api/v1/health/ready` (readiness) with Kubernetes probe example YAML

The document should be comprehensive but concise (~200-300 lines). Reference existing docs where appropriate rather than duplicating.

**Step 2:** Commit: `git add docs/DEPLOYMENT.md && git commit -m "docs: add DEPLOYMENT.md with Docker, reverse proxy, and upgrade guide"`

---

### Task 18: docs/CONFIGURATION.md

**Files:**
- Create: `docs/CONFIGURATION.md`

**Step 1:** Write the configuration reference. Content:

1. **Overview** — Hybrid config model: 5 env-only vars, all others in DB
2. **Environment-only variables** — Table with variable name, required/optional, default, description. Include `_FILE` variants for secrets.
3. **Database-configurable settings** — Table organized by category (Session & Security, Deployment, Sync, Audit, Paths, Performance) with: setting key, env var that seeds it, default value, restart-required flag, description
4. **Seeding behavior** — On first run, env vars seed DB values. After that, DB takes precedence. Changes via Settings UI or API.
5. **Restart-required settings** — List of settings that need a restart (`http.threads`, `ui.dir`, `migrations.dir`, `audit.db_url`) with explanation
6. **API reference** — `GET /api/v1/settings` and `PUT /api/v1/settings` with example request/response

Reference: The settings are defined in `docs/plans/2026-03-08-v1.0-design.md` Workstream 2.

**Step 2:** Commit: `git add docs/CONFIGURATION.md && git commit -m "docs: add CONFIGURATION.md settings reference"`

---

### Task 19: docs/AUTHENTICATION.md

**Files:**
- Create: `docs/AUTHENTICATION.md`

**Step 1:** Write the authentication guide. Content:

1. **Overview** — Three auth methods: local accounts, OIDC, SAML 2.0. Plus API keys for programmatic access.
2. **Local authentication** — Account creation via setup wizard or admin, Argon2id hashing, JWT sessions with TTL configuration, forced password change
3. **OIDC provider setup** — Step-by-step with screenshots descriptions for common providers (Keycloak, Okta, Azure AD). Config fields: issuer_url, client_id, client_secret, redirect_uri, scopes, groups_claim
4. **SAML provider setup** — Config fields: entity_id, sso_url, certificate, ACS URL, name_id_format, group_attribute. Metadata exchange process.
5. **IdP group mapping** — How mapping rules work (exact match and wildcard), default group fallback, example rules JSON
6. **Claim/attribute test diagnostic** — How to use the test button to view raw claims/attributes before configuring mappings
7. **Display name extraction** — How display names are derived from IdP claims
8. **API keys** — Creation, one-time display, authentication via `X-API-Key` header, revocation
9. **Session management** — JWT structure, sliding TTL, absolute TTL, session cleanup

Reference: WS4 design in `docs/plans/2026-03-08-v1.0-design.md`.

**Step 2:** Commit: `git add docs/AUTHENTICATION.md && git commit -m "docs: add AUTHENTICATION.md with OIDC, SAML, and API key guides"`

---

### Task 20: docs/GITOPS.md

**Files:**
- Create: `docs/GITOPS.md`

**Step 1:** Write the GitOps guide. Content:

1. **Overview** — Git repositories as first-class entities, zone snapshots committed after every deployment
2. **Adding a Git repository** — UI walkthrough: name, remote URL, auth type selection
3. **SSH authentication** — SSH key generation, adding deploy key, known_hosts configuration, troubleshooting
4. **HTTPS authentication** — Personal access token setup for GitHub/GitLab/Bitbucket
5. **Branch strategies** — Repo default branch, per-zone branch override, multi-environment example (main/staging/production)
6. **Zone snapshot format** — JSON structure: `{view_name}/{zone_name}.json` with example content showing records, metadata, timestamps
7. **Auto-capture** — How baseline snapshots are created for zones never deployed through Meridian (WS7)
8. **Config backup via GitOps** — Setting `backup.git_repo_id`, backup path, scheduled vs manual backup
9. **Test connection** — Using the test button to verify authentication before saving
10. **Migration from environment variables** — How `DNS_GIT_REMOTE_URL` is auto-migrated on upgrade

Reference: WS5 and WS7 designs in `docs/plans/2026-03-08-v1.0-design.md`.

**Step 2:** Commit: `git add docs/GITOPS.md && git commit -m "docs: add GITOPS.md with multi-repo setup and branch strategies"`

---

### Task 21: docs/PERMISSIONS.md

**Files:**
- Create: `docs/PERMISSIONS.md`

**Step 1:** Write the permissions guide. Content:

1. **Overview** — Discrete permissions collected into named roles, assigned via groups with optional scope
2. **Permission strings** — Full table of all 40+ permissions organized by category (Zones, Records, Providers, Views, Variables, Git Repos, Audit, Users, Groups, Roles, Settings, Backup) — copy from WS3 design
3. **Default roles** — Admin (all permissions), Operator (everything except admin functions), Viewer (read-only + audit export) — with exact permission lists
4. **Custom roles** — How to create, permission checkbox grid, naming conventions
5. **Groups and role assignment** — How users are assigned to groups, each group membership carries a role
6. **Hierarchical scoping** — Global (no scope), view-level, zone-level scoping explained with examples
7. **Resolution logic** — How permissions are resolved for a given user + resource check, union semantics
8. **API reference** — Role CRUD endpoints, permission endpoints, group member endpoints with scope parameters
9. **Common patterns** — Example configurations: "DNS operator for production zones only", "Read-only auditor", "Full admin for one view"

Reference: WS3 design in `docs/plans/2026-03-08-v1.0-design.md`.

**Step 2:** Commit: `git add docs/PERMISSIONS.md && git commit -m "docs: add PERMISSIONS.md with roles, scoping, and resolution logic"`

---

### Task 22: docs/ARCHITECTURE.md Refresh

**Files:**
- Modify: `docs/ARCHITECTURE.md`

**Step 1:** Read the current `docs/ARCHITECTURE.md` and identify sections that need updating for WS1-7 changes. Key updates:

1. **Config model** — Document hybrid config (env-only + DB-backed), `SettingsRepository`, seeding behavior (WS2)
2. **Permission system** — Replace old three-role model description with discrete permissions, roles, scoped group assignments (WS3)
3. **Authentication** — Add OIDC and SAML auth flows, `FederatedAuthService`, `OidcService`, `SamlService`, user provisioning (WS4)
4. **GitOps** — Update from single-repo `GitOpsMirror` to multi-repo `GitRepoManager` + `GitRepoMirror` pattern (WS5)
5. **Backup/Restore** — Add `BackupService` component, export/restore flow, GitOps backup (WS6)
6. **Zone capture** — Add `DeploymentEngine::capture()`, auto-capture in sync task (WS7)
7. **Theme system** — Document named presets, theme store, `updatePreset()` mechanism (WS1)
8. **Component diagram** — Update to include new services and their relationships
9. **API endpoint table** — Ensure all endpoints are listed

**Step 2:** Make the edits. This is a large file (~86KB per CLAUDE.md) — focus on sections that reference outdated information. Don't rewrite sections that are still accurate.

**Step 3:** Commit: `git add docs/ARCHITECTURE.md && git commit -m "docs: refresh ARCHITECTURE.md for WS1-7 changes"`

---

### Task 23: README.md Rewrite

**Files:**
- Modify: `README.md`

**Step 1:** Rewrite `README.md` for a public audience. Structure:

```markdown
# Meridian DNS

[One-paragraph project description — what it does, who it's for]

[![License: AGPL-3.0](badge)](LICENSE)

## Screenshots

[3-5 screenshots from docs/screenshots/ — Dashboard, Zone records, Deployment diff,
Theme showcase, Settings]

> Note: Screenshots to be captured from a running instance and added to
> `docs/screenshots/` before release.

## Features

[Bullet list of major capabilities — expand from current README but organized
by category: Core, Security, Operations, UI]

## Quick Start

### Docker Compose (recommended)

[3-step quick start: create .env, docker compose up, open browser]

### From Source

[Build instructions — cmake, ninja, UI build]

## Documentation

[Table linking to all docs: DEPLOYMENT.md, CONFIGURATION.md, AUTHENTICATION.md,
GITOPS.md, PERMISSIONS.md, ARCHITECTURE.md, CODE_STANDARDS.md, BUILD_ENVIRONMENT.md,
OpenAPI spec]

## Tech Stack

[Updated table from current README]

## Contributing

[Brief paragraph pointing to CONTRIBUTING.md]

## License

[Dual license statement — AGPL-3.0 for open source use, commercial license available.
Links to LICENSE and COMMERCIAL-LICENSE.md]
```

**Step 2:** Remove the outdated "Pre-implementation" status notice.

**Step 3:** Commit: `git add README.md && git commit -m "docs: rewrite README.md for v1.0 public release"`

---

### Task 24: OpenAPI Spec Audit — Extract Routes

**Files:**
- Reference: All `src/api/routes/*.cpp` and `include/api/routes/*.hpp`
- Reference: `docs/openapi.yaml`

**Step 1:** Extract all registered routes from the codebase:

```bash
grep -rn 'CROW_ROUTE' src/api/routes/ --include='*.cpp' | \
  sed 's/.*CROW_ROUTE(app, "\(.*\)").*/\1/' | sort
```

Also check `src/api/StaticFileHandler.cpp` and any routes registered directly in `main.cpp`.

**Step 2:** Extract all paths from the OpenAPI spec:

```bash
grep -E '^\s+/api/v1/' docs/openapi.yaml | sed 's/://;s/^ *//' | sort
```

**Step 3:** Cross-reference the two lists. Identify:
- Routes in code but not in spec (need to add)
- Routes in spec but not in code (need to remove or mark deprecated)

Document the gap list as a checklist for the next step.

**Step 4:** Commit: (no commit for this analysis step — it informs Task 25)

---

### Task 25: OpenAPI Spec Audit — Update Spec

**Files:**
- Modify: `docs/openapi.yaml`

**Step 1:** Update `info.version` to `1.0.0` (currently `0.1.0`).

**Step 2:** For each missing endpoint identified in Task 24, add the OpenAPI path definition with:
- Correct HTTP method
- Request body schema (for POST/PUT)
- Response schemas (success + error cases)
- Authentication requirements (`bearerAuth` or `apiKeyAuth`, or empty for unauthenticated)
- Permission annotation in description (e.g., "Requires `zones.deploy` permission")

Key endpoints likely missing or needing updates:
- `GET /api/v1/health/live` — unauthenticated, returns `{status, version}`
- `GET /api/v1/health/ready` — unauthenticated, returns `{status, version, components}`
- All WS3 role endpoints (`/api/v1/roles`, `/api/v1/roles/{id}`, `/api/v1/roles/{id}/permissions`)
- All WS4 IdP endpoints (`/api/v1/identity-providers/*`, `/api/v1/auth/oidc/*`, `/api/v1/auth/saml/*`)
- All WS5 git repo endpoints (`/api/v1/git-repos/*`)
- All WS6 backup endpoints (`/api/v1/backup/*`, `/api/v1/zones/{id}/export`, `/api/v1/zones/{id}/import`)
- WS7 capture endpoint (`POST /api/v1/zones/{id}/capture`)

**Step 3:** Verify response schemas match actual JSON structures. Check 2-3 endpoints by reading the route handler code and comparing the `nlohmann::json` construction against the OpenAPI schema.

**Step 4:** Validate the spec structure:

```bash
# Install spectral if not present
npm install -g @stoplight/spectral-cli
spectral lint docs/openapi.yaml
```

Fix any structural issues reported by the linter.

**Step 5:** Commit: `git add docs/openapi.yaml && git commit -m "docs: audit and update OpenAPI spec for v1.0.0"`

---

### Task 26: Smoke Test Review — WS3 (Permissions)

**Files:**
- Review: `tests/integration/test_permission_service.cpp`
- Review: `tests/integration/test_role_repository.cpp`
- Review: `tests/integration/test_role_routes.cpp`

**Step 1:** Read the existing test files and check against the design's required coverage:

> Create custom role → assign permissions → verify access grant/deny

**Step 2:** If existing tests already cover this happy path, document that they pass and move on. If gaps exist, add the missing test cases.

**Step 3:** Run the permission tests: `build/tests/dns-tests --gtest_filter="*Permission*:*Role*"`

Expected: All pass (or skip if missing `DNS_DB_URL`).

**Step 4:** Commit if changes made: `git add -A && git commit -m "test: verify WS3 permission smoke test coverage"`

---

### Task 27: Smoke Test Review — WS4 (OIDC/SAML)

**Files:**
- Review: `tests/integration/test_federated_auth.cpp`
- Review: `tests/integration/test_idp_repository.cpp`

**Step 1:** Check coverage against: "IdP CRUD, verify discovery document fetch for OIDC (mocked)"

**Step 2:** Review and add tests if gaps exist. OIDC discovery document fetch may need a mock HTTP server or mocked response.

**Step 3:** Run: `build/tests/dns-tests --gtest_filter="*Federated*:*Idp*"`

**Step 4:** Commit if changes: `git add -A && git commit -m "test: verify WS4 OIDC/SAML smoke test coverage"`

---

### Task 28: Smoke Test Review — WS5 (Git Repos)

**Files:**
- Review: `tests/integration/test_git_repo_repository.cpp`
- Review: `tests/integration/test_git_repo_manager.cpp`

**Step 1:** Check coverage against: "Repo CRUD, test connection endpoint (against local bare repo)"

**Step 2:** Review and add if gaps. The test connection test may need a local bare git repo fixture.

**Step 3:** Run: `build/tests/dns-tests --gtest_filter="*GitRepo*"`

**Step 4:** Commit if changes: `git add -A && git commit -m "test: verify WS5 git repos smoke test coverage"`

---

### Task 29: Smoke Test Review — WS6 (Backup)

**Files:**
- Review: `tests/integration/test_backup_restore.cpp`

**Step 1:** Check coverage against: "Export → restore preview → restore apply → verify entities restored"

**Step 2:** Review and add if gaps. The round-trip test should:
1. Export system state
2. Call restore with `preview=true` and verify the preview summary
3. Call restore with `apply=true` and verify entities are restored
4. Verify credential fields are empty/placeholder after restore

**Step 3:** Run: `build/tests/dns-tests --gtest_filter="*Backup*"`

**Step 4:** Commit if changes: `git add -A && git commit -m "test: verify WS6 backup/restore smoke test coverage"`

---

### Task 30: Smoke Test Review — WS7 (Zone Capture)

**Files:**
- Review: `tests/integration/test_zone_capture.cpp`

**Step 1:** Check coverage against: "Manual capture → verify deployment record created with correct metadata"

**Step 2:** The test should verify:
1. `POST /zones/{id}/capture` creates a deployment record
2. The deployment snapshot contains `generated_by: "manual-capture"`
3. The snapshot contains records from the provider (not DB records)
4. Audit log entry is created with `operation: "zone.capture"`

**Step 3:** Run: `build/tests/dns-tests --gtest_filter="*Capture*:*ZoneCapture*"`

**Step 4:** Commit if changes: `git add -A && git commit -m "test: verify WS7 zone capture smoke test coverage"`

---

### Task 31: Build Verification

**Files:**
- None (verification only)

**Step 1:** Clean build with strict compiler flags:

```bash
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2>&1 | tee build-output.log
```

Expected: Zero warnings, zero errors.

**Step 2:** If any warnings exist, fix them. Common issues after mass-editing (license headers):
- Blank line warnings in some compilers
- Include order changes

**Step 3:** Run all unit tests:

```bash
build/tests/dns-tests --gtest_filter=-*Integration*
```

Expected: All 162 non-integration tests pass.

**Step 4:** If `DNS_DB_URL` is available, run integration tests:

```bash
DNS_DB_URL="postgresql://..." build/tests/dns-tests
```

Expected: All 285 tests pass.

**Step 5:** Run the license header check:

```bash
scripts/check-license-headers.sh
```

Expected: `OK: All source files have SPDX headers.`

**Step 6:** Commit any fixes: `git add -A && git commit -m "fix: resolve build warnings from release prep"`

---

### Task 32: Final Quality Gate

**Files:**
- None (verification only)

**Step 1:** Verify version consistency across all touchpoints:

```bash
# CMake version
grep 'VERSION' CMakeLists.txt | head -1
# Expected: VERSION 1.0.0

# Generated header
cat build/generated/common/Version.hpp | grep MERIDIAN_VERSION
# Expected: #define MERIDIAN_VERSION "1.0.0"

# OpenAPI spec version
grep 'version:' docs/openapi.yaml | head -1
# Expected: version: 1.0.0

# CLI version
build/src/meridian-dns --version
# Expected: meridian-dns 1.0.0
```

**Step 2:** Verify Docker build succeeds:

```bash
docker build -t meridian-dns:v1.0.0-test . 2>&1 | tail -20
```

Expected: Build succeeds, HEALTHCHECK uses `/api/v1/health/live`.

**Step 3:** Run a quick sanity check of the Docker image:

```bash
docker run --rm meridian-dns:v1.0.0-test --version
# Expected: meridian-dns 1.0.0
```

**Step 4:** Verify all documentation files exist:

```bash
for f in LICENSE COMMERCIAL-LICENSE.md CLA.md CONTRIBUTING.md SECURITY.md CHANGELOG.md \
  docs/DEPLOYMENT.md docs/CONFIGURATION.md docs/AUTHENTICATION.md docs/GITOPS.md \
  docs/PERMISSIONS.md; do
  [ -f "$f" ] && echo "OK: $f" || echo "MISSING: $f"
done
```

Expected: All files exist.

**Step 5:** Create a `docs/screenshots/` directory placeholder:

```bash
mkdir -p docs/screenshots
echo "Screenshots will be captured from a running themed instance before release." > docs/screenshots/README.md
```

**Step 6:** Final commit: `git add -A && git commit -m "chore: complete v1.0.0 release preparation"`

---

## Post-Plan Notes

### Screenshots (Manual Step)

Screenshots must be captured manually from a running instance with a dark theme preset (e.g., Catppuccin Mocha). Store in `docs/screenshots/`:

| Screenshot | Content |
|-----------|---------|
| `dashboard.png` | Dashboard with zone summary cards and provider health |
| `zone-records.png` | DataTable with record management, variable highlights |
| `deployment-diff.png` | Side-by-side diff preview before deploy |
| `theme-showcase.png` | Dark theme demonstrating the visual system |
| `settings.png` | Card-based settings layout |

Target: <500KB each, reasonably compressed PNG. Reference from `README.md` with relative paths.

### Future Improvements

1. **Crow SecurityHeadersMiddleware** — Refactor from `applySecurityHeaders()` function to a proper Crow middleware struct. This requires changing `crow::SimpleApp` to `crow::Crow<SecurityHeadersMiddleware>` across ~42 file references. Deferred from v1.0 to avoid mechanical churn during release prep.
2. **Provider health caching** — Add a maintenance task that periodically checks provider connectivity and caches results in the DB. The `/health/ready` endpoint would then report actual provider health instead of just counting providers.
3. **CI/CD implementation** — The pipeline is designed in `docs/internal/RELEASE.md` but not implemented. GitHub Actions or GitLab CI configuration would be a follow-up.
4. **Automated CLA bot** — Currently CLA enforcement is manual (PR review checklist). A GitHub bot (e.g., CLA Assistant) could automate this.