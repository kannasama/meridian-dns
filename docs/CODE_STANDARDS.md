# Code Standards — Meridian DNS

> **Canonical reference** for naming conventions, formatting, error handling, and ownership rules.
> Originally defined in the [Project Framing Design](plans/2026-02-28-project-framing-design.md) §5.

---

## Table of Contents

1. [Naming Conventions](#1-naming-conventions)
2. [Hungarian Notation Prefix Table](#2-hungarian-notation-prefix-table)
3. [Class Abbreviations](#3-class-abbreviations)
4. [Formatting and Style](#4-formatting-and-style)
5. [Error Handling](#5-error-handling)
6. [Ownership and Pointers](#6-ownership-and-pointers)
7. [Thread Safety](#7-thread-safety)
8. [SQL and Database](#8-sql-and-database)

---

## 1. Naming Conventions

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

---

## 2. Hungarian Notation Prefix Table

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

---

## 3. Class Abbreviations

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

---

## 4. Formatting and Style

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

---

## 5. Error Handling

- Business errors: throw from the `AppError` hierarchy (see [ARCHITECTURE.md](ARCHITECTURE.md) §9.1)
- Never catch and swallow silently — log at minimum
- No exceptions in constructors that acquire external resources — use factory functions or `init()` methods
- All error responses follow the JSON shape defined in [ARCHITECTURE.md](ARCHITECTURE.md) §9.2

---

## 6. Ownership and Pointers

- `std::unique_ptr` for exclusive ownership (providers, repositories)
- `std::shared_ptr` only when shared ownership is genuinely needed (connection pool internals)
- Raw pointers only for non-owning references; prefer references (`&`) where possible
- No `new`/`delete` anywhere in application code

---

## 7. Thread Safety

Functions that use static internal buffers are **banned** in this project. All code runs in a
multi-threaded context (Crow worker threads, ThreadPool, MaintenanceScheduler).

### Banned Functions and Replacements

| Banned | Replacement | Header |
|--------|------------|--------|
| `std::gmtime()` | `gmtime_r()` via `common/TimeUtils.hpp` | `#include "common/TimeUtils.hpp"` |
| `std::localtime()` | `localtime_r()` | POSIX `<time.h>` |
| `std::asctime()` | `strftime()` with `gmtime_r()` result | POSIX `<time.h>` |
| `std::ctime()` | `strftime()` with `localtime_r()` result | POSIX `<time.h>` |
| `std::strtok()` | Manual `find()`/`substr()` or `std::string_view` | N/A |
| `getenv()` in hot paths | Cache at startup | N/A |

### Standard pattern for timestamps

```cpp
#include "common/TimeUtils.hpp"

// Preferred -- use the project utility
std::string sTs = dns::common::nowIso8601();

// If custom format is needed -- use gmtime_r directly
std::tm tmBuf{};
gmtime_r(&tTime, &tmBuf);
std::ostringstream oss;
oss << std::put_time(&tmBuf, "%Y-%m-%dT%H:%M:%SZ");
```

> **History:** `std::gmtime()` was caught twice in code review (CR-CRYPTO-01 in v1.0,
> CR2-SEC-01 in v1.1). This standard exists to prevent recurrence.

---

## 8. SQL and Database

### Parameterized Queries

All SQL queries MUST use libpqxx parameterized queries (`pqxx::params{...}`) with `$N`
placeholders. Never interpolate user input into SQL strings.

### Array Construction

When building PostgreSQL array literals for use in SQL, use `txn.quote()` for each element.
**Do not manually escape** quotes or backslashes.

**Correct:**

```cpp
std::string sArr = "ARRAY[";
for (size_t i = 0; i < vItems.size(); ++i) {
  if (i > 0) sArr += ",";
  sArr += txn.quote(vItems[i]);
}
sArr += "]";
```

**Incorrect (banned):**

```cpp
// Do NOT do this -- fragile manual escaping
for (char c : sItem) {
  if (c == '"' || c == '\\') sEscaped += '\\';
  sEscaped += c;
}
```

> **History:** Manual array escaping in `TagRepository` was flagged in CR2-SEC-05 (v1.1).
> Use `txn.quote()` consistently, as `ZoneRepository::updateTags()` does.
