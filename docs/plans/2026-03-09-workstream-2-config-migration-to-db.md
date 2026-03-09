# Workstream 2: Configuration Migration to Database — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Move runtime configuration from environment variables into a database-backed `system_config` table. Environment variables seed initial values on first run; DB values take precedence after that. Admin UI for managing settings.

**Architecture:** A `SettingsRepository` reads/writes the `system_config` table. On startup, for each DB-configurable setting, if no row exists in `system_config`, the value is seeded from the env var (if set) or the compiled default. The `Config` struct retains the 5 env-only vars; all others are loaded from DB after seeding. A new `SettingsRoutes` class exposes `GET/PUT /api/v1/settings` (admin-only). The UI adds a Settings page with card-based sections.

**Tech Stack:** C++20, PostgreSQL (libpqxx), Crow HTTP, Google Test, Vue 3 + TypeScript + PrimeVue

---

## Task Overview

| # | Task | Description |
|---|------|-------------|
| 1 | Schema migration v007 | Add `description` and `updated_at` columns to `system_config` |
| 2 | SettingsRepository | DAL class for CRUD on `system_config` table |
| 3 | SettingsRepository tests | Unit + integration tests for the repository |
| 4 | Config refactor — env-only vars | Slim down `Config::load()` to only load the 5 env-only vars |
| 5 | Config seeding from DB | Startup logic: seed `system_config` from env vars, then populate `Config` from DB |
| 6 | Config seeding tests | Integration tests for seeding behavior |
| 7 | SettingsRoutes — GET | `GET /api/v1/settings` endpoint returning all settings |
| 8 | SettingsRoutes — PUT | `PUT /api/v1/settings` endpoint for updating settings |
| 9 | SettingsRoutes tests | Integration tests for the settings API |
| 10 | UI — settings API client | TypeScript API module for settings endpoints |
| 11 | UI — SettingsView page | Admin-only settings page with card-based sections |
| 12 | UI — routing + sidebar | Add settings route and sidebar navigation entry |
| 13 | MaintenanceScheduler dynamic reload | Hot-reload maintenance intervals when settings change |
| 14 | Full verification pass | Build, test, manual QA |

---

### Task 1: Schema Migration v007

**Files:**
- Create: `scripts/db/v007/001_extend_system_config.sql`

The `system_config` table already exists (bootstrapped in `MigrationRunner::bootstrap()` as `key TEXT PRIMARY KEY, value TEXT NOT NULL`). This migration adds optional metadata columns.

**Step 1: Create the migration directory and SQL file**

Create `scripts/db/v007/001_extend_system_config.sql`:

```sql
-- Workstream 2: Extend system_config for DB-backed settings
ALTER TABLE system_config ADD COLUMN IF NOT EXISTS description TEXT;
ALTER TABLE system_config ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ DEFAULT now();
```

**Step 2: Verify the migration file exists**

Run: `ls -la scripts/db/v007/`
Expected: Shows `001_extend_system_config.sql`.

**Step 3: Commit**

```bash
git add scripts/db/v007/
git commit -m "feat(db): add v007 migration extending system_config with description and updated_at"
```

---

### Task 2: SettingsRepository

**Files:**
- Create: `include/dal/SettingsRepository.hpp`
- Create: `src/dal/SettingsRepository.cpp`

This repository manages the `system_config` table. It provides typed access to individual settings and a bulk get/set API.

**Step 1: Create the header**

Create `include/dal/SettingsRepository.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Row type returned from system_config queries.
struct SettingRow {
  std::string sKey;
  std::string sValue;
  std::string sDescription;
  std::string sUpdatedAt;  // ISO 8601 timestamp string
};

/// Manages the system_config table for DB-backed settings.
/// Class abbreviation: sr (settings repo)
class SettingsRepository {
 public:
  explicit SettingsRepository(ConnectionPool& cpPool);
  ~SettingsRepository();

  /// Get a single setting by key. Returns nullopt if not found.
  std::optional<SettingRow> findByKey(const std::string& sKey);

  /// Get the value for a key, or return sDefault if not found.
  std::string getValue(const std::string& sKey, const std::string& sDefault = "");

  /// Get value as int, or return iDefault if not found or unparseable.
  int getInt(const std::string& sKey, int iDefault);

  /// Get value as bool (true/false/1/0), or return bDefault if not found.
  bool getBool(const std::string& sKey, bool bDefault);

  /// List all settings.
  std::vector<SettingRow> listAll();

  /// Insert or update a setting. Sets updated_at to now().
  void upsert(const std::string& sKey, const std::string& sValue,
              const std::string& sDescription = "");

  /// Insert a setting only if the key does not exist (seed behavior).
  /// Returns true if inserted, false if already existed.
  bool seedIfMissing(const std::string& sKey, const std::string& sValue,
                     const std::string& sDescription = "");

  /// Delete a setting by key.
  void deleteByKey(const std::string& sKey);

 private:
  ConnectionPool& _cpPool;

  SettingRow mapRow(const auto& row) const;
};

}  // namespace dns::dal
```

**Step 2: Create the implementation**

Create `src/dal/SettingsRepository.cpp`:

```cpp
#include "dal/SettingsRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

SettingsRepository::SettingsRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
SettingsRepository::~SettingsRepository() = default;

SettingRow SettingsRepository::mapRow(const auto& row) const {
  SettingRow sr;
  sr.sKey = row[0].template as<std::string>();
  sr.sValue = row[1].template as<std::string>();
  sr.sDescription = row[2].is_null() ? "" : row[2].template as<std::string>();
  sr.sUpdatedAt = row[3].is_null() ? "" : row[3].template as<std::string>();
  return sr;
}

std::optional<SettingRow> SettingsRepository::findByKey(const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec_params(
      "SELECT key, value, description, updated_at FROM system_config WHERE key = $1",
      sKey);
  txn.commit();
  if (result.empty()) {
    return std::nullopt;
  }
  return mapRow(result[0]);
}

std::string SettingsRepository::getValue(const std::string& sKey, const std::string& sDefault) {
  auto oRow = findByKey(sKey);
  return oRow ? oRow->sValue : sDefault;
}

int SettingsRepository::getInt(const std::string& sKey, int iDefault) {
  auto oRow = findByKey(sKey);
  if (!oRow) return iDefault;
  try {
    return std::stoi(oRow->sValue);
  } catch (...) {
    return iDefault;
  }
}

bool SettingsRepository::getBool(const std::string& sKey, bool bDefault) {
  auto oRow = findByKey(sKey);
  if (!oRow) return bDefault;
  return oRow->sValue == "true" || oRow->sValue == "1";
}

std::vector<SettingRow> SettingsRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT key, value, description, updated_at FROM system_config ORDER BY key");
  txn.commit();

  std::vector<SettingRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapRow(row));
  }
  return vRows;
}

void SettingsRepository::upsert(const std::string& sKey, const std::string& sValue,
                                const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec_params(
      "INSERT INTO system_config (key, value, description, updated_at) "
      "VALUES ($1, $2, $3, now()) "
      "ON CONFLICT (key) DO UPDATE SET value = $2, description = $3, updated_at = now()",
      sKey, sValue, sDescription.empty() ? std::optional<std::string>{} : sDescription);
  txn.commit();
}

bool SettingsRepository::seedIfMissing(const std::string& sKey, const std::string& sValue,
                                       const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec_params(
      "INSERT INTO system_config (key, value, description, updated_at) "
      "VALUES ($1, $2, $3, now()) "
      "ON CONFLICT (key) DO NOTHING",
      sKey, sValue, sDescription.empty() ? std::optional<std::string>{} : sDescription);
  txn.commit();
  return result.affected_rows() > 0;
}

void SettingsRepository::deleteByKey(const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec_params("DELETE FROM system_config WHERE key = $1", sKey);
  txn.commit();
}

}  // namespace dns::dal
```

**Step 3: Verify build compiles**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds (the new files are picked up by the existing `GLOB_RECURSE` in `src/CMakeLists.txt`).

**Step 4: Commit**

```bash
git add include/dal/SettingsRepository.hpp src/dal/SettingsRepository.cpp
git commit -m "feat(dal): add SettingsRepository for system_config table"
```

---

### Task 3: SettingsRepository Tests

**Files:**
- Create: `tests/integration/test_settings_repository.cpp`

Follow the existing integration test pattern (see `tests/integration/test_provider_repository.cpp`). Tests skip when `DNS_DB_URL` is not set.

**Step 1: Write the test file**

Create `tests/integration/test_settings_repository.cpp`:

```cpp
#include "dal/SettingsRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class SettingsRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Clean test keys (leave setup_completed alone)
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM system_config WHERE key LIKE 'test.%'");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(SettingsRepositoryTest, SeedIfMissing_InsertsNewKey) {
  bool bInserted = _srRepo->seedIfMissing("test.key1", "value1", "A test setting");
  EXPECT_TRUE(bInserted);

  auto oRow = _srRepo->findByKey("test.key1");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "value1");
  EXPECT_EQ(oRow->sDescription, "A test setting");
}

TEST_F(SettingsRepositoryTest, SeedIfMissing_DoesNotOverwriteExisting) {
  _srRepo->upsert("test.key2", "original", "desc");

  bool bInserted = _srRepo->seedIfMissing("test.key2", "seeded_value", "new desc");
  EXPECT_FALSE(bInserted);

  auto oRow = _srRepo->findByKey("test.key2");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "original");
}

TEST_F(SettingsRepositoryTest, Upsert_InsertsAndUpdates) {
  _srRepo->upsert("test.key3", "v1", "initial");
  auto oRow = _srRepo->findByKey("test.key3");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "v1");

  _srRepo->upsert("test.key3", "v2", "updated");
  oRow = _srRepo->findByKey("test.key3");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "v2");
  EXPECT_EQ(oRow->sDescription, "updated");
}

TEST_F(SettingsRepositoryTest, FindByKey_ReturnsNulloptForMissing) {
  auto oRow = _srRepo->findByKey("test.nonexistent");
  EXPECT_FALSE(oRow.has_value());
}

TEST_F(SettingsRepositoryTest, GetValue_ReturnsDefaultForMissing) {
  std::string sVal = _srRepo->getValue("test.missing", "fallback");
  EXPECT_EQ(sVal, "fallback");
}

TEST_F(SettingsRepositoryTest, GetInt_ParsesValueOrReturnsDefault) {
  _srRepo->upsert("test.int_key", "42");
  EXPECT_EQ(_srRepo->getInt("test.int_key", 0), 42);

  _srRepo->upsert("test.bad_int", "not_a_number");
  EXPECT_EQ(_srRepo->getInt("test.bad_int", 99), 99);

  EXPECT_EQ(_srRepo->getInt("test.missing_int", 7), 7);
}

TEST_F(SettingsRepositoryTest, GetBool_ParsesValueOrReturnsDefault) {
  _srRepo->upsert("test.bool_true", "true");
  EXPECT_TRUE(_srRepo->getBool("test.bool_true", false));

  _srRepo->upsert("test.bool_one", "1");
  EXPECT_TRUE(_srRepo->getBool("test.bool_one", false));

  _srRepo->upsert("test.bool_false", "false");
  EXPECT_FALSE(_srRepo->getBool("test.bool_false", true));

  EXPECT_TRUE(_srRepo->getBool("test.missing_bool", true));
}

TEST_F(SettingsRepositoryTest, ListAll_ReturnsAllSettings) {
  _srRepo->upsert("test.list_a", "a");
  _srRepo->upsert("test.list_b", "b");

  auto vAll = _srRepo->listAll();
  // Should contain at least our 2 test keys (plus setup_completed if present)
  int iTestKeys = 0;
  for (const auto& sr : vAll) {
    if (sr.sKey == "test.list_a" || sr.sKey == "test.list_b") {
      ++iTestKeys;
    }
  }
  EXPECT_EQ(iTestKeys, 2);
}

TEST_F(SettingsRepositoryTest, DeleteByKey_RemovesKey) {
  _srRepo->upsert("test.delete_me", "bye");
  EXPECT_TRUE(_srRepo->findByKey("test.delete_me").has_value());

  _srRepo->deleteByKey("test.delete_me");
  EXPECT_FALSE(_srRepo->findByKey("test.delete_me").has_value());
}
```

**Step 2: Verify tests compile**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 3: Run tests (if DB available)**

Run: `build/tests/dns-tests --gtest_filter='SettingsRepository*' 2>&1`
Expected: If `DNS_DB_URL` is set, all 9 tests pass. Otherwise, all skip with "DNS_DB_URL not set".

**Step 4: Commit**

```bash
git add tests/integration/test_settings_repository.cpp
git commit -m "test(dal): add SettingsRepository integration tests"
```

---

### Task 4: Config Refactor — Env-Only Vars

**Files:**
- Modify: `include/common/Config.hpp`
- Modify: `src/common/Config.cpp`

Slim down `Config::load()` so it only loads the 5 env-only variables: `DNS_DB_URL`, `DNS_MASTER_KEY`/`_FILE`, `DNS_JWT_SECRET`/`_FILE`, `DNS_LOG_LEVEL`, `DNS_HTTP_PORT`. All other settings will be loaded from DB in the next task. Keep the fields on `Config` for now — they'll be populated from DB after seeding.

Add a new method `Config::loadFromDb(SettingsRepository&)` that populates the remaining fields from the database.

Add a new method `Config::seedToDb(SettingsRepository&)` that seeds default values into the database from env vars.

**Step 1: Update the header**

In `include/common/Config.hpp`, add forward declaration and two new methods:

```cpp
// After the existing includes, add:
namespace dns::dal { class SettingsRepository; }

// Inside the Config struct, add these public methods after load():

  /// Seed default values into system_config from env vars.
  /// Only inserts if the key does not already exist in DB.
  static void seedToDb(dns::dal::SettingsRepository& srRepo);

  /// Populate non-env-only Config fields from the database.
  void loadFromDb(dns::dal::SettingsRepository& srRepo);
```

**Step 2: Define the settings registry**

Create a new file `include/common/SettingsDef.hpp` that defines all DB-configurable settings as a compile-time registry. This avoids duplicating key strings and defaults across seed/load/API code:

```cpp
#pragma once

#include <array>
#include <string_view>

namespace dns::common {

/// Metadata for a single DB-configurable setting.
struct SettingDef {
  std::string_view sKey;
  std::string_view sDefault;
  std::string_view sDescription;
  std::string_view sEnvVar;      // env var to seed from (empty if none)
  bool bRestartRequired;          // true if change needs restart
};

/// All DB-configurable settings. Source of truth for keys, defaults, and descriptions.
inline constexpr std::array<SettingDef, 13> kSettings = {{
  {"http.threads", "4", "Number of HTTP server threads", "DNS_HTTP_THREADS", true},
  {"session.absolute_ttl_seconds", "86400", "Session absolute TTL in seconds",
   "DNS_SESSION_ABSOLUTE_TTL_SECONDS", false},
  {"session.cleanup_interval_seconds", "3600", "Session cleanup interval in seconds",
   "DNS_SESSION_CLEANUP_INTERVAL_SECONDS", false},
  {"apikey.cleanup_grace_seconds", "300", "API key cleanup grace period in seconds",
   "DNS_API_KEY_CLEANUP_GRACE_SECONDS", false},
  {"apikey.cleanup_interval_seconds", "3600", "API key cleanup interval in seconds",
   "DNS_API_KEY_CLEANUP_INTERVAL_SECONDS", false},
  {"deployment.retention_count", "10", "Number of deployment snapshots to retain per zone",
   "DNS_DEPLOYMENT_RETENTION_COUNT", false},
  {"ui.dir", "", "Path to built UI assets (empty = disabled)", "DNS_UI_DIR", true},
  {"migrations.dir", "/opt/meridian-dns/db", "Path to migration version directories",
   "DNS_MIGRATIONS_DIR", true},
  {"sync.check_interval_seconds", "3600", "Zone sync check interval in seconds (0 = disabled)",
   "DNS_SYNC_CHECK_INTERVAL", false},
  {"audit.db_url", "", "Separate audit database URL (empty = use main DB)",
   "DNS_AUDIT_DB_URL", true},
  {"audit.stdout", "false", "Also write audit entries to stdout", "DNS_AUDIT_STDOUT", false},
  {"audit.retention_days", "365", "Audit log retention in days",
   "DNS_AUDIT_RETENTION_DAYS", false},
  {"audit.purge_interval_seconds", "86400", "Audit purge interval in seconds",
   "DNS_AUDIT_PURGE_INTERVAL_SECONDS", false},
}};

}  // namespace dns::common
```

**Step 3: Update `Config::load()` to only load env-only vars**

In `src/common/Config.cpp`, strip `Config::load()` down to the 5 env-only variables. Keep loading the DB-configurable env vars so they're available for seeding, but store them in temporary locals rather than the Config fields (the fields will be populated from DB in `loadFromDb()`):

Actually — a simpler approach. Keep `Config::load()` loading everything from env vars as before (for backward compatibility during migration). Then `seedToDb()` pushes env values into DB if missing. Then `loadFromDb()` overwrites Config fields from DB. This means env vars still work as a fallback for first run, and DB takes precedence once seeded.

So `Config::load()` stays **unchanged**. Add these two new methods:

In `src/common/Config.cpp`, add at the bottom (before the closing namespace brace):

```cpp
#include "common/SettingsDef.hpp"
#include "dal/SettingsRepository.hpp"

void Config::seedToDb(dns::dal::SettingsRepository& srRepo) {
  auto spLog = Logger::get();

  for (const auto& def : kSettings) {
    // Check if env var is set — use it as seed value
    std::string sSeedValue{def.sDefault};
    if (!def.sEnvVar.empty()) {
      const char* pEnv = std::getenv(std::string(def.sEnvVar).c_str());
      if (pEnv && pEnv[0] != '\0') {
        sSeedValue = pEnv;
      }
    }

    bool bInserted = srRepo.seedIfMissing(
        std::string(def.sKey), sSeedValue, std::string(def.sDescription));
    if (bInserted) {
      spLog->debug("Seeded setting {}: {}", def.sKey, sSeedValue);
    }
  }
}

void Config::loadFromDb(dns::dal::SettingsRepository& srRepo) {
  iHttpThreads = srRepo.getInt("http.threads", iHttpThreads);
  iSessionAbsoluteTtlSeconds = srRepo.getInt("session.absolute_ttl_seconds",
                                              iSessionAbsoluteTtlSeconds);
  iSessionCleanupIntervalSeconds = srRepo.getInt("session.cleanup_interval_seconds",
                                                  iSessionCleanupIntervalSeconds);
  iApiKeyCleanupGraceSeconds = srRepo.getInt("apikey.cleanup_grace_seconds",
                                              iApiKeyCleanupGraceSeconds);
  iApiKeyCleanupIntervalSeconds = srRepo.getInt("apikey.cleanup_interval_seconds",
                                                 iApiKeyCleanupIntervalSeconds);
  iDeploymentRetentionCount = srRepo.getInt("deployment.retention_count",
                                             iDeploymentRetentionCount);
  sUiDir = srRepo.getValue("ui.dir", sUiDir);
  sMigrationsDir = srRepo.getValue("migrations.dir", sMigrationsDir);
  iSyncCheckInterval = srRepo.getInt("sync.check_interval_seconds", iSyncCheckInterval);

  std::string sAuditDb = srRepo.getValue("audit.db_url", "");
  if (!sAuditDb.empty()) {
    oAuditDbUrl = sAuditDb;
  }
  bAuditStdout = srRepo.getBool("audit.stdout", bAuditStdout);
  iAuditRetentionDays = srRepo.getInt("audit.retention_days", iAuditRetentionDays);
  iAuditPurgeIntervalSeconds = srRepo.getInt("audit.purge_interval_seconds",
                                              iAuditPurgeIntervalSeconds);
}
```

**Step 4: Verify build compiles**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 5: Commit**

```bash
git add include/common/SettingsDef.hpp include/common/Config.hpp src/common/Config.cpp
git commit -m "feat(config): add SettingsDef registry, seedToDb, and loadFromDb methods"
```

---

### Task 5: Config Seeding from DB (Startup Wiring)

**Files:**
- Modify: `src/main.cpp`

Wire the seeding and DB-loading into the startup sequence. The flow becomes:

1. `Config::load()` — env vars (unchanged)
2. Connect to DB, run migrations (including v007)
3. Create `SettingsRepository`
4. `Config::seedToDb(srRepo)` — push env values into DB if missing
5. `cfgApp.loadFromDb(srRepo)` — overwrite Config fields from DB

**Step 1: Add include and create SettingsRepository after migrations**

In `src/main.cpp`, add include at the top:

```cpp
#include "dal/SettingsRepository.hpp"
```

After the migration step (Step 0) and before Step 2 (CryptoService), insert:

```cpp
    // ── Step 0b: Seed and load DB settings ──────────────────────────────
    {
      dns::dal::ConnectionPool cpSeedPool(cfgApp.sDbUrl, 1);
      dns::dal::SettingsRepository srSeedRepo(cpSeedPool);
      dns::common::Config::seedToDb(srSeedRepo);
      cfgApp.loadFromDb(srSeedRepo);
      spLog->info("Step 0b: Settings seeded and loaded from database");
    }
```

**Why a temporary pool?** The main `ConnectionPool` hasn't been created yet (it depends on `Config::iDbPoolSize` which we just loaded). Use a single-connection pool for seeding, then discard it. The main pool is created at Step 4 with the correct size.

**Step 2: Also create a SettingsRepository on the main pool for routes**

After the main `ConnectionPool` is created (Step 4), alongside the other repositories in Step 7a:

```cpp
    auto settingsRepo = std::make_unique<dns::dal::SettingsRepository>(*cpPool);
```

This will be passed to `SettingsRoutes` in a later task.

**Step 3: Verify build compiles**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Verify tests still pass**

Run: `build/tests/dns-tests 2>&1 | tail -10`
Expected: Same pass/skip counts as before (no regressions).

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(startup): wire config seeding and DB loading into startup sequence"
```

---

### Task 6: Config Seeding Tests

**Files:**
- Create: `tests/integration/test_config_seeding.cpp`

Tests verify:
1. `seedToDb()` inserts defaults when DB is empty
2. `seedToDb()` does not overwrite existing values
3. `loadFromDb()` correctly populates Config fields from DB
4. Env var takes precedence over compiled default when seeding

**Step 1: Write the test file**

Create `tests/integration/test_config_seeding.cpp`:

```cpp
#include "common/Config.hpp"
#include "common/SettingsDef.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/SettingsRepository.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class ConfigSeedingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Clear all settings except setup_completed
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM system_config WHERE key != 'setup_completed'");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(ConfigSeedingTest, SeedToDb_InsertsCompiledDefaults) {
  dns::common::Config::seedToDb(*_srRepo);

  // Verify a known setting was seeded with its compiled default
  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "10");

  auto oRow2 = _srRepo->findByKey("http.threads");
  ASSERT_TRUE(oRow2.has_value());
  EXPECT_EQ(oRow2->sValue, "4");
}

TEST_F(ConfigSeedingTest, SeedToDb_DoesNotOverwriteExistingValues) {
  _srRepo->upsert("deployment.retention_count", "50", "custom");

  dns::common::Config::seedToDb(*_srRepo);

  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "50");
}

TEST_F(ConfigSeedingTest, SeedToDb_SeedsAllDefinedSettings) {
  dns::common::Config::seedToDb(*_srRepo);

  for (const auto& def : dns::common::kSettings) {
    auto oRow = _srRepo->findByKey(std::string(def.sKey));
    ASSERT_TRUE(oRow.has_value()) << "Missing setting: " << def.sKey;
  }
}

TEST_F(ConfigSeedingTest, LoadFromDb_PopulatesConfigFields) {
  _srRepo->upsert("deployment.retention_count", "25");
  _srRepo->upsert("http.threads", "8");
  _srRepo->upsert("audit.stdout", "true");
  _srRepo->upsert("sync.check_interval_seconds", "0");

  dns::common::Config cfg;
  cfg.loadFromDb(*_srRepo);

  EXPECT_EQ(cfg.iDeploymentRetentionCount, 25);
  EXPECT_EQ(cfg.iHttpThreads, 8);
  EXPECT_TRUE(cfg.bAuditStdout);
  EXPECT_EQ(cfg.iSyncCheckInterval, 0);
}

TEST_F(ConfigSeedingTest, LoadFromDb_UsesFieldDefaultsForMissingKeys) {
  // Don't seed anything — loadFromDb should fall back to Config field defaults
  dns::common::Config cfg;
  int iOriginalRetention = cfg.iDeploymentRetentionCount;
  cfg.loadFromDb(*_srRepo);
  EXPECT_EQ(cfg.iDeploymentRetentionCount, iOriginalRetention);
}
```

**Step 2: Verify tests compile**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 3: Run tests**

Run: `build/tests/dns-tests --gtest_filter='ConfigSeeding*' 2>&1`
Expected: All 5 tests pass (or skip if no DB).

**Step 4: Commit**

```bash
git add tests/integration/test_config_seeding.cpp
git commit -m "test(config): add integration tests for config seeding and DB loading"
```

---

### Task 7: SettingsRoutes — GET

**Files:**
- Create: `include/api/routes/SettingsRoutes.hpp`
- Create: `src/api/routes/SettingsRoutes.cpp` (partial — GET only)

`GET /api/v1/settings` returns all settings with metadata. Admin-only.

**Step 1: Create the header**

Create `include/api/routes/SettingsRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::dal {
class SettingsRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/settings
/// Class abbreviation: st
class SettingsRoutes {
 public:
  SettingsRoutes(dns::dal::SettingsRepository& srRepo,
                 const dns::api::AuthMiddleware& amMiddleware);
  ~SettingsRoutes();

  /// Register settings routes on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::SettingsRepository& _srRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
```

**Step 2: Create the implementation with GET**

Create `src/api/routes/SettingsRoutes.cpp`:

```cpp
#include "api/routes/SettingsRoutes.hpp"

#include "api/RouteHelpers.hpp"
#include "common/SettingsDef.hpp"
#include "dal/SettingsRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

SettingsRoutes::SettingsRoutes(dns::dal::SettingsRepository& srRepo,
                               const dns::api::AuthMiddleware& amMiddleware)
    : _srRepo(srRepo), _amMiddleware(amMiddleware) {}

SettingsRoutes::~SettingsRoutes() = default;

void SettingsRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/settings — list all settings with metadata
  CROW_ROUTE(app, "/api/v1/settings")
      .methods(crow::HTTPMethod::GET)(
          [this](const crow::request& req) -> crow::response {
            try {
              auto rcCtx = authenticate(_amMiddleware, req);
              requireRole(rcCtx, "admin");

              auto vRows = _srRepo.listAll();

              // Build a map from key → SettingDef for metadata lookup
              nlohmann::json jSettings = nlohmann::json::array();

              for (const auto& row : vRows) {
                // Skip internal keys (like setup_completed)
                bool bIsConfigSetting = false;
                std::string sCompiledDefault;
                bool bRestartRequired = false;

                for (const auto& def : dns::common::kSettings) {
                  if (def.sKey == row.sKey) {
                    bIsConfigSetting = true;
                    sCompiledDefault = std::string(def.sDefault);
                    bRestartRequired = def.bRestartRequired;
                    break;
                  }
                }

                if (!bIsConfigSetting) continue;

                nlohmann::json jRow = {
                    {"key", row.sKey},
                    {"value", row.sValue},
                    {"description", row.sDescription},
                    {"default", sCompiledDefault},
                    {"restart_required", bRestartRequired},
                    {"updated_at", row.sUpdatedAt},
                };
                jSettings.push_back(jRow);
              }

              return jsonResponse(200, jSettings);
            } catch (const common::AppError& e) {
              return errorResponse(e);
            }
          });
}

}  // namespace dns::api::routes
```

**Step 3: Verify build compiles**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Commit**

```bash
git add include/api/routes/SettingsRoutes.hpp src/api/routes/SettingsRoutes.cpp
git commit -m "feat(api): add GET /api/v1/settings endpoint (admin-only)"
```

---

### Task 8: SettingsRoutes — PUT

**Files:**
- Modify: `src/api/routes/SettingsRoutes.cpp`

`PUT /api/v1/settings` accepts a JSON object of key-value pairs to update. Validates that all keys are known DB-configurable settings. Returns which settings were updated.

**Step 1: Add the PUT route**

In `src/api/routes/SettingsRoutes.cpp`, inside `registerRoutes()`, add after the GET route:

```cpp
  // PUT /api/v1/settings — update one or more settings
  CROW_ROUTE(app, "/api/v1/settings")
      .methods(crow::HTTPMethod::PUT)(
          [this](const crow::request& req) -> crow::response {
            try {
              auto rcCtx = authenticate(_amMiddleware, req);
              requireRole(rcCtx, "admin");

              auto jBody = nlohmann::json::parse(req.body);
              if (!jBody.is_object()) {
                throw common::ValidationError(
                    "invalid_body", "Request body must be a JSON object of key-value pairs");
              }

              nlohmann::json jUpdated = nlohmann::json::array();

              for (auto& [sKey, jValue] : jBody.items()) {
                // Validate key is a known setting
                bool bKnown = false;
                std::string sDescription;
                for (const auto& def : dns::common::kSettings) {
                  if (def.sKey == sKey) {
                    bKnown = true;
                    sDescription = std::string(def.sDescription);
                    break;
                  }
                }
                if (!bKnown) {
                  throw common::ValidationError(
                      "unknown_setting", "Unknown setting key: " + sKey);
                }

                std::string sNewValue;
                if (jValue.is_string()) {
                  sNewValue = jValue.get<std::string>();
                } else if (jValue.is_number_integer()) {
                  sNewValue = std::to_string(jValue.get<int64_t>());
                } else if (jValue.is_boolean()) {
                  sNewValue = jValue.get<bool>() ? "true" : "false";
                } else {
                  throw common::ValidationError(
                      "invalid_value",
                      "Setting value must be a string, integer, or boolean: " + sKey);
                }

                _srRepo.upsert(sKey, sNewValue, sDescription);
                jUpdated.push_back(sKey);
              }

              return jsonResponse(200, {{"message", "Settings updated"},
                                        {"updated", jUpdated}});
            } catch (const common::AppError& e) {
              return errorResponse(e);
            } catch (const nlohmann::json::exception&) {
              return invalidJsonResponse();
            }
          });
```

**Step 2: Verify build compiles**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Build succeeds.

**Step 3: Commit**

```bash
git add src/api/routes/SettingsRoutes.cpp
git commit -m "feat(api): add PUT /api/v1/settings endpoint for updating settings"
```

---

### Task 9: SettingsRoutes Tests

**Files:**
- Create: `tests/integration/test_settings_routes.cpp`

Test GET and PUT endpoints. Follow the existing test pattern from `tests/integration/test_api_validation.cpp`.

**Step 1: Write the test file**

Create `tests/integration/test_settings_routes.cpp`:

```cpp
#include "api/routes/SettingsRoutes.hpp"
#include "api/RouteHelpers.hpp"
#include "common/SettingsDef.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/SettingsRepository.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class SettingsRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Seed settings so GET returns data
    dns::common::Config::seedToDb(*_srRepo);
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(SettingsRoutesTest, ListAll_ReturnsAllConfigurableSettings) {
  auto vRows = _srRepo->listAll();

  // Filter to only known config settings (same logic as GET endpoint)
  int iConfigKeys = 0;
  for (const auto& row : vRows) {
    for (const auto& def : dns::common::kSettings) {
      if (def.sKey == row.sKey) {
        ++iConfigKeys;
        break;
      }
    }
  }
  EXPECT_EQ(iConfigKeys, static_cast<int>(dns::common::kSettings.size()));
}

TEST_F(SettingsRoutesTest, Upsert_UpdatesExistingSetting) {
  _srRepo->upsert("deployment.retention_count", "99");
  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "99");
}

TEST_F(SettingsRoutesTest, ValidateKnownSettingKey) {
  bool bKnown = false;
  for (const auto& def : dns::common::kSettings) {
    if (def.sKey == "deployment.retention_count") {
      bKnown = true;
      break;
    }
  }
  EXPECT_TRUE(bKnown);

  bKnown = false;
  for (const auto& def : dns::common::kSettings) {
    if (def.sKey == "bogus.setting") {
      bKnown = true;
      break;
    }
  }
  EXPECT_FALSE(bKnown);
}
```

**Step 2: Verify tests compile and run**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='SettingsRoutes*' 2>&1`
Expected: All tests pass (or skip if no DB).

**Step 3: Commit**

```bash
git add tests/integration/test_settings_routes.cpp
git commit -m "test(api): add SettingsRoutes integration tests"
```

---

### Task 10: Wire SettingsRoutes into main.cpp

**Files:**
- Modify: `src/main.cpp`

Register `SettingsRoutes` in the startup sequence alongside other route handlers.

**Step 1: Add include**

In `src/main.cpp`, add at the top with the other route includes:

```cpp
#include "api/routes/SettingsRoutes.hpp"
```

**Step 2: Create SettingsRoutes and register**

After the `apiKeyRoutes` creation (around line 410), add:

```cpp
    auto settingsRoutes = std::make_unique<dns::api::routes::SettingsRoutes>(
        *settingsRepo, *amMiddleware);
```

After the `apiKeyRoutes->registerRoutes(crowApp)` line, add:

```cpp
    settingsRoutes->registerRoutes(crowApp);
```

**Step 3: Verify build compiles and tests pass**

Run: `cmake --build build --parallel && build/tests/dns-tests 2>&1 | tail -10`
Expected: Build succeeds, same test counts as before.

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(startup): wire SettingsRoutes into API server"
```

---

### Task 11: UI — Settings API Client

**Files:**
- Create: `ui/src/api/settings.ts`
- Modify: `ui/src/types/index.ts`

**Step 1: Add types**

In `ui/src/types/index.ts`, add at the bottom:

```typescript
export interface SystemSetting {
  key: string
  value: string
  description: string
  default: string
  restart_required: boolean
  updated_at: string
}
```

**Step 2: Create the API module**

Create `ui/src/api/settings.ts`:

```typescript
import { get, put } from './client'
import type { SystemSetting } from '../types'

export function listSettings(): Promise<SystemSetting[]> {
  return get('/settings')
}

export function updateSettings(data: Record<string, string | number | boolean>): Promise<{ message: string; updated: string[] }> {
  return put('/settings', data)
}
```

**Step 3: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc --noEmit`
Expected: No type errors.

**Step 4: Commit**

```bash
git add ui/src/api/settings.ts ui/src/types/index.ts
git commit -m "feat(ui): add settings API client module and SystemSetting type"
```

---

### Task 12: UI — SettingsView Page

**Files:**
- Create: `ui/src/views/SettingsView.vue`

Admin-only settings page with card-based sections. Each section groups related settings. Each field shows current value, compiled default, and a restart-required indicator where applicable.

**Step 1: Create the view**

Create `ui/src/views/SettingsView.vue`:

```vue
<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import InputSwitch from 'primevue/inputswitch'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import PageHeader from '../components/shared/PageHeader.vue'
import { listSettings, updateSettings } from '../api/settings'
import { useNotificationStore } from '../stores/notification'
import type { SystemSetting } from '../types'

const notify = useNotificationStore()
const settings = ref<SystemSetting[]>([])
const editValues = ref<Record<string, string>>({})
const loading = ref(false)
const saving = ref(false)

const sections = [
  {
    title: 'Session & Security',
    icon: 'pi pi-shield',
    keys: [
      'session.absolute_ttl_seconds',
      'session.cleanup_interval_seconds',
      'apikey.cleanup_grace_seconds',
      'apikey.cleanup_interval_seconds',
    ],
  },
  {
    title: 'Deployment',
    icon: 'pi pi-upload',
    keys: ['deployment.retention_count'],
  },
  {
    title: 'Sync',
    icon: 'pi pi-sync',
    keys: ['sync.check_interval_seconds'],
  },
  {
    title: 'Audit',
    icon: 'pi pi-history',
    keys: [
      'audit.retention_days',
      'audit.purge_interval_seconds',
      'audit.stdout',
    ],
  },
  {
    title: 'Paths',
    icon: 'pi pi-folder',
    keys: ['ui.dir', 'migrations.dir', 'audit.db_url'],
  },
  {
    title: 'Performance',
    icon: 'pi pi-bolt',
    keys: ['http.threads'],
  },
]

const settingsByKey = computed(() => {
  const map: Record<string, SystemSetting> = {}
  for (const s of settings.value) {
    map[s.key] = s
  }
  return map
})

const hasChanges = computed(() => {
  for (const s of settings.value) {
    if (editValues.value[s.key] !== s.value) return true
  }
  return false
})

async function fetchSettings() {
  loading.value = true
  try {
    settings.value = await listSettings()
    editValues.value = {}
    for (const s of settings.value) {
      editValues.value[s.key] = s.value
    }
  } catch {
    notify.error('Failed to load settings')
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
    const changed: Record<string, string> = {}
    for (const s of settings.value) {
      if (editValues.value[s.key] !== s.value) {
        changed[s.key] = editValues.value[s.key]
      }
    }
    if (Object.keys(changed).length === 0) return

    const result = await updateSettings(changed)
    notify.success(`Updated ${result.updated.length} setting(s)`)

    // Check if any restart-required setting was changed
    const restartNeeded = result.updated.some(key => {
      const s = settingsByKey.value[key]
      return s?.restart_required
    })
    if (restartNeeded) {
      notify.warn('Some changes require a service restart to take effect')
    }

    await fetchSettings()
  } catch {
    notify.error('Failed to save settings')
  } finally {
    saving.value = false
  }
}

function resetToDefaults() {
  for (const s of settings.value) {
    editValues.value[s.key] = s.default
  }
}

function isIntegerSetting(key: string) {
  return !key.endsWith('.stdout') && !key.includes('.dir') && !key.includes('.db_url')
}

function isBooleanSetting(key: string) {
  return key === 'audit.stdout'
}

function isStringSetting(key: string) {
  return key.includes('.dir') || key.includes('.db_url')
}

onMounted(fetchSettings)
</script>

<template>
  <div class="settings-page">
    <PageHeader title="Settings" subtitle="System configuration">
      <Button
        label="Reset to Defaults"
        icon="pi pi-refresh"
        severity="secondary"
        size="small"
        :disabled="loading"
        @click="resetToDefaults"
      />
      <Button
        label="Save Changes"
        icon="pi pi-check"
        size="small"
        :disabled="!hasChanges || saving"
        :loading="saving"
        @click="save"
      />
    </PageHeader>

    <div v-if="loading" class="loading-state">Loading settings...</div>

    <div v-else class="settings-sections">
      <section v-for="section in sections" :key="section.title" class="settings-section">
        <h3 class="section-title">
          <i :class="section.icon" class="section-icon" />
          {{ section.title }}
        </h3>
        <div class="settings-grid">
          <div
            v-for="key in section.keys"
            :key="key"
            class="setting-field"
          >
            <div class="setting-header">
              <label :for="key" class="setting-label">{{ key }}</label>
              <Tag
                v-if="settingsByKey[key]?.restart_required"
                value="Restart required"
                severity="warn"
                class="restart-tag"
              />
            </div>
            <p class="setting-description">
              {{ settingsByKey[key]?.description }}
            </p>
            <div class="setting-input">
              <InputSwitch
                v-if="isBooleanSetting(key)"
                :modelValue="editValues[key] === 'true'"
                @update:modelValue="editValues[key] = $event ? 'true' : 'false'"
              />
              <InputNumber
                v-else-if="isIntegerSetting(key)"
                :modelValue="Number(editValues[key]) || 0"
                @update:modelValue="editValues[key] = String($event ?? 0)"
                :id="key"
                class="w-full"
                :useGrouping="false"
              />
              <InputText
                v-else-if="isStringSetting(key)"
                v-model="editValues[key]"
                :id="key"
                class="w-full"
                :placeholder="settingsByKey[key]?.default || '(empty)'"
              />
            </div>
            <small class="setting-default">
              Default: {{ settingsByKey[key]?.default || '(empty)' }}
            </small>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<style scoped>
.settings-page {
  padding: 1.5rem;
  max-width: 60rem;
}

.loading-state {
  color: var(--p-surface-400);
  padding: 2rem;
}

.settings-sections {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.settings-section {
  background: var(--p-surface-900);
  border: 1px solid var(--p-surface-700);
  border-radius: 0.5rem;
  padding: 1.25rem;
}

:root:not(.app-dark) .settings-section {
  background: var(--p-surface-0);
  border-color: var(--p-surface-200);
}

.section-title {
  margin: 0 0 1rem;
  font-size: 1rem;
  font-weight: 600;
  color: var(--p-surface-100);
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

:root:not(.app-dark) .section-title {
  color: var(--p-surface-900);
}

.section-icon {
  font-size: 1rem;
}

.settings-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr));
  gap: 1.25rem;
}

.setting-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.setting-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.setting-label {
  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  font-size: 0.8125rem;
  font-weight: 600;
  color: var(--p-surface-200);
}

:root:not(.app-dark) .setting-label {
  color: var(--p-surface-800);
}

.restart-tag {
  font-size: 0.625rem;
}

.setting-description {
  margin: 0;
  font-size: 0.75rem;
  color: var(--p-surface-400);
  line-height: 1.4;
}

:root:not(.app-dark) .setting-description {
  color: var(--p-surface-500);
}

.setting-input {
  margin-top: 0.25rem;
}

.setting-default {
  font-size: 0.6875rem;
  color: var(--p-surface-500);
}

:root:not(.app-dark) .setting-default {
  color: var(--p-surface-400);
}
</style>
```

**Step 2: Verify TypeScript compiles**

Run: `cd ui && npx vue-tsc --noEmit`
Expected: No type errors.

**Step 3: Commit**

```bash
git add ui/src/views/SettingsView.vue
git commit -m "feat(ui): add SettingsView page with card-based sections"
```

---

### Task 13: UI — Routing + Sidebar

**Files:**
- Modify: `ui/src/router/index.ts`
- Modify: `ui/src/components/layout/AppSidebar.vue`

Add the `/settings` route and sidebar navigation entry (admin-only).

**Step 1: Add route**

In `ui/src/router/index.ts`, add inside the `children` array of the `'/'` route, after the `profile` route:

```typescript
        {
          path: 'settings',
          name: 'settings',
          component: () => import('../views/SettingsView.vue'),
        },
```

**Step 2: Add sidebar entry**

In `ui/src/components/layout/AppSidebar.vue`, add to the `navItems` array after the `Groups` entry:

```typescript
  { label: 'Settings', icon: 'pi pi-cog', to: '/settings', adminOnly: true },
```

**Step 3: Verify dev server loads**

Run: `cd ui && npm run dev`
Expected: Sidebar shows "Settings" link for admin users. Clicking it navigates to `/settings` and shows the settings page.

**Step 4: Commit**

```bash
git add ui/src/router/index.ts ui/src/components/layout/AppSidebar.vue
git commit -m "feat(ui): add settings route and sidebar navigation entry"
```

---

### Task 14: MaintenanceScheduler Dynamic Reload

**Files:**
- Modify: `include/core/MaintenanceScheduler.hpp`
- Modify: `src/core/MaintenanceScheduler.cpp`
- Modify: `src/api/routes/SettingsRoutes.cpp`

When settings are updated via PUT, dynamically-configurable maintenance task intervals should take effect on the next cycle. The simplest approach: `MaintenanceScheduler` gets a `reschedule()` method that updates a task's interval without stopping/restarting.

**Step 1: Add `reschedule()` to MaintenanceScheduler header**

In `include/core/MaintenanceScheduler.hpp`, add a public method:

```cpp
  /// Update the interval for an existing scheduled task.
  /// Takes effect on the next cycle (does not interrupt a currently sleeping task).
  void reschedule(const std::string& sName, std::chrono::seconds interval);
```

**Step 2: Implement `reschedule()`**

In `src/core/MaintenanceScheduler.cpp`, add the implementation. This should update the stored interval for the named task and notify the condition variable so the sleeping thread wakes up and uses the new interval.

The exact implementation depends on the current `MaintenanceScheduler` internals. Read the current implementation and add the `reschedule()` method that updates the interval in the task's stored metadata and signals the condition variable.

**Step 3: Wire into SettingsRoutes PUT handler**

In `src/api/routes/SettingsRoutes.cpp`, after successfully updating settings, if any interval-related settings changed, call `reschedule()` on the scheduler. This requires passing a `MaintenanceScheduler*` to `SettingsRoutes`.

Update the constructor to accept an optional `MaintenanceScheduler*`:

```cpp
SettingsRoutes(dns::dal::SettingsRepository& srRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::core::MaintenanceScheduler* pScheduler = nullptr);
```

In the PUT handler, after the upsert loop:

```cpp
              // Hot-reload maintenance intervals
              if (_pScheduler) {
                static const std::map<std::string, std::string> kIntervalMap = {
                    {"session.cleanup_interval_seconds", "session-flush"},
                    {"apikey.cleanup_interval_seconds", "api-key-cleanup"},
                    {"audit.purge_interval_seconds", "audit-purge"},
                    {"sync.check_interval_seconds", "sync-check"},
                };
                for (const auto& sKey : jUpdated) {
                  auto it = kIntervalMap.find(sKey.get<std::string>());
                  if (it != kIntervalMap.end()) {
                    int iNewInterval = _srRepo.getInt(it->first, 0);
                    if (iNewInterval > 0) {
                      _pScheduler->reschedule(it->second,
                                              std::chrono::seconds(iNewInterval));
                    }
                  }
                }
              }
```

**Step 4: Update main.cpp to pass scheduler to SettingsRoutes**

In `src/main.cpp`, update the `SettingsRoutes` creation:

```cpp
    auto settingsRoutes = std::make_unique<dns::api::routes::SettingsRoutes>(
        *settingsRepo, *amMiddleware, msScheduler.get());
```

**Step 5: Verify build compiles and tests pass**

Run: `cmake --build build --parallel && build/tests/dns-tests 2>&1 | tail -10`
Expected: Build succeeds, same test counts.

**Step 6: Commit**

```bash
git add include/core/MaintenanceScheduler.hpp src/core/MaintenanceScheduler.cpp \
        include/api/routes/SettingsRoutes.hpp src/api/routes/SettingsRoutes.cpp \
        src/main.cpp
git commit -m "feat(core): add MaintenanceScheduler reschedule + wire settings hot-reload"
```

---

### Task 15: Full Verification Pass

**Files:** None (QA task)

**Step 1: Build the entire project**

Run: `cmake --build build --parallel 2>&1`
Expected: Clean build, no warnings, no errors.

**Step 2: Run all tests**

Run: `build/tests/dns-tests 2>&1 | tail -20`
Expected: All existing tests still pass/skip. New settings tests pass (if `DNS_DB_URL` set) or skip.

**Step 3: Verify the UI builds**

Run: `cd ui && npm run build`
Expected: Production build completes without errors.

**Step 4: Manual smoke test (if DB available)**

1. Start the app, verify startup logs show "Step 0b: Settings seeded and loaded from database"
2. Open the UI, navigate to Settings page
3. Verify all 13 settings are displayed with correct defaults
4. Change `deployment.retention_count` to 20, click Save
5. Verify success toast, refresh page, verify value persisted
6. Change `http.threads` to 8, verify "Restart required" warning toast
7. Restart the app, verify `http.threads` reads 8 from DB

**Step 5: Check for regressions**

1. Verify login still works
2. Verify zone CRUD still works
3. Verify provider CRUD still works
4. Verify deployments page still loads

**Step 6: Final commit if any fixes needed**

```bash
git add -u
git commit -m "fix: address issues found during settings verification pass"
```

**Step 7: Verify production build**

Run: `cd ui && npm run build && cmake --build build --parallel`
Expected: Both builds succeed cleanly.

---

## Notes for Implementer

### Backward Compatibility

Environment variables continue to work as before. The only behavioral change: if a setting exists in `system_config`, it takes precedence over the env var. This is by design — the DB is the source of truth after initial seeding.

On first run after this upgrade:
1. App starts, loads env vars
2. Runs v007 migration (adds columns to `system_config`)
3. Seeds all 13 settings into `system_config` using env var values (or compiled defaults)
4. Loads Config fields from DB

On subsequent runs:
1. Seeds are no-ops (keys already exist)
2. Config loaded from DB
3. Env vars are ignored for DB-configurable settings

### The `setup_completed` Key

The `system_config` table already has `setup_completed = 'true'` if setup has been completed. This is an internal key, not a configurable setting. The `GET /api/v1/settings` endpoint filters it out — only keys defined in `kSettings` are returned.

### pqxx Template Member Functions

The `mapRow()` method uses `row[N].template as<std::string>()` — the `template` keyword is required because the function is a template member called on a dependent type. If the compiler complains about `auto` parameter in `mapRow`, change it to take `const pqxx::row& row` explicitly.

### Error Handling in SettingsRoutes

The PUT endpoint validates that all keys are known before updating any of them. If an unknown key is provided, the entire request fails with 400. This prevents typos from silently creating orphan keys in `system_config`.

### What NOT to Change

- Do not change the `Config::load()` method — it must continue loading all env vars for backward compatibility and seeding
- Do not remove any env var support from `.env.example` or `Dockerfile`
- Do not change the `MigrationRunner::bootstrap()` method — it must remain minimal to avoid breaking first-run setup
- Do not modify the `SetupRoutes` system_config usage (`setup_completed` key)
- Do not add authentication to the settings page — it's handled by the existing `requireRole("admin")` check in RouteHelpers
