# Project Framing Design — Meridian DNS

> **Date:** 2026-02-28
> **Status:** Draft
> **Scope:** Project skeleton + foundation layer for the server-side application
> **Prerequisite:** [ARCHITECTURE.md](../ARCHITECTURE.md), [SECURITY_PLAN.md](SECURITY_PLAN.md)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Key Decisions](#2-key-decisions)
3. [Phase 1: Project Skeleton](#3-phase-1-project-skeleton)
  - 3.1 [Build System](#31-build-system)
  - 3.2 [Dependencies](#32-dependencies)
  - 3.3 [Directory Layout](#33-directory-layout)
  - 3.4 [Header and Source Stubs](#34-header-and-source-stubs)
4. [Phase 2: Foundation Layer](#4-phase-2-foundation-layer)
  - 4.1 [Component Dependency Order](#41-component-dependency-order)
  - 4.2 [Component Specifications](#42-component-specifications)
5. [Code Standards](#5-code-standards)
  - 5.1 [Naming Conventions](#51-naming-conventions)
  - 5.2 [Hungarian Notation Prefix Table](#52-hungarian-notation-prefix-table)
  - 5.3 [Class Abbreviations](#53-class-abbreviations)
  - 5.4 [Formatting and Style](#54-formatting-and-style)
  - 5.5 [Error Handling](#55-error-handling)
  - 5.6 [Ownership and Pointers](#56-ownership-and-pointers)
6. [Testing Strategy](#6-testing-strategy)
  - 6.1 [Framework](#61-framework)
  - 6.2 [Test Structure](#62-test-structure)
  - 6.3 [Foundation Layer Tests](#63-foundation-layer-tests)
  - 6.4 [Running Tests](#64-running-tests)
7. [Documentation Updates](#7-documentation-updates)
8. [Implementation Plan](#8-implementation-plan)

---

## 1. Overview

This document defines the two-phase approach to establishing the meridian-dns codebase:

- **Phase 1 — Project Skeleton:** CMakeLists.txt, directory structure, empty header/source stubs with class declarations, and a compilable (but non-functional) binary.
- **Phase 2 — Foundation Layer:** Implement the lowest-layer components (error types, config, logging, connection pool, crypto, JWT) so the binary can start, load configuration, connect to PostgreSQL, and pass tests.

**Scope exclusion:** The TUI client is being extracted into a separate repository with its own design document. This project produces a single server binary (`meridian-dns`) that exposes the REST API. All TUI-related code (FTXUI, screens, components, `ApiKeyConfig`) is excluded from this repository.

---

## 2. Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Testing framework | Google Test + Google Mock via FetchContent | Best mocking support for the project's many interfaces (`IProvider`, `IJwtSigner`); version-pinned via FetchContent for reproducibility |
| Logging library | spdlog | De facto C++ structured logging; supports JSON output, configurable levels; available in Arch repos |
| TUI scope | Separate repository | TUI communicates exclusively via REST API; no shared code with server beyond the API contract |
| Build targets | `meridian-core` (static lib) + `meridian-dns` (exe) + `dns-tests` (exe) | Core logic is testable independently of main(); single binary output |
| CMake minimum | 3.20 | Required for modern FetchContent, C++20 support |
| C++ standard | C++20 | Required for `std::jthread`, `std::format`, `std::filesystem` |
| Naming convention | Hungarian notation variant | Type-prefixed variables, leading underscore for members, PascalCase classes |

---

## 3. Phase 1: Project Skeleton

### 3.1 Build System

Three CMake targets:

| Target | Type | Contents |
|--------|------|----------|
| `meridian-core` | `STATIC` library | All `.cpp` files under `src/` except `main.cpp` |
| `meridian-dns` | Executable | `src/main.cpp` → links `meridian-core` |
| `dns-tests` | Executable | All files under `tests/` → links `meridian-core` + GTest/GMock |

**CMake file structure:**

- `CMakeLists.txt` — Root: project definition, C++20 standard, `find_package()` calls, `add_subdirectory()`
- `src/CMakeLists.txt` — Defines `meridian-core` library and `meridian-dns` executable
- `tests/CMakeLists.txt` — FetchContent for GTest, defines `dns-tests` executable

### 3.2 Dependencies

All external dependencies use system packages via `find_package()` or `pkg_check_modules()`, except GTest which uses `FetchContent`.

| Library | CMake Discovery | Arch Package | Purpose |
|---------|----------------|--------------|---------|
| libpqxx | `find_package(libpqxx)` | `libpqxx` | PostgreSQL C++ client |
| OpenSSL | `find_package(OpenSSL)` | `openssl` | AES-256-GCM, SHA-512, HMAC, Argon2id |
| libgit2 | `pkg_check_modules(LIBGIT2 libgit2)` | `libgit2` | GitOps mirror |
| Crow | `FetchContent_MakeAvailable(Crow)` | N/A (fetched at configure time) | HTTP server |
| nlohmann_json | `find_package(nlohmann_json)` | `nlohmann-json` | JSON serialization |
| spdlog | `find_package(spdlog)` | `spdlog` | Structured logging |
| GTest/GMock | `FetchContent` (pinned release tag) | N/A | Testing framework |

### 3.3 Directory Layout

Server-only layout (TUI directories removed from ARCHITECTURE.md §10):

```
meridian-dns/
├── CMakeLists.txt
├── .clang-format
├── .gitignore
├── include/
│   ├── common/
│   │   ├── Errors.hpp
│   │   ├── Logger.hpp
│   │   ├── Config.hpp
│   │   └── Types.hpp
│   ├── api/
│   │   ├── ApiServer.hpp
│   │   ├── AuthMiddleware.hpp
│   │   └── routes/
│   │       ├── AuthRoutes.hpp
│   │       ├── ProviderRoutes.hpp
│   │       ├── ViewRoutes.hpp
│   │       ├── ZoneRoutes.hpp
│   │       ├── RecordRoutes.hpp
│   │       ├── VariableRoutes.hpp
│   │       ├── DeploymentRoutes.hpp
│   │       ├── AuditRoutes.hpp
│   │       └── HealthRoutes.hpp
│   ├── core/
│   │   ├── VariableEngine.hpp
│   │   ├── DiffEngine.hpp
│   │   ├── DeploymentEngine.hpp
│   │   ├── RollbackEngine.hpp
│   │   ├── MaintenanceScheduler.hpp
│   │   └── ThreadPool.hpp
│   ├── providers/
│   │   ├── IProvider.hpp
│   │   ├── ProviderFactory.hpp
│   │   ├── PowerDnsProvider.hpp
│   │   ├── CloudflareProvider.hpp
│   │   └── DigitalOceanProvider.hpp
│   ├── dal/
│   │   ├── ConnectionPool.hpp
│   │   ├── ProviderRepository.hpp
│   │   ├── ViewRepository.hpp
│   │   ├── ZoneRepository.hpp
│   │   ├── RecordRepository.hpp
│   │   ├── VariableRepository.hpp
│   │   ├── DeploymentRepository.hpp
│   │   ├── AuditRepository.hpp
│   │   ├── UserRepository.hpp
│   │   ├── SessionRepository.hpp
│   │   └── ApiKeyRepository.hpp
│   ├── gitops/
│   │   └── GitOpsMirror.hpp
│   └── security/
│       ├── CryptoService.hpp
│       ├── AuthService.hpp
│       ├── IJwtSigner.hpp
│       ├── HmacJwtSigner.hpp
│       └── SamlReplayCache.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── common/
│   │   ├── Logger.cpp
│   │   └── Config.cpp
│   ├── api/
│   │   ├── ApiServer.cpp
│   │   ├── AuthMiddleware.cpp
│   │   └── routes/
│   │       ├── AuthRoutes.cpp
│   │       ├── ProviderRoutes.cpp
│   │       ├── ViewRoutes.cpp
│   │       ├── ZoneRoutes.cpp
│   │       ├── RecordRoutes.cpp
│   │       ├── VariableRoutes.cpp
│   │       ├── DeploymentRoutes.cpp
│   │       ├── AuditRoutes.cpp
│   │       └── HealthRoutes.cpp
│   ├── core/
│   │   ├── VariableEngine.cpp
│   │   ├── DiffEngine.cpp
│   │   ├── DeploymentEngine.cpp
│   │   ├── RollbackEngine.cpp
│   │   ├── MaintenanceScheduler.cpp
│   │   └── ThreadPool.cpp
│   ├── providers/
│   │   ├── ProviderFactory.cpp
│   │   ├── PowerDnsProvider.cpp
│   │   ├── CloudflareProvider.cpp
│   │   └── DigitalOceanProvider.cpp
│   ├── dal/
│   │   ├── ConnectionPool.cpp
│   │   ├── ProviderRepository.cpp
│   │   ├── ViewRepository.cpp
│   │   ├── ZoneRepository.cpp
│   │   ├── RecordRepository.cpp
│   │   ├── VariableRepository.cpp
│   │   ├── DeploymentRepository.cpp
│   │   ├── AuditRepository.cpp
│   │   ├── UserRepository.cpp
│   │   ├── SessionRepository.cpp
│   │   └── ApiKeyRepository.cpp
│   ├── gitops/
│   │   └── GitOpsMirror.cpp
│   └── security/
│       ├── CryptoService.cpp
│       ├── AuthService.cpp
│       ├── HmacJwtSigner.cpp
│       └── SamlReplayCache.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_errors.cpp
│   │   ├── test_config.cpp
│   │   ├── test_crypto_service.cpp
│   │   ├── test_hmac_jwt_signer.cpp
│   │   ├── test_variable_engine.cpp
│   │   └── test_provider_factory.cpp
│   └── integration/
│       ├── test_connection_pool.cpp
│       └── test_deployment_pipeline.cpp
├── scripts/
│   ├── db/
│   │   ├── 001_initial_schema.sql
│   │   └── 002_add_indexes.sql
│   └── docker/
│       └── entrypoint.sh
├── docs/
│   ├── BUILD_ENVIRONMENT.md
│   ├── CODE_STANDARDS.md
│   └── plans/
└── plans/
```

### 3.4 Header and Source Stubs

Each header file contains:

- `#pragma once`
- Namespace declaration (`namespace dns::<layer>`)
- Class declaration with public method signatures (matching ARCHITECTURE.md interfaces)
- No implementation bodies

Each `.cpp` file contains:

- Include of corresponding header
- Stub implementations that throw `std::runtime_error{"not implemented"}` or return default values
- Enough to compile and link successfully

**Exception:** `IProvider.hpp` is a pure abstract interface with `= 0` methods and no `.cpp` file.

---

## 4. Phase 2: Foundation Layer

### 4.1 Component Dependency Order

Components are implemented bottom-up. Each only depends on those listed before it.

```mermaid
graph TD
  A[common/Errors.hpp — header-only] --> C[common/Logger]
  B[common/Types.hpp — header-only] --> C
  C --> D[common/Config]
  D --> E[dal/ConnectionPool]
  D --> F[security/CryptoService]
  F --> G[security/IJwtSigner + HmacJwtSigner]
  E --> H[main.cpp startup: steps 1-5]
  F --> H
  G --> H
```

### 4.2 Component Specifications

#### 4.2.1 common/Errors.hpp (header-only)

Implements the error hierarchy from ARCHITECTURE.md §9.1:

```cpp
#pragma once
#include <stdexcept>
#include <string>

namespace dns::common {

struct AppError : public std::runtime_error {
  int _iHttpStatus;
  std::string _sErrorCode;
  explicit AppError(int iHttpStatus, std::string sCode, std::string sMsg);
};

struct ValidationError     : AppError { /* 400 */ };
struct AuthenticationError : AppError { /* 401 */ };
struct AuthorizationError  : AppError { /* 403 */ };
struct NotFoundError       : AppError { /* 404 */ };
struct ConflictError       : AppError { /* 409 */ };
struct ProviderError       : AppError { /* 502 */ };
struct UnresolvedVariableError : AppError { /* 422 */ };
struct DeploymentLockedError   : AppError { /* 409 */ };
struct GitMirrorError      : AppError { /* 500, non-fatal */ };

} // namespace dns::common
```

#### 4.2.2 common/Types.hpp (header-only)

Shared value types from ARCHITECTURE.md §4.2-4.3:

- `DnsRecord`, `PushResult`, `HealthStatus` (enum)
- `DiffAction` (enum), `RecordDiff`, `PreviewResult`
- `RequestContext` (from §4.1)

#### 4.2.3 common/Logger.hpp/.cpp

Thin wrapper over spdlog:

- `dns::common::Logger::init(const std::string& sLevel)` — sets global log level from `DNS_LOG_LEVEL`
- `dns::common::Logger::get()` — returns the shared `spdlog::logger` instance
- Supports structured key-value pairs via spdlog's formatting
- JSON output mode togglable (future: via `DNS_LOG_FORMAT=json`)

#### 4.2.4 common/Config.hpp/.cpp

Environment variable loader implementing ARCHITECTURE.md §8:

- Loads all env vars into a typed struct
- Implements `_FILE` fallback pattern for `DNS_MASTER_KEY` and `DNS_JWT_SECRET` (SEC-02)
- Validates required variables at startup; throws fatal error on missing
- Validates constraints:
  - `DNS_DEPLOYMENT_RETENTION_COUNT >= 1`
  - `DNS_SESSION_ABSOLUTE_TTL_SECONDS >= DNS_JWT_TTL_SECONDS`
- Calls `OPENSSL_cleanse()` on raw secret strings after handoff

#### 4.2.5 dal/ConnectionPool.hpp/.cpp

Fixed-size connection pool:

- Pool of `pqxx::connection` objects sized by `DNS_DB_POOL_SIZE`
- RAII `ConnectionGuard` for checkout/return
- Thread-safe via `std::mutex` + `std::condition_variable`
- Blocks on exhaustion (with configurable timeout)
- Validates connections with a lightweight query on checkout

#### 4.2.6 security/CryptoService.hpp/.cpp

Cryptographic operations (ARCHITECTURE.md §4.6.1):

- `encrypt(sPlaintext)` / `decrypt(sCiphertext)` — AES-256-GCM with per-operation 12-byte IV
- Storage format: `base64(iv):base64(ciphertext + tag)`
- `generateApiKey()` — 32 random bytes via `RAND_bytes()` → base64url (43 chars)
- `hashApiKey(sRawKey)` — SHA-512 via OpenSSL `EVP_sha512()` → hex string (128 chars)
- Master key loaded from Config, zeroed after initialization

#### 4.2.7 security/IJwtSigner.hpp + HmacJwtSigner.hpp/.cpp

JWT signing abstraction (ARCHITECTURE.md §4.6.2):

- `IJwtSigner` — pure interface: `sign(jPayload)` → string, `verify(sToken)` → json
- `HmacJwtSigner` — HS256 implementation using OpenSSL HMAC
- JWT structure: header.payload.signature (base64url-encoded)
- Payload fields: `sub`, `username`, `role`, `auth_method`, `iat`, `exp`
- `verify()` throws `AuthenticationError` on invalid signature, expiry, or malformed token

#### 4.2.8 main.cpp — Startup Sequence (Steps 1–5)

Implements ARCHITECTURE.md §11.4 steps 1 through 5:

```
1. Load Config (validate required env vars, _FILE fallback, OPENSSL_cleanse)
2. Initialize CryptoService with DNS_MASTER_KEY
3. Construct IJwtSigner based on DNS_JWT_ALGORITHM (default: HmacJwtSigner/HS256)
4. Initialize ConnectionPool (DNS_DB_URL, DNS_DB_POOL_SIZE)
5. Log "foundation ready" — remaining startup steps are stubs
```

Steps 6–12 (GitOps, ThreadPool, MaintenanceScheduler, routes, Crow HTTP server) remain as stubs that log "not yet implemented" and are built in subsequent phases.

---

## 5. Code Standards

### 5.1 Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes / structs / enums | PascalCase, no prefix | `VariableEngine`, `DiffAction` |
| Class/struct instance variables | Class abbreviation (≤3 chars) + PascalCase | `veEngine`, `cpPool` |
| Primitives / containers / pointers | Type prefix + PascalCase | `iZoneId`, `sName`, `vRecords` |
| Class member variables | Leading underscore + prefix | `_sUsername`, `_iPoolSize` |
| Member functions | camelCase | `expand()`, `listRecords()` |
| Function parameters | Type prefix + PascalCase (same as local vars) | `iZoneId`, `sRawKey` |
| Constants / enum values | PascalCase | `HealthStatus::Degraded` |
| Namespaces | lowercase | `dns::core` |
| Files | PascalCase matching class | `VariableEngine.hpp` |

### 5.2 Hungarian Notation Prefix Table

| Type | Prefix | Example |
|------|--------|---------|
| `int`, `int32_t` | `i` | `iZoneId` |
| `int64_t` | `i` | `iDeploymentSeq` |
| `uint32_t` | `u` | `uTtl` |
| `size_t` | `n` | `nPoolSize` |
| `bool` | `b` | `bPurgeDrift` |
| `std::string` | `s` | `sZoneName` |
| `double` / `float` | `f` | `fTimeout` |
| `char` | `c` | `cDelimiter` |
| `std::vector<T>` | `v` | `vDiffs`, `vRecords` |
| `std::map` / `std::unordered_map` | `m` | `mZoneMutexes` |
| `std::set` / `std::unordered_set` | `st` | `stSeenIds` |
| `std::optional<T>` | `o` | `oExpiresAt` |
| `std::function` | `fn` | `fnTask` |
| `std::unique_ptr<T>` | `up` | `upProvider` |
| `std::shared_ptr<T>` | `sp` | `spConnection` |
| Raw pointer (`T*`) | `p` | `pContext` |
| `std::mutex` | `mtx` | `mtxZone` |
| `std::chrono::time_point` | `tp` | `tpCreatedAt` |
| `std::chrono::seconds` / duration | `dur` | `durInterval` |
| `nlohmann::json` | `j` | `jPayload` |

### 5.3 Class Abbreviations

Defined per class, documented in each header file.

| Class | Abbreviation | Usage Example |
|-------|-------------|---------------|
| `VariableEngine` | `ve` | `veEngine` |
| `DiffEngine` | `de` | `deEngine` |
| `DeploymentEngine` | `dep` | `depEngine` |
| `RollbackEngine` | `re` | `reEngine` |
| `ConnectionPool` | `cp` | `cpPool` |
| `ConnectionGuard` | `cg` | `cgConn` |
| `CryptoService` | `cs` | `csService` |
| `AuthService` | `as` | `asAuth` |
| `Config` | `cfg` | `cfgApp` |
| `GitOpsMirror` | `gm` | `gmMirror` |
| `ApiServer` | `api` | `apiServer` |
| `ThreadPool` | `tp` | `tpPool` |
| `MaintenanceScheduler` | `ms` | `msScheduler` |
| `PreviewResult` | `pr` | `prResult` |
| `RecordDiff` | `rd` | `rdDiff` |
| `DnsRecord` | `dr` | `drRecord` |
| `PushResult` | `prs` | `prsResult` |
| `RequestContext` | `rc` | `rcContext` |

New class abbreviations are assigned when the class is created and documented in the header.

### 5.4 Formatting and Style

Enforced via `.clang-format` in the repository root:

- **Indent:** 2 spaces
- **Column limit:** 100
- **Base style:** Google (with overrides)
- **Braces:** Attach (K&R style)
- **Header guards:** `#pragma once`
- **Include order** (separated by blank lines):
  1. Corresponding header (in `.cpp` files)
  2. Project headers (`#include "common/..."`)
  3. Third-party headers (`#include <spdlog/spdlog.h>`)
  4. Standard library headers (`#include <string>`)
- **const correctness:** Member functions that do not mutate state are `const`. Pass by `const&` unless consuming. Mark variables `const` wherever possible.
- **`noexcept`:** On destructors and move operations.

### 5.5 Error Handling

- Business errors: throw from the `AppError` hierarchy (§9.1 of ARCHITECTURE.md)
- Never catch and swallow silently — log at minimum
- No exceptions in constructors that acquire external resources — use factory functions or `init()` methods
- All error responses follow the JSON shape defined in ARCHITECTURE.md §9.2

### 5.6 Ownership and Pointers

- `std::unique_ptr` for exclusive ownership (providers, repositories)
- `std::shared_ptr` only when shared ownership is genuinely needed (connection pool internals)
- Raw pointers only for non-owning references; prefer references (`&`) where possible
- No `new`/`delete` anywhere in application code

---

## 6. Testing Strategy

### 6.1 Framework

**Google Test + Google Mock**, pulled via CMake `FetchContent` (pinned to a stable release tag).

**Test naming:** Files: `test_<component_snake_case>.cpp`. Cases: `TEST(ClassName, descriptiveBehavior)`.

### 6.2 Test Structure

```
tests/
├── CMakeLists.txt
├── unit/                            # No external dependencies
│   ├── test_errors.cpp
│   ├── test_config.cpp
│   ├── test_crypto_service.cpp
│   ├── test_hmac_jwt_signer.cpp
│   ├── test_variable_engine.cpp     # Placeholder
│   └── test_provider_factory.cpp    # Placeholder
└── integration/                     # Requires live PostgreSQL
    ├── test_connection_pool.cpp
    └── test_deployment_pipeline.cpp # Placeholder
```

### 6.3 Foundation Layer Tests

**Unit tests (no external dependencies):**

| Test File | Validates |
|-----------|-----------|
| `test_errors.cpp` | Each error type carries correct `_iHttpStatus` and `_sErrorCode`; `catch(AppError&)` works polymorphically |
| `test_config.cpp` | Reads env vars; falls back to `_FILE`; validates required vars throw on absence; `DNS_DEPLOYMENT_RETENTION_COUNT >= 1`; `DNS_SESSION_ABSOLUTE_TTL >= DNS_JWT_TTL` |
| `test_crypto_service.cpp` | `encrypt()` → `decrypt()` roundtrip; different IVs per call; `generateApiKey()` → 43-char base64url; `hashApiKey()` → 128-char hex SHA-512 |
| `test_hmac_jwt_signer.cpp` | `sign()` → `verify()` roundtrip; expired token throws `AuthenticationError`; tampered token throws; payload fields preserved |

**Integration tests (require live PostgreSQL):**

| Test File | Validates |
|-----------|-----------|
| `test_connection_pool.cpp` | Pool creates `DNS_DB_POOL_SIZE` connections; `ConnectionGuard` RAII releases on scope exit; exhausted pool blocks; `SELECT 1` succeeds on each connection |

**Integration test gating:** Tests are labeled `Integration` via CTest labels. They only run when `DNS_DB_URL` is set in the environment. Default `ctest` invocation skips them.

### 6.4 Running Tests

```bash
# Unit tests only (default)
ctest --test-dir build --output-on-failure

# Include integration tests
DNS_DB_URL="postgresql://dns:dns@localhost:5432/meridian_dns" \
  ctest --test-dir build --output-on-failure -L Integration
```

> **Future enhancement:** For public release or CI, add a `docker-compose.test.yml` that spins up a disposable PostgreSQL container, runs migrations, and executes integration tests in a self-contained environment.

---

## 7. Documentation Updates

The following changes are required to existing documentation as part of this work:

| Document | Change |
|----------|--------|
| `ARCHITECTURE.md` §4.7 | Replace inline TUI content with a summary paragraph and link to external TUI design document |
| `ARCHITECTURE.md` §10 | Remove `tui/` directories from the file structure listing |
| `BUILD_ENVIRONMENT.md` | Add `spdlog` to the dependency table (official repo); note GTest is pulled via FetchContent |
| New: `docs/CODE_STANDARDS.md` | Full code standards reference (§5 of this document) |
| New: `docs/TUI_DESIGN.md` | Stub design document for the TUI client (separate repository) |

---

## 8. Implementation Plan

Tasks are listed in execution order. Each is independently completable.

### Phase 1: Project Skeleton

1. Create root `CMakeLists.txt` with project definition, C++20, dependency discovery
2. Create `src/CMakeLists.txt` with `meridian-core` library and `meridian-dns` executable targets
3. Create `tests/CMakeLists.txt` with FetchContent for GTest and `dns-tests` target
4. Create `.clang-format` with project formatting rules
5. Create all directories under `include/` and `src/`
6. Create all header stubs with class declarations, namespaces, and Hungarian notation
7. Create all `.cpp` stubs with minimal implementations
8. Create `src/main.cpp` with startup sequence skeleton
9. Create `scripts/db/001_initial_schema.sql` and `002_add_indexes.sql` from ARCHITECTURE.md §5
10. Create `scripts/docker/entrypoint.sh` from ARCHITECTURE.md §11.2
11. Verify the project compiles and links successfully

### Phase 2: Foundation Layer

12. Implement `common/Errors.hpp` (header-only error hierarchy)
13. Implement `common/Types.hpp` (header-only shared types)
14. Implement `common/Logger.hpp/.cpp` (spdlog wrapper)
15. Implement `common/Config.hpp/.cpp` (env var loader with `_FILE` support)
16. Implement `dal/ConnectionPool.hpp/.cpp` (libpqxx pool with RAII guard)
17. Implement `security/CryptoService.hpp/.cpp` (AES-256-GCM + SHA-512)
18. Implement `security/IJwtSigner.hpp` + `security/HmacJwtSigner.hpp/.cpp` (HS256)
19. Implement `main.cpp` startup sequence steps 1–5

### Phase 3: Foundation Tests

20. Write `tests/unit/test_errors.cpp`
21. Write `tests/unit/test_config.cpp`
22. Write `tests/unit/test_crypto_service.cpp`
23. Write `tests/unit/test_hmac_jwt_signer.cpp`
24. Write `tests/integration/test_connection_pool.cpp`
25. Verify all tests pass

### Phase 4: Documentation Updates

26. Create `docs/CODE_STANDARDS.md`
27. Update `ARCHITECTURE.md` §4.7 — TUI extraction
28. Update `ARCHITECTURE.md` §10 — remove TUI directories
29. Update `docs/BUILD_ENVIRONMENT.md` — add spdlog, note GTest FetchContent
30. Create `docs/TUI_DESIGN.md` — stub for external TUI design
