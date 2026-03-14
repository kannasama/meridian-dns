# Workstream 5: Multi-Repo GitOps — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the singleton `GitOpsMirror` with a multi-repo `GitRepoManager` that manages one `GitRepoMirror` instance per database-configured git repository. Git repos become first-class entities with encrypted credentials, and zones reference a specific repo with optional branch override. Support SSH, HTTPS, and no-auth remotes.

**Architecture:** A new `git_repos` table stores repo configuration with AES-256-GCM encrypted credentials via `CryptoService`. `GitRepoRepository` provides DAL CRUD. `GitRepoMirror` encapsulates single-repo libgit2 operations (extracted from the existing `GitOpsMirror` singleton). `GitRepoManager` manages a map of `GitRepoMirror` instances keyed by repo ID, with hot-reload on admin changes. `DeploymentEngine` is updated to look up the zone's `git_repo_id` and commit to the correct mirror. On first run after upgrade, existing `DNS_GIT_REMOTE_URL` env var config is auto-migrated to a `git_repos` row. API routes (`GitRepoRoutes`) expose CRUD + test-connection + manual-sync. The UI adds a Git Repositories management page and updates the zone form with repo/branch selectors.

**Tech Stack:** C++20, PostgreSQL (libpqxx), libgit2, Crow HTTP, Google Test, Vue 3 + TypeScript + PrimeVue

---

## Task Overview

| # | Task | Description |
|---|------|-------------|
| 1 | Schema migration v010 | Create `git_repos` table, add `git_repo_id` and `git_branch` to `zones` |
| 2 | SettingsDef update | Add `gitops.base_path` DB setting |
| 3 | GitRepoRepository | DAL CRUD for `git_repos` table with credential encryption |
| 4 | GitRepoRepository tests | Integration tests for the repository |
| 5 | ZoneRepository update | Add `git_repo_id` and `git_branch` to ZoneRow and queries |
| 6 | ZoneRepository tests | Integration tests for updated zone fields |
| 7 | GitRepoMirror | Single-repo git operations extracted from GitOpsMirror |
| 8 | GitRepoMirror tests | Unit tests for snapshot building + local commit |
| 9 | GitRepoManager | Multi-repo manager with hot-reload |
| 10 | GitRepoManager tests | Unit tests for lifecycle management |
| 11 | DeploymentEngine refactor | Replace `GitOpsMirror*` with `GitRepoManager*` |
| 12 | DeploymentEngine tests | Verify commit routes to correct repo mirror |
| 13 | Env var migration | Auto-migrate `DNS_GIT_REMOTE_URL` to `git_repos` row on first run |
| 14 | Env var migration tests | Integration tests for migration logic |
| 15 | RequestValidator update | Add git repo input validation methods |
| 16 | GitRepoRoutes | CRUD + test-connection + manual-sync API endpoints |
| 17 | GitRepoRoutes tests | Integration tests for all endpoints |
| 18 | ZoneRoutes update | Accept `git_repo_id` and `git_branch` in zone CRUD |
| 19 | ZoneRoutes tests | Integration tests for updated zone endpoints |
| 20 | main.cpp wiring | Replace GitOpsMirror setup with GitRepoManager |
| 21 | UI — git repos API client | TypeScript API module for git repo endpoints |
| 22 | UI — types | TypeScript interfaces for GitRepo |
| 23 | UI — GitReposView page | DataTable CRUD with drawer form |
| 24 | UI — zone form update | Git repo dropdown + branch override field |
| 25 | UI — routing + sidebar | Add git repos route and navigation entry |
| 26 | Full verification pass | Build, test, manual QA |

---

### Task 1: Schema Migration v010

**Files:**
- Create: `scripts/db/v010/001_git_repos.sql`

This migration creates the `git_repos` table and adds git-related columns to the `zones` table.

**Step 1: Create the migration directory and SQL file**

Create `scripts/db/v010/001_git_repos.sql`:

```sql
-- Workstream 5: Multi-repo GitOps — git repository configuration

CREATE TABLE git_repos (
  id                    SERIAL PRIMARY KEY,
  name                  VARCHAR(100) UNIQUE NOT NULL,
  remote_url            TEXT NOT NULL,
  auth_type             VARCHAR(10) NOT NULL DEFAULT 'none'
                        CHECK (auth_type IN ('ssh', 'https', 'none')),
  encrypted_credentials TEXT,
  default_branch        VARCHAR(100) NOT NULL DEFAULT 'main',
  local_path            TEXT,
  known_hosts           TEXT,
  is_enabled            BOOLEAN NOT NULL DEFAULT true,
  last_sync_at          TIMESTAMPTZ,
  last_sync_status      VARCHAR(20),
  last_sync_error       TEXT,
  created_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at            TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_git_repos_enabled ON git_repos (is_enabled) WHERE is_enabled = true;

-- Add git repo reference and branch override to zones
ALTER TABLE zones ADD COLUMN git_repo_id INTEGER REFERENCES git_repos(id) ON DELETE SET NULL;
ALTER TABLE zones ADD COLUMN git_branch VARCHAR(100);
```

**Step 2: Verify the migration file exists**

Run: `ls -la scripts/db/v010/`
Expected: Shows `001_git_repos.sql`.

**Step 3: Commit**

```bash
git add scripts/db/v010/
git commit -m "feat(db): add v010 migration for git_repos table and zone git columns"
```

---

### Task 2: SettingsDef Update

**Files:**
- Modify: `include/common/SettingsDef.hpp:18-43`

Add the `gitops.base_path` setting to the `kSettings` array so the base directory for git repo local clones is DB-configurable.

**Step 1: Add the new setting to `kSettings`**

In `include/common/SettingsDef.hpp`, add a new entry at the end of the `kSettings` array (before the closing `}}`):

```cpp
{"gitops.base_path", "/var/meridian-dns/repos",
 "Base directory for git repository local clones", "DNS_GITOPS_BASE_PATH", true},
```

Update the array size from `14` to `15`.

**Step 2: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 3: Commit**

```bash
git add include/common/SettingsDef.hpp
git commit -m "feat(config): add gitops.base_path DB setting for repo clone base directory"
```

---

### Task 3: GitRepoRepository

**Files:**
- Create: `include/dal/GitRepoRepository.hpp`
- Create: `src/dal/GitRepoRepository.cpp`

This repository manages the `git_repos` table. Credentials are encrypted/decrypted via `CryptoService`, following the same pattern as `ProviderRepository` (`include/dal/ProviderRepository.hpp`) and `IdpRepository` (`include/dal/IdpRepository.hpp`).

**Step 1: Create the header**

Create `include/dal/GitRepoRepository.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pqxx {
class row;
}

namespace dns::security {
class CryptoService;
}

namespace dns::dal {

class ConnectionPool;

/// Row returned from git_repos queries.
struct GitRepoRow {
  int64_t iId = 0;
  std::string sName;
  std::string sRemoteUrl;
  std::string sAuthType;              // "ssh", "https", "none"
  std::string sDecryptedCredentials;  // Decrypted JSON string (only populated by findById)
  std::string sDefaultBranch;
  std::string sLocalPath;             // Auto-generated if empty
  std::string sKnownHosts;
  bool bIsEnabled = true;
  std::string sLastSyncAt;
  std::string sLastSyncStatus;        // "success", "failed", or empty
  std::string sLastSyncError;
  std::string sCreatedAt;
  std::string sUpdatedAt;
};

/// CRUD for git_repos table with encrypted credential storage.
/// Class abbreviation: gr
class GitRepoRepository {
 public:
  GitRepoRepository(ConnectionPool& cpPool, const dns::security::CryptoService& csService);
  ~GitRepoRepository();

  /// Create a git repo. Encrypts credentials before INSERT. Returns the new ID.
  int64_t create(const std::string& sName, const std::string& sRemoteUrl,
                 const std::string& sAuthType, const std::string& sPlaintextCredentials,
                 const std::string& sDefaultBranch, const std::string& sLocalPath,
                 const std::string& sKnownHosts);

  /// List all git repos. Does NOT decrypt credentials.
  std::vector<GitRepoRow> listAll();

  /// List enabled git repos. Does NOT decrypt credentials.
  std::vector<GitRepoRow> listEnabled();

  /// Find a git repo by ID. Decrypts credentials.
  std::optional<GitRepoRow> findById(int64_t iId);

  /// Find a git repo by name. Decrypts credentials.
  std::optional<GitRepoRow> findByName(const std::string& sName);

  /// Update a git repo. Re-encrypts credentials only if sPlaintextCredentials is non-empty.
  /// Sets updated_at to now().
  void update(int64_t iId, const std::string& sName, const std::string& sRemoteUrl,
              const std::string& sAuthType, const std::string& sPlaintextCredentials,
              const std::string& sDefaultBranch, const std::string& sLocalPath,
              const std::string& sKnownHosts, bool bIsEnabled);

  /// Update sync status fields after a sync operation.
  void updateSyncStatus(int64_t iId, const std::string& sStatus,
                        const std::string& sError = "");

  /// Delete a git repo by ID. Throws NotFoundError if not found.
  void deleteById(int64_t iId);

 private:
  ConnectionPool& _cpPool;
  const dns::security::CryptoService& _csService;

  GitRepoRow mapRow(const pqxx::row& row, bool bDecryptCredentials = false) const;
};

}  // namespace dns::dal
```

**Step 2: Create the implementation**

Create `src/dal/GitRepoRepository.cpp`:

```cpp
#include "dal/GitRepoRepository.hpp"

#include <pqxx/pqxx>

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"
#include "security/CryptoService.hpp"

namespace dns::dal {

GitRepoRepository::GitRepoRepository(ConnectionPool& cpPool,
                                     const dns::security::CryptoService& csService)
    : _cpPool(cpPool), _csService(csService) {}

GitRepoRepository::~GitRepoRepository() = default;

GitRepoRow GitRepoRepository::mapRow(const pqxx::row& row, bool bDecryptCredentials) const {
  GitRepoRow gr;
  gr.iId = row["id"].as<int64_t>();
  gr.sName = row["name"].as<std::string>();
  gr.sRemoteUrl = row["remote_url"].as<std::string>();
  gr.sAuthType = row["auth_type"].as<std::string>();
  gr.sDefaultBranch = row["default_branch"].as<std::string>();
  gr.sLocalPath = row["local_path"].is_null() ? "" : row["local_path"].as<std::string>();
  gr.sKnownHosts = row["known_hosts"].is_null() ? "" : row["known_hosts"].as<std::string>();
  gr.bIsEnabled = row["is_enabled"].as<bool>();
  gr.sLastSyncAt = row["last_sync_at"].is_null() ? "" : row["last_sync_at"].as<std::string>();
  gr.sLastSyncStatus = row["last_sync_status"].is_null()
                           ? "" : row["last_sync_status"].as<std::string>();
  gr.sLastSyncError = row["last_sync_error"].is_null()
                          ? "" : row["last_sync_error"].as<std::string>();
  gr.sCreatedAt = row["created_at"].as<std::string>();
  gr.sUpdatedAt = row["updated_at"].as<std::string>();

  if (bDecryptCredentials && !row["encrypted_credentials"].is_null()) {
    std::string sEncrypted = row["encrypted_credentials"].as<std::string>();
    if (!sEncrypted.empty()) {
      gr.sDecryptedCredentials = _csService.decrypt(sEncrypted);
    }
  }

  return gr;
}

int64_t GitRepoRepository::create(const std::string& sName, const std::string& sRemoteUrl,
                                  const std::string& sAuthType,
                                  const std::string& sPlaintextCredentials,
                                  const std::string& sDefaultBranch,
                                  const std::string& sLocalPath,
                                  const std::string& sKnownHosts) {
  std::string sEncrypted;
  if (!sPlaintextCredentials.empty()) {
    sEncrypted = _csService.encrypt(sPlaintextCredentials);
  }

  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec_params(
      "INSERT INTO git_repos (name, remote_url, auth_type, encrypted_credentials, "
      "default_branch, local_path, known_hosts) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
      sName, sRemoteUrl, sAuthType,
      sEncrypted.empty() ? std::optional<std::string>(std::nullopt) : sEncrypted,
      sDefaultBranch,
      sLocalPath.empty() ? std::optional<std::string>(std::nullopt) : sLocalPath,
      sKnownHosts.empty() ? std::optional<std::string>(std::nullopt) : sKnownHosts);

  txn.commit();
  return result[0][0].as<int64_t>();
}

std::vector<GitRepoRow> GitRepoRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec("SELECT * FROM git_repos ORDER BY name");
  txn.commit();

  std::vector<GitRepoRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapRow(row, false));
  }
  return vRows;
}

std::vector<GitRepoRow> GitRepoRepository::listEnabled() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT * FROM git_repos WHERE is_enabled = true ORDER BY name");
  txn.commit();

  std::vector<GitRepoRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapRow(row, false));
  }
  return vRows;
}

std::optional<GitRepoRow> GitRepoRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec_params("SELECT * FROM git_repos WHERE id = $1", iId);
  txn.commit();

  if (result.empty()) return std::nullopt;
  return mapRow(result[0], true);
}

std::optional<GitRepoRow> GitRepoRepository::findByName(const std::string& sName) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec_params("SELECT * FROM git_repos WHERE name = $1", sName);
  txn.commit();

  if (result.empty()) return std::nullopt;
  return mapRow(result[0], true);
}

void GitRepoRepository::update(int64_t iId, const std::string& sName,
                               const std::string& sRemoteUrl, const std::string& sAuthType,
                               const std::string& sPlaintextCredentials,
                               const std::string& sDefaultBranch,
                               const std::string& sLocalPath,
                               const std::string& sKnownHosts,
                               bool bIsEnabled) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  if (!sPlaintextCredentials.empty()) {
    std::string sEncrypted = _csService.encrypt(sPlaintextCredentials);
    txn.exec_params(
        "UPDATE git_repos SET name=$1, remote_url=$2, auth_type=$3, "
        "encrypted_credentials=$4, default_branch=$5, local_path=$6, "
        "known_hosts=$7, is_enabled=$8, updated_at=now() WHERE id=$9",
        sName, sRemoteUrl, sAuthType, sEncrypted, sDefaultBranch,
        sLocalPath.empty() ? std::optional<std::string>(std::nullopt) : sLocalPath,
        sKnownHosts.empty() ? std::optional<std::string>(std::nullopt) : sKnownHosts,
        bIsEnabled, iId);
  } else {
    txn.exec_params(
        "UPDATE git_repos SET name=$1, remote_url=$2, auth_type=$3, "
        "default_branch=$4, local_path=$5, known_hosts=$6, is_enabled=$7, "
        "updated_at=now() WHERE id=$8",
        sName, sRemoteUrl, sAuthType, sDefaultBranch,
        sLocalPath.empty() ? std::optional<std::string>(std::nullopt) : sLocalPath,
        sKnownHosts.empty() ? std::optional<std::string>(std::nullopt) : sKnownHosts,
        bIsEnabled, iId);
  }

  txn.commit();
}

void GitRepoRepository::updateSyncStatus(int64_t iId, const std::string& sStatus,
                                         const std::string& sError) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec_params(
      "UPDATE git_repos SET last_sync_at=now(), last_sync_status=$1, "
      "last_sync_error=$2 WHERE id=$3",
      sStatus,
      sError.empty() ? std::optional<std::string>(std::nullopt) : sError,
      iId);
  txn.commit();
}

void GitRepoRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec_params(
      "DELETE FROM git_repos WHERE id = $1 RETURNING id", iId);
  txn.commit();

  if (result.empty()) {
    throw dns::common::NotFoundError("GIT_REPO_NOT_FOUND",
                                     "Git repo " + std::to_string(iId) + " not found");
  }
}

}  // namespace dns::dal
```

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 4: Commit**

```bash
git add include/dal/GitRepoRepository.hpp src/dal/GitRepoRepository.cpp
git commit -m "feat(dal): add GitRepoRepository for multi-repo GitOps CRUD"
```

---

### Task 4: GitRepoRepository Tests

**Files:**
- Create: `tests/integration/test_git_repo_repository.cpp`

Follow the same pattern as `tests/integration/test_idp_repository.cpp` — skip if `DNS_DB_URL` is not set.

**Step 1: Write the test file**

Create `tests/integration/test_git_repo_repository.cpp`:

```cpp
#include "dal/GitRepoRepository.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <pqxx/pqxx>

#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include "security/CryptoService.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
const std::string kTestMasterKey =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
}  // namespace

class GitRepoRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _csService = std::make_unique<dns::security::CryptoService>(kTestMasterKey);
    _grRepo = std::make_unique<dns::dal::GitRepoRepository>(*_cpPool, *_csService);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("UPDATE zones SET git_repo_id = NULL");
    txn.exec("DELETE FROM git_repos");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::GitRepoRepository> _grRepo;
};

TEST_F(GitRepoRepositoryTest, CreateAndFindById) {
  std::string sCreds = R"({"private_key":"test-key","passphrase":""})";
  int64_t iId = _grRepo->create("test-repo", "git@github.com:org/repo.git",
                                "ssh", sCreds, "main", "", "github.com ssh-rsa AAAA...");
  EXPECT_GT(iId, 0);

  auto oRow = _grRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "test-repo");
  EXPECT_EQ(oRow->sRemoteUrl, "git@github.com:org/repo.git");
  EXPECT_EQ(oRow->sAuthType, "ssh");
  EXPECT_EQ(oRow->sDecryptedCredentials, sCreds);
  EXPECT_EQ(oRow->sDefaultBranch, "main");
  EXPECT_TRUE(oRow->bIsEnabled);
}

TEST_F(GitRepoRepositoryTest, ListAllDoesNotDecryptCredentials) {
  _grRepo->create("repo-a", "https://github.com/org/a.git", "https",
                  R"({"username":"user","token":"ghp_xxx"})", "main", "", "");
  _grRepo->create("repo-b", "git@github.com:org/b.git", "ssh",
                  R"({"private_key":"key","passphrase":""})", "develop", "", "");

  auto vRows = _grRepo->listAll();
  ASSERT_EQ(vRows.size(), 2u);
  EXPECT_TRUE(vRows[0].sDecryptedCredentials.empty());
  EXPECT_TRUE(vRows[1].sDecryptedCredentials.empty());
  EXPECT_EQ(vRows[0].sName, "repo-a");
  EXPECT_EQ(vRows[1].sName, "repo-b");
}

TEST_F(GitRepoRepositoryTest, ListEnabled) {
  int64_t iId1 = _grRepo->create("enabled-repo", "https://gh.com/a.git",
                                  "none", "", "main", "", "");
  int64_t iId2 = _grRepo->create("disabled-repo", "https://gh.com/b.git",
                                  "none", "", "main", "", "");
  _grRepo->update(iId2, "disabled-repo", "https://gh.com/b.git", "none", "",
                  "main", "", "", false);

  auto vEnabled = _grRepo->listEnabled();
  ASSERT_EQ(vEnabled.size(), 1u);
  EXPECT_EQ(vEnabled[0].iId, iId1);
}

TEST_F(GitRepoRepositoryTest, UpdateWithNewCredentials) {
  int64_t iId = _grRepo->create("update-repo", "https://gh.com/repo.git",
                                "https", R"({"username":"u","token":"old"})",
                                "main", "", "");
  _grRepo->update(iId, "update-repo", "https://gh.com/repo.git", "https",
                  R"({"username":"u","token":"new"})", "develop", "", "", true);

  auto oRow = _grRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sDecryptedCredentials, R"({"username":"u","token":"new"})");
  EXPECT_EQ(oRow->sDefaultBranch, "develop");
}

TEST_F(GitRepoRepositoryTest, UpdateWithoutChangingCredentials) {
  std::string sOrigCreds = R"({"private_key":"keep-me","passphrase":""})";
  int64_t iId = _grRepo->create("keep-creds-repo", "git@gh.com:x.git",
                                "ssh", sOrigCreds, "main", "", "");
  _grRepo->update(iId, "keep-creds-repo", "git@gh.com:x.git", "ssh",
                  "", "main", "", "", true);

  auto oRow = _grRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sDecryptedCredentials, sOrigCreds);
}

TEST_F(GitRepoRepositoryTest, UpdateSyncStatus) {
  int64_t iId = _grRepo->create("sync-repo", "https://gh.com/s.git",
                                "none", "", "main", "", "");
  _grRepo->updateSyncStatus(iId, "success");

  auto oRow = _grRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sLastSyncStatus, "success");
  EXPECT_FALSE(oRow->sLastSyncAt.empty());
  EXPECT_TRUE(oRow->sLastSyncError.empty());
}

TEST_F(GitRepoRepositoryTest, DeleteById) {
  int64_t iId = _grRepo->create("del-repo", "https://gh.com/d.git",
                                "none", "", "main", "", "");
  _grRepo->deleteById(iId);
  EXPECT_FALSE(_grRepo->findById(iId).has_value());
}

TEST_F(GitRepoRepositoryTest, DeleteNonExistentThrows) {
  EXPECT_THROW(_grRepo->deleteById(99999), dns::common::NotFoundError);
}

TEST_F(GitRepoRepositoryTest, FindByName) {
  _grRepo->create("named-repo", "https://gh.com/n.git", "none", "", "main", "", "");
  auto oRow = _grRepo->findByName("named-repo");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sRemoteUrl, "https://gh.com/n.git");
  EXPECT_FALSE(_grRepo->findByName("nonexistent").has_value());
}

TEST_F(GitRepoRepositoryTest, DuplicateNameThrows) {
  _grRepo->create("dup-repo", "https://gh.com/a.git", "none", "", "main", "", "");
  EXPECT_THROW(
      _grRepo->create("dup-repo", "https://gh.com/b.git", "none", "", "main", "", ""),
      std::exception);
}
```

**Step 2: Run the tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitRepoRepository*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP if no `DNS_DB_URL`).

**Step 3: Commit**

```bash
git add tests/integration/test_git_repo_repository.cpp
git commit -m "test(dal): add GitRepoRepository integration tests"
```

---

### Task 5: ZoneRepository Update

**Files:**
- Modify: `include/dal/ZoneRepository.hpp:14-24` (ZoneRow struct)
- Modify: `include/dal/ZoneRepository.hpp:34-36` (create signature)
- Modify: `include/dal/ZoneRepository.hpp:48-50` (update signature)
- Modify: `src/dal/ZoneRepository.cpp` (all SQL queries and mapRow)

Add `git_repo_id` and `git_branch` fields to `ZoneRow`, and accept them in `create()` and `update()`.

**Step 1: Update the ZoneRow struct**

In `include/dal/ZoneRepository.hpp`, add two new fields to `ZoneRow` before `tpCreatedAt`:

```cpp
std::optional<int64_t> oGitRepoId;      // FK to git_repos
std::optional<std::string> oGitBranch;   // Branch override (nullopt = use repo default)
```

**Step 2: Update create() and update() signatures**

Add optional parameters to the end of both functions:

```cpp
int64_t create(const std::string& sName, int64_t iViewId,
               std::optional<int> oRetention,
               bool bManageSoa = false, bool bManageNs = false,
               std::optional<int64_t> oGitRepoId = std::nullopt,
               std::optional<std::string> oGitBranch = std::nullopt);

void update(int64_t iId, const std::string& sName, int64_t iViewId,
            std::optional<int> oRetention, bool bManageSoa = false,
            bool bManageNs = false,
            std::optional<int64_t> oGitRepoId = std::nullopt,
            std::optional<std::string> oGitBranch = std::nullopt);
```

**Step 3: Update the implementation**

In `src/dal/ZoneRepository.cpp`:

1. Add to `mapRow` after existing fields:
```cpp
if (!row["git_repo_id"].is_null()) {
  zr.oGitRepoId = row["git_repo_id"].as<int64_t>();
}
if (!row["git_branch"].is_null()) {
  zr.oGitBranch = row["git_branch"].as<std::string>();
}
```

2. Update `create()` INSERT SQL to include `git_repo_id` and `git_branch` columns, passing `std::optional` values from the parameters.

3. Update `update()` UPDATE SQL similarly.

**Step 4: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build. Existing callers use defaults, so no changes needed elsewhere at this step.

**Step 5: Commit**

```bash
git add include/dal/ZoneRepository.hpp src/dal/ZoneRepository.cpp
git commit -m "feat(dal): add git_repo_id and git_branch to ZoneRepository"
```

---

### Task 6: ZoneRepository Tests

**Files:**
- Modify: `tests/integration/test_zone_repository.cpp`

**Step 1: Add tests for the new git fields**

Add to the existing test file:

```cpp
TEST_F(ZoneRepositoryTest, CreateWithGitRepoIdAndBranch) {
  // Create a git_repos row first
  int64_t iRepoId = 0;
  {
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("INSERT INTO git_repos (name, remote_url, auth_type, default_branch) "
             "VALUES ('test-repo', 'https://gh.com/r.git', 'none', 'main')");
    auto res = txn.exec("SELECT id FROM git_repos WHERE name = 'test-repo'");
    iRepoId = res[0][0].as<int64_t>();
    txn.commit();
  }

  int64_t iId = _zrRepo->create("git-zone.example.com", _iViewId, std::nullopt,
                                false, false, iRepoId, "production");

  auto oZone = _zrRepo->findById(iId);
  ASSERT_TRUE(oZone.has_value());
  ASSERT_TRUE(oZone->oGitRepoId.has_value());
  EXPECT_EQ(*oZone->oGitRepoId, iRepoId);
  ASSERT_TRUE(oZone->oGitBranch.has_value());
  EXPECT_EQ(*oZone->oGitBranch, "production");
}

TEST_F(ZoneRepositoryTest, CreateWithoutGitRepoHasNullFields) {
  int64_t iId = _zrRepo->create("no-git.example.com", _iViewId, std::nullopt);
  auto oZone = _zrRepo->findById(iId);
  ASSERT_TRUE(oZone.has_value());
  EXPECT_FALSE(oZone->oGitRepoId.has_value());
  EXPECT_FALSE(oZone->oGitBranch.has_value());
}
```

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='ZoneRepository*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_zone_repository.cpp
git commit -m "test(dal): add zone git_repo_id and git_branch integration tests"
```

---

### Task 7: GitRepoMirror

**Files:**
- Create: `include/gitops/GitRepoMirror.hpp`
- Create: `src/gitops/GitRepoMirror.cpp`

Extract per-repo git operations from `GitOpsMirror` (`include/gitops/GitOpsMirror.hpp`, `src/gitops/GitOpsMirror.cpp`) into a new `GitRepoMirror` class. Key differences from `GitOpsMirror`:

- Auth config via `GitRepoAuth` struct (in-memory keys from DB, not file paths)
- SSH credentials use `git_credential_ssh_key_memory_new` (key content, not file path)
- HTTPS credentials use `git_credential_userpass_plaintext_new`
- `commitSnapshot()` takes a branch parameter and does checkout → write → commit → push → checkout default
- No repository dependencies — only does file I/O and git operations
- Operations serialized via per-instance mutex

**Step 1: Create the header**

Create `include/gitops/GitRepoMirror.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

struct git_repository;
struct git_credential;
struct git_cert;

namespace dns::gitops {

/// Authentication configuration for a git repo mirror.
struct GitRepoAuth {
  std::string sAuthType;   // "ssh", "https", "none"
  std::string sPrivateKey; // SSH: PEM private key contents
  std::string sPassphrase; // SSH: key passphrase
  std::string sUsername;   // HTTPS: username
  std::string sToken;      // HTTPS: personal access token
  std::string sKnownHosts; // SSH: known_hosts content for host verification
};

/// Wraps a single git repository: clone/open, pull, branch checkout, commit, push.
/// All operations are serialized via an internal mutex.
/// Class abbreviation: grm
class GitRepoMirror {
 public:
  GitRepoMirror(int64_t iRepoId, const std::string& sName);
  ~GitRepoMirror();

  GitRepoMirror(const GitRepoMirror&) = delete;
  GitRepoMirror& operator=(const GitRepoMirror&) = delete;

  /// Clone or open existing repo at sLocalPath. Sets up auth config.
  void initialize(const std::string& sRemoteUrl, const std::string& sLocalPath,
                  const std::string& sDefaultBranch, const GitRepoAuth& auth);

  /// Write a snapshot file and commit+push on the specified branch.
  /// sRelativePath: path within repo (e.g., "view-name/zone.json")
  /// sBranch: branch to commit on (empty = repo default branch)
  void commitSnapshot(const std::string& sRelativePath, const std::string& sContent,
                      const std::string& sCommitMessage, const std::string& sBranch = "");

  /// Fetch latest from remote.
  void pull();

  int64_t repoId() const { return _iRepoId; }
  const std::string& name() const { return _sName; }

  /// libgit2 credentials callback.
  static int credentialsCb(git_credential** ppOut, const char* pUrl,
                           const char* pUsernameFromUrl, unsigned int iAllowedTypes,
                           void* pPayload);

  /// libgit2 certificate_check callback.
  static int certificateCheckCb(git_cert* pCert, int bValid,
                                const char* pHost, void* pPayload);

 private:
  void checkoutBranch(const std::string& sBranch);
  void gitAddCommitPush(const std::string& sMessage, const std::string& sBranch);
  void setupSshHome();

  int64_t _iRepoId;
  std::string _sName;
  std::string _sRemoteUrl;
  std::string _sLocalPath;
  std::string _sDefaultBranch;
  GitRepoAuth _auth;
  git_repository* _pRepo = nullptr;
  std::mutex _mtx;
};

}  // namespace dns::gitops
```

**Step 2: Create the implementation**

Create `src/gitops/GitRepoMirror.cpp`. This is extracted from `src/gitops/GitOpsMirror.cpp` with these modifications:

1. **SSH credentials callback** — uses `git_credential_ssh_key_memory_new()` (key content from memory, not file path) instead of `git_credential_ssh_key_new()`:
```cpp
if ((iAllowedTypes & GIT_CREDENTIAL_SSH_KEY) &&
    pSelf->_auth.sAuthType == "ssh" && !pSelf->_auth.sPrivateKey.empty()) {
  const char* pUsername = (pUsernameFromUrl && pUsernameFromUrl[0])
                              ? pUsernameFromUrl : "git";
  return git_credential_ssh_key_memory_new(
      ppOut, pUsername, nullptr,
      pSelf->_auth.sPrivateKey.c_str(),
      pSelf->_auth.sPassphrase.empty() ? nullptr : pSelf->_auth.sPassphrase.c_str());
}
```

2. **HTTPS credentials callback** — adds `git_credential_userpass_plaintext_new()`:
```cpp
if ((iAllowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT) &&
    pSelf->_auth.sAuthType == "https" && !pSelf->_auth.sToken.empty()) {
  return git_credential_userpass_plaintext_new(
      ppOut,
      pSelf->_auth.sUsername.empty() ? "oauth2" : pSelf->_auth.sUsername.c_str(),
      pSelf->_auth.sToken.c_str());
}
```

3. **Certificate check callback** — reads `sKnownHosts` from the auth struct (string content, not file path), using `std::istringstream` instead of `std::ifstream`.

4. **`commitSnapshot()`** — new method that accepts `sRelativePath`, `sContent`, `sCommitMessage`, and `sBranch`. Workflow: checkout branch → write file → `gitAddCommitPush()` → checkout default branch if we switched.

5. **`checkoutBranch()`** — new method for branch switching. Looks up `refs/heads/{branch}`, falls back to creating from `refs/remotes/origin/{branch}` or HEAD.

6. **`gitAddCommitPush()`** — same as `GitOpsMirror::gitAddCommitPush()` but pushes a branch-specific refspec: `refs/heads/{branch}:refs/heads/{branch}`.

Reference: `src/gitops/GitOpsMirror.cpp` for the full libgit2 boilerplate.

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build (`src/CMakeLists.txt` already GLOBs `src/gitops/*.cpp`).

**Step 4: Commit**

```bash
git add include/gitops/GitRepoMirror.hpp src/gitops/GitRepoMirror.cpp
git commit -m "feat(gitops): add GitRepoMirror for per-repo git operations"
```

---

### Task 8: GitRepoMirror Tests

**Files:**
- Create: `tests/unit/test_git_repo_mirror.cpp`

Unit tests for construction and local-only git operations (no remote).

**Step 1: Write the test file**

Create `tests/unit/test_git_repo_mirror.cpp`:

```cpp
#include "gitops/GitRepoMirror.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(GitRepoMirrorTest, ConstructorInitializesFields) {
  dns::gitops::GitRepoMirror grm(42, "test-mirror");
  EXPECT_EQ(grm.repoId(), 42);
  EXPECT_EQ(grm.name(), "test-mirror");
}

TEST(GitRepoMirrorTest, InitializeOpensExistingRepo) {
  auto sTmpDir = fs::temp_directory_path() / "grm-test-init";
  fs::remove_all(sTmpDir);
  fs::create_directories(sTmpDir);
  // Pre-create a git repo using git CLI
  std::system(("git init " + sTmpDir.string() + " 2>/dev/null").c_str());

  dns::gitops::GitRepoMirror grm(1, "local-test");
  dns::gitops::GitRepoAuth auth;
  auth.sAuthType = "none";

  EXPECT_NO_THROW(grm.initialize("", sTmpDir.string(), "main", auth));
  fs::remove_all(sTmpDir);
}

TEST(GitRepoMirrorTest, CommitSnapshotWritesFileLocally) {
  auto sTmpDir = fs::temp_directory_path() / "grm-test-commit";
  fs::remove_all(sTmpDir);
  fs::create_directories(sTmpDir);

  // Create git repo with initial commit so HEAD exists
  std::string sSetup =
      "cd " + sTmpDir.string() + " && "
      "git init 2>/dev/null && "
      "git checkout -b main 2>/dev/null && "
      "touch .gitkeep && git add . && "
      "git -c user.name=test -c user.email=t@t commit -m init 2>/dev/null";
  std::system(sSetup.c_str());

  dns::gitops::GitRepoMirror grm(1, "commit-test");
  dns::gitops::GitRepoAuth auth;
  auth.sAuthType = "none";
  grm.initialize("", sTmpDir.string(), "main", auth);

  std::string sContent = R"({"zone":"example.com","records":[]})";
  grm.commitSnapshot("default/example.com.json", sContent, "test commit");

  // Verify the file was written
  fs::path filePath = sTmpDir / "default" / "example.com.json";
  EXPECT_TRUE(fs::exists(filePath));

  std::ifstream ifs(filePath);
  std::string sRead((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
  EXPECT_EQ(sRead, sContent);

  fs::remove_all(sTmpDir);
}
```

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitRepoMirror*' 2>&1 | tail -20`
Expected: All tests PASS.

**Step 3: Commit**

```bash
git add tests/unit/test_git_repo_mirror.cpp
git commit -m "test(gitops): add GitRepoMirror unit tests"
```

---

### Task 9: GitRepoManager

**Files:**
- Create: `include/gitops/GitRepoManager.hpp`
- Create: `src/gitops/GitRepoManager.cpp`

Manages multiple `GitRepoMirror` instances. Loads enabled repos from DB on startup, provides hot-reload when admin adds/edits/removes repos, and routes commit calls by repo ID.

**Step 1: Create the header**

Create `include/gitops/GitRepoManager.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dns::dal {
class GitRepoRepository;
class ZoneRepository;
class ViewRepository;
class RecordRepository;
}  // namespace dns::dal

namespace dns::core {
class VariableEngine;
}

namespace dns::gitops {

class GitRepoMirror;

/// Manages multiple GitRepoMirror instances, one per enabled git_repos row.
/// Class abbreviation: grmgr
class GitRepoManager {
 public:
  GitRepoManager(dns::dal::GitRepoRepository& grRepo,
                 dns::dal::ZoneRepository& zrRepo,
                 dns::dal::ViewRepository& vrRepo,
                 dns::dal::RecordRepository& rrRepo,
                 dns::core::VariableEngine& veEngine,
                 const std::string& sBasePath);
  ~GitRepoManager();

  /// Load all enabled repos from DB and initialize mirrors. Called at startup.
  void initialize();

  /// Reload a single repo mirror (add, update, or remove). Called after admin CRUD.
  void reloadRepo(int64_t iRepoId);

  /// Remove a repo mirror (called after admin deletes a repo).
  void removeRepo(int64_t iRepoId);

  /// Commit a zone snapshot to its assigned git repo.
  /// Looks up zone's git_repo_id, builds the snapshot, and commits.
  /// No-op if the zone has no git_repo_id assigned.
  void commitZoneSnapshot(int64_t iZoneId, const std::string& sActor);

  /// Pull (fetch) latest for all enabled mirrors.
  void pullAll();

  /// Pull for a single repo by ID.
  void pullRepo(int64_t iRepoId);

  /// Test connection for a repo: clone to temp dir, verify auth works.
  /// Returns empty string on success, error message on failure.
  std::string testConnection(int64_t iRepoId);

  /// Build the zone snapshot JSON (same format as GitOpsMirror::buildSnapshotJson).
  std::string buildSnapshotJson(int64_t iZoneId, const std::string& sActor) const;

 private:
  GitRepoMirror* findMirror(int64_t iRepoId);

  dns::dal::GitRepoRepository& _grRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::core::VariableEngine& _veEngine;
  std::string _sBasePath;

  std::unordered_map<int64_t, std::unique_ptr<GitRepoMirror>> _mMirrors;
  std::mutex _mtx;
};

}  // namespace dns::gitops
```

**Step 2: Create the implementation**

Create `src/gitops/GitRepoManager.cpp`:

```cpp
#include "gitops/GitRepoManager.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "gitops/GitRepoMirror.hpp"

namespace dns::gitops {

GitRepoManager::GitRepoManager(dns::dal::GitRepoRepository& grRepo,
                               dns::dal::ZoneRepository& zrRepo,
                               dns::dal::ViewRepository& vrRepo,
                               dns::dal::RecordRepository& rrRepo,
                               dns::core::VariableEngine& veEngine,
                               const std::string& sBasePath)
    : _grRepo(grRepo), _zrRepo(zrRepo), _vrRepo(vrRepo),
      _rrRepo(rrRepo), _veEngine(veEngine), _sBasePath(sBasePath) {}

GitRepoManager::~GitRepoManager() = default;

namespace {

GitRepoAuth buildAuth(const dns::dal::GitRepoRow& row) {
  GitRepoAuth auth;
  auth.sAuthType = row.sAuthType;
  auth.sKnownHosts = row.sKnownHosts;

  if (!row.sDecryptedCredentials.empty()) {
    try {
      auto j = nlohmann::json::parse(row.sDecryptedCredentials);
      if (row.sAuthType == "ssh") {
        auth.sPrivateKey = j.value("private_key", "");
        auth.sPassphrase = j.value("passphrase", "");
      } else if (row.sAuthType == "https") {
        auth.sUsername = j.value("username", "");
        auth.sToken = j.value("token", "");
      }
    } catch (...) {
      // Malformed credentials JSON — leave auth fields empty
    }
  }

  return auth;
}

std::string computeLocalPath(const std::string& sBasePath, int64_t iRepoId,
                             const std::string& sConfiguredPath) {
  if (!sConfiguredPath.empty()) return sConfiguredPath;
  return sBasePath + "/" + std::to_string(iRepoId);
}

}  // namespace

void GitRepoManager::initialize() {
  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  auto vRepos = _grRepo.listEnabled();
  for (auto& repoRow : vRepos) {
    // listEnabled doesn't decrypt — need findById for credentials
    auto oRepo = _grRepo.findById(repoRow.iId);
    if (!oRepo) continue;

    auto sLocalPath = computeLocalPath(_sBasePath, oRepo->iId, oRepo->sLocalPath);
    auto auth = buildAuth(*oRepo);

    try {
      auto upMirror = std::make_unique<GitRepoMirror>(oRepo->iId, oRepo->sName);
      upMirror->initialize(oRepo->sRemoteUrl, sLocalPath, oRepo->sDefaultBranch, auth);
      upMirror->pull();
      _mMirrors[oRepo->iId] = std::move(upMirror);
      _grRepo.updateSyncStatus(oRepo->iId, "success");
      spLog->info("GitRepoManager: initialized repo '{}' (id={})", oRepo->sName, oRepo->iId);
    } catch (const std::exception& ex) {
      _grRepo.updateSyncStatus(oRepo->iId, "failed", ex.what());
      spLog->error("GitRepoManager: failed to initialize repo '{}': {}",
                   oRepo->sName, ex.what());
    }
  }

  spLog->info("GitRepoManager: initialized {} repo mirror(s)", _mMirrors.size());
}

void GitRepoManager::reloadRepo(int64_t iRepoId) {
  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  // Remove existing mirror if present
  _mMirrors.erase(iRepoId);

  auto oRepo = _grRepo.findById(iRepoId);
  if (!oRepo || !oRepo->bIsEnabled) {
    spLog->info("GitRepoManager: repo {} removed/disabled — mirror unloaded", iRepoId);
    return;
  }

  auto sLocalPath = computeLocalPath(_sBasePath, oRepo->iId, oRepo->sLocalPath);
  auto auth = buildAuth(*oRepo);

  try {
    auto upMirror = std::make_unique<GitRepoMirror>(oRepo->iId, oRepo->sName);
    upMirror->initialize(oRepo->sRemoteUrl, sLocalPath, oRepo->sDefaultBranch, auth);
    upMirror->pull();
    _mMirrors[oRepo->iId] = std::move(upMirror);
    _grRepo.updateSyncStatus(oRepo->iId, "success");
    spLog->info("GitRepoManager: reloaded repo '{}' (id={})", oRepo->sName, oRepo->iId);
  } catch (const std::exception& ex) {
    _grRepo.updateSyncStatus(oRepo->iId, "failed", ex.what());
    spLog->error("GitRepoManager: failed to reload repo '{}': {}",
                 oRepo->sName, ex.what());
  }
}

void GitRepoManager::removeRepo(int64_t iRepoId) {
  std::lock_guard lock(_mtx);
  _mMirrors.erase(iRepoId);
}

GitRepoMirror* GitRepoManager::findMirror(int64_t iRepoId) {
  auto it = _mMirrors.find(iRepoId);
  return (it != _mMirrors.end()) ? it->second.get() : nullptr;
}

std::string GitRepoManager::buildSnapshotJson(int64_t iZoneId,
                                              const std::string& sActor) const {
  // Same logic as GitOpsMirror::buildSnapshotJson — see src/gitops/GitOpsMirror.cpp:226
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  auto oView = _vrRepo.findById(oZone->iViewId);
  std::string sViewName = oView ? oView->sName : "unknown";

  auto vRecords = _rrRepo.listByZoneId(iZoneId);

  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& rec : vRecords) {
    std::string sExpandedValue;
    try {
      sExpandedValue = _veEngine.expand(rec.sValueTemplate, iZoneId);
    } catch (...) {
      sExpandedValue = rec.sValueTemplate;
    }
    jRecords.push_back({
        {"record_id", rec.iId},
        {"name", rec.sName},
        {"type", rec.sType},
        {"ttl", rec.iTtl},
        {"value_template", rec.sValueTemplate},
        {"value", sExpandedValue},
        {"priority", rec.iPriority},
    });
  }

  auto tpNow = std::chrono::system_clock::now();
  auto ttNow = std::chrono::system_clock::to_time_t(tpNow);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&ttNow), "%FT%TZ");

  nlohmann::json j = {
      {"zone", oZone->sName},
      {"view", sViewName},
      {"generated_at", oss.str()},
      {"generated_by", sActor},
      {"records", jRecords},
  };
  return j.dump(2);
}

void GitRepoManager::commitZoneSnapshot(int64_t iZoneId, const std::string& sActor) {
  auto spLog = common::Logger::get();

  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone || !oZone->oGitRepoId.has_value()) {
    // Zone has no git repo assigned — no-op
    return;
  }

  int64_t iRepoId = *oZone->oGitRepoId;
  GitRepoMirror* pMirror = nullptr;
  {
    std::lock_guard lock(_mtx);
    pMirror = findMirror(iRepoId);
  }

  if (!pMirror) {
    spLog->warn("GitRepoManager: repo {} not loaded — skipping commit for zone {}",
                iRepoId, iZoneId);
    return;
  }

  try {
    auto oView = _vrRepo.findById(oZone->iViewId);
    std::string sViewName = oView ? oView->sName : "unknown";

    std::string sRelPath = sViewName + "/" + oZone->sName + ".json";
    std::string sJson = buildSnapshotJson(iZoneId, sActor);
    std::string sBranch = oZone->oGitBranch.value_or("");
    std::string sMsg = "Update " + oZone->sName + " by " + sActor + " via API";

    pMirror->commitSnapshot(sRelPath, sJson, sMsg, sBranch);
    spLog->info("GitRepoManager: committed zone '{}' to repo {} branch '{}'",
                oZone->sName, iRepoId,
                sBranch.empty() ? "(default)" : sBranch);
  } catch (const std::exception& ex) {
    spLog->error("GitRepoManager: commit failed for zone {}: {}", iZoneId, ex.what());
  }
}

void GitRepoManager::pullAll() {
  std::lock_guard lock(_mtx);
  for (auto& [iId, upMirror] : _mMirrors) {
    try {
      upMirror->pull();
      _grRepo.updateSyncStatus(iId, "success");
    } catch (const std::exception& ex) {
      _grRepo.updateSyncStatus(iId, "failed", ex.what());
    }
  }
}

void GitRepoManager::pullRepo(int64_t iRepoId) {
  std::lock_guard lock(_mtx);
  auto* pMirror = findMirror(iRepoId);
  if (!pMirror) {
    throw common::NotFoundError("GIT_REPO_NOT_LOADED",
                                "Git repo " + std::to_string(iRepoId) + " is not loaded");
  }
  pMirror->pull();
  _grRepo.updateSyncStatus(iRepoId, "success");
}

std::string GitRepoManager::testConnection(int64_t iRepoId) {
  auto oRepo = _grRepo.findById(iRepoId);
  if (!oRepo) {
    return "Git repo not found";
  }

  auto auth = buildAuth(*oRepo);
  auto sTmpDir = std::filesystem::temp_directory_path() /
                 ("meridian-git-test-" + std::to_string(iRepoId));
  std::filesystem::remove_all(sTmpDir);

  try {
    GitRepoMirror testMirror(iRepoId, oRepo->sName + "-test");
    testMirror.initialize(oRepo->sRemoteUrl, sTmpDir.string(),
                         oRepo->sDefaultBranch, auth);
    std::filesystem::remove_all(sTmpDir);
    return "";  // Success
  } catch (const std::exception& ex) {
    std::filesystem::remove_all(sTmpDir);
    return ex.what();
  }
}

}  // namespace dns::gitops
```

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 4: Commit**

```bash
git add include/gitops/GitRepoManager.hpp src/gitops/GitRepoManager.cpp
git commit -m "feat(gitops): add GitRepoManager for multi-repo lifecycle management"
```

---

### Task 10: GitRepoManager Tests

**Files:**
- Create: `tests/integration/test_git_repo_manager.cpp`

Integration tests for the manager — requires `DNS_DB_URL`. Tests initialize, commit routing, and hot-reload.

**Step 1: Write the test file**

Create `tests/integration/test_git_repo_manager.cpp`. Test that:

1. `initialize()` loads enabled repos without crashing (using local-only repos)
2. `commitZoneSnapshot()` is a no-op when zone has no git_repo_id
3. `reloadRepo()` adds/removes mirrors correctly
4. `removeRepo()` removes the mirror from the map

Use local-only git repos (no remote URL) to avoid needing network access in tests. Create git repos via `git init` in temp directories.

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitRepoManager*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_git_repo_manager.cpp
git commit -m "test(gitops): add GitRepoManager integration tests"
```

---

### Task 11: DeploymentEngine Refactor

**Files:**
- Modify: `include/core/DeploymentEngine.hpp:23-24` (forward declarations)
- Modify: `include/core/DeploymentEngine.hpp:35-44` (constructor + member)
- Modify: `src/core/DeploymentEngine.cpp:21` (include)
- Modify: `src/core/DeploymentEngine.cpp:26-45` (constructor)
- Modify: `src/core/DeploymentEngine.cpp:272-275` (GitOps commit call)

Replace `GitOpsMirror*` with `GitRepoManager*` in `DeploymentEngine`.

**Step 1: Update the header**

In `include/core/DeploymentEngine.hpp`:

1. Change the forward declaration from `class GitOpsMirror;` to `class GitRepoManager;` in `namespace dns::gitops`
2. Change the constructor parameter from `dns::gitops::GitOpsMirror* pGitMirror` to `dns::gitops::GitRepoManager* pGitRepoManager`
3. Change the member from `dns::gitops::GitOpsMirror* _pGitMirror;` to `dns::gitops::GitRepoManager* _pGitRepoManager;`

**Step 2: Update the implementation**

In `src/core/DeploymentEngine.cpp`:

1. Change `#include "gitops/GitOpsMirror.hpp"` to `#include "gitops/GitRepoManager.hpp"`
2. Update constructor to accept and store `GitRepoManager*`
3. Change the GitOps commit call (around line 272-275) from:
```cpp
if (_pGitMirror) {
  _pGitMirror->commit(iZoneId, sActor);
}
```
to:
```cpp
if (_pGitRepoManager) {
  _pGitRepoManager->commitZoneSnapshot(iZoneId, sActor);
}
```

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -10`
Expected: Successful build (main.cpp will need updating in Task 20, but the parameter name change should compile since it's a pointer).

**Step 4: Commit**

```bash
git add include/core/DeploymentEngine.hpp src/core/DeploymentEngine.cpp
git commit -m "refactor(core): replace GitOpsMirror with GitRepoManager in DeploymentEngine"
```

---

### Task 12: DeploymentEngine Tests

**Files:**
- Modify: `tests/integration/test_deployment_pipeline.cpp`

Update existing deployment tests to pass `nullptr` for `GitRepoManager` (same as current `GitOpsMirror` nullptr pattern).

**Step 1: Update test construction**

If the test file constructs `DeploymentEngine` directly, update the `GitOpsMirror*` nullptr to match the new `GitRepoManager*` parameter name. The type hasn't changed (still a pointer), so this should already compile. Verify no test references `GitOpsMirror` directly.

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='DeploymentPipeline*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_deployment_pipeline.cpp
git commit -m "test(core): update DeploymentEngine tests for GitRepoManager"
```

---

### Task 13: Env Var Migration

**Files:**
- Create: `src/gitops/GitOpsMigration.cpp`
- Create: `include/gitops/GitOpsMigration.hpp`

Auto-migrate `DNS_GIT_REMOTE_URL` to a `git_repos` row on first run after upgrade. If `DNS_GIT_REMOTE_URL` is set and no git_repos rows exist yet:

1. Read the SSH key file contents (from `DNS_GIT_SSH_KEY_PATH`)
2. Create a `git_repos` row with the env var values
3. Assign all existing zones to this repo
4. Log a deprecation notice

**Step 1: Create the header**

Create `include/gitops/GitOpsMigration.hpp`:

```cpp
#pragma once

#include <string>

namespace dns::dal {
class GitRepoRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::security {
class CryptoService;
}

namespace dns::gitops {

/// One-time migration from DNS_GIT_REMOTE_URL env var to git_repos table.
/// Called during startup after DB migrations.
class GitOpsMigration {
 public:
  /// Check if legacy env vars are set and no git_repos exist.
  /// If so, create a git_repos row and assign all zones to it.
  /// Returns true if migration was performed.
  static bool migrateIfNeeded(dns::dal::GitRepoRepository& grRepo,
                              dns::dal::ZoneRepository& zrRepo);
};

}  // namespace dns::gitops
```

**Step 2: Create the implementation**

Create `src/gitops/GitOpsMigration.cpp`:

```cpp
#include "gitops/GitOpsMigration.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/Logger.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace dns::gitops {

bool GitOpsMigration::migrateIfNeeded(dns::dal::GitRepoRepository& grRepo,
                                      dns::dal::ZoneRepository& zrRepo) {
  auto spLog = common::Logger::get();

  // Check if legacy env var is set
  const char* pRemoteUrl = std::getenv("DNS_GIT_REMOTE_URL");
  if (!pRemoteUrl || std::string(pRemoteUrl).empty()) {
    return false;
  }

  // Check if any git_repos already exist (migration already done)
  auto vExisting = grRepo.listAll();
  if (!vExisting.empty()) {
    spLog->info("GitOpsMigration: git_repos table already populated — "
                "ignoring DNS_GIT_REMOTE_URL env var");
    return false;
  }

  std::string sRemoteUrl(pRemoteUrl);
  spLog->info("GitOpsMigration: migrating DNS_GIT_REMOTE_URL='{}' to git_repos table",
              sRemoteUrl);

  // Read SSH key file if configured
  std::string sAuthType = "none";
  std::string sCredentials;
  std::string sKnownHosts;

  const char* pSshKeyPath = std::getenv("DNS_GIT_SSH_KEY_PATH");
  if (pSshKeyPath && std::string(pSshKeyPath).length() > 0) {
    sAuthType = "ssh";
    std::ifstream ifs(pSshKeyPath);
    if (ifs.is_open()) {
      std::ostringstream oss;
      oss << ifs.rdbuf();
      nlohmann::json jCreds = {
          {"private_key", oss.str()},
          {"passphrase", ""},
      };
      sCredentials = jCreds.dump();
      spLog->info("GitOpsMigration: read SSH key from '{}'", pSshKeyPath);
    } else {
      spLog->warn("GitOpsMigration: could not read SSH key from '{}'", pSshKeyPath);
    }
  }

  const char* pKnownHosts = std::getenv("DNS_GIT_KNOWN_HOSTS_FILE");
  if (pKnownHosts && std::string(pKnownHosts).length() > 0) {
    std::ifstream ifs(pKnownHosts);
    if (ifs.is_open()) {
      std::ostringstream oss;
      oss << ifs.rdbuf();
      sKnownHosts = oss.str();
    }
  }

  // Determine default branch
  std::string sDefaultBranch = "main";

  // Determine local path from existing env var
  const char* pLocalPath = std::getenv("DNS_GIT_LOCAL_PATH");
  std::string sLocalPath = pLocalPath ? std::string(pLocalPath) : "";

  // Create the git_repos row
  int64_t iRepoId = grRepo.create("default", sRemoteUrl, sAuthType,
                                  sCredentials, sDefaultBranch, sLocalPath, sKnownHosts);
  spLog->info("GitOpsMigration: created git_repos row id={}", iRepoId);

  // Assign all existing zones to this repo
  auto vZones = zrRepo.listAll();
  for (const auto& zone : vZones) {
    zrRepo.update(zone.iId, zone.sName, zone.iViewId, zone.oDeploymentRetention,
                  zone.bManageSoa, zone.bManageNs, iRepoId, std::nullopt);
  }
  spLog->info("GitOpsMigration: assigned {} existing zone(s) to repo '{}'",
              vZones.size(), "default");

  spLog->warn("GitOpsMigration: DNS_GIT_REMOTE_URL env var is DEPRECATED. "
              "Git repos are now managed via the admin UI. "
              "The env var will be ignored on subsequent starts.");

  return true;
}

}  // namespace dns::gitops
```

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 4: Commit**

```bash
git add include/gitops/GitOpsMigration.hpp src/gitops/GitOpsMigration.cpp
git commit -m "feat(gitops): add env var migration from DNS_GIT_REMOTE_URL to git_repos"
```

---

### Task 14: Env Var Migration Tests

**Files:**
- Create: `tests/integration/test_gitops_migration.cpp`

**Step 1: Write the test file**

Test that `migrateIfNeeded()`:
1. Returns `false` when `DNS_GIT_REMOTE_URL` is not set
2. Returns `false` when git_repos already exist
3. Creates a repo row and assigns zones when env var is set and table is empty

Use `setenv()`/`unsetenv()` to manipulate env vars in tests.

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitOpsMigration*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_gitops_migration.cpp
git commit -m "test(gitops): add env var migration integration tests"
```

---

### Task 15: RequestValidator Update

**Files:**
- Modify: `include/api/RequestValidator.hpp:28-29`
- Modify: `src/api/RequestValidator.cpp`

Add validation methods for git repo inputs.

**Step 1: Add new validation methods to the header**

In `include/api/RequestValidator.hpp`, add:

```cpp
static void validateGitRepoName(const std::string& sName);
static void validateGitRemoteUrl(const std::string& sUrl);
static void validateGitAuthType(const std::string& sAuthType);
static void validateGitBranch(const std::string& sBranch);
```

**Step 2: Implement the validators**

In `src/api/RequestValidator.cpp`, add:

```cpp
void RequestValidator::validateGitRepoName(const std::string& sName) {
  validateRequired(sName, "name");
  validateStringLength(sName, "name", 100);
}

void RequestValidator::validateGitRemoteUrl(const std::string& sUrl) {
  validateRequired(sUrl, "remote_url");
  validateStringLength(sUrl, "remote_url", 500);
  // Must start with git@, ssh://, https://, http://, or file://
  if (sUrl.rfind("git@", 0) != 0 && sUrl.rfind("ssh://", 0) != 0 &&
      sUrl.rfind("https://", 0) != 0 && sUrl.rfind("http://", 0) != 0 &&
      sUrl.rfind("file://", 0) != 0) {
    throw dns::common::ValidationError(
        "INVALID_REMOTE_URL",
        "remote_url must start with git@, ssh://, https://, http://, or file://");
  }
}

void RequestValidator::validateGitAuthType(const std::string& sAuthType) {
  if (sAuthType != "ssh" && sAuthType != "https" && sAuthType != "none") {
    throw dns::common::ValidationError(
        "INVALID_AUTH_TYPE", "auth_type must be 'ssh', 'https', or 'none'");
  }
}

void RequestValidator::validateGitBranch(const std::string& sBranch) {
  if (sBranch.empty()) return;
  validateStringLength(sBranch, "branch", 100);
  // Branch names can't start with - or contain ..
  if (sBranch[0] == '-' || sBranch.find("..") != std::string::npos) {
    throw dns::common::ValidationError(
        "INVALID_BRANCH", "Invalid git branch name");
  }
}
```

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 4: Commit**

```bash
git add include/api/RequestValidator.hpp src/api/RequestValidator.cpp
git commit -m "feat(api): add git repo input validation methods"
```

---

### Task 16: GitRepoRoutes

**Files:**
- Create: `include/api/routes/GitRepoRoutes.hpp`
- Create: `src/api/routes/GitRepoRoutes.cpp`

CRUD endpoints + test-connection + manual-sync for git repos. Follows the same pattern as `IdpRoutes` (`include/api/routes/IdpRoutes.hpp`).

**Step 1: Create the header**

Create `include/api/routes/GitRepoRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::api {
class AuthMiddleware;
}
namespace dns::dal {
class GitRepoRepository;
}
namespace dns::gitops {
class GitRepoManager;
}

namespace dns::api::routes {

/// Handlers for /api/v1/git-repos
class GitRepoRoutes {
 public:
  GitRepoRoutes(dns::dal::GitRepoRepository& grRepo,
                const dns::api::AuthMiddleware& amMiddleware,
                dns::gitops::GitRepoManager& grmManager);
  ~GitRepoRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::GitRepoRepository& _grRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::gitops::GitRepoManager& _grmManager;
};

}  // namespace dns::api::routes
```

**Step 2: Create the implementation**

Create `src/api/routes/GitRepoRoutes.cpp`. Endpoints:

| Method | Path | Permission | Description |
|--------|------|-----------|-------------|
| `GET` | `/api/v1/git-repos` | `repos.view` | List all repos |
| `POST` | `/api/v1/git-repos` | `repos.create` | Create repo → `reloadRepo()` |
| `GET` | `/api/v1/git-repos/<int>` | `repos.view` | Get repo (credentials masked) |
| `PUT` | `/api/v1/git-repos/<int>` | `repos.edit` | Update repo → `reloadRepo()` |
| `DELETE` | `/api/v1/git-repos/<int>` | `repos.delete` | Delete repo → `removeRepo()` |
| `POST` | `/api/v1/git-repos/<int>/test` | `repos.edit` | Test connection |
| `POST` | `/api/v1/git-repos/<int>/sync` | `repos.edit` | Manual pull |

Key implementation notes:
- Use `Permissions::kReposView`, `kReposCreate`, `kReposEdit`, `kReposDelete` from `include/common/Permissions.hpp`
- On create/update, validate with `RequestValidator::validateGitRepoName()`, `validateGitRemoteUrl()`, `validateGitAuthType()`
- After create/update, call `_grmManager.reloadRepo(iId)` for hot-reload
- After delete, call `_grmManager.removeRepo(iId)`
- For `GET /{id}`, mask credentials in response (return `has_credentials: true/false` instead of decrypted content)
- For test-connection, call `_grmManager.testConnection(iId)` and return success/error
- For manual sync, call `_grmManager.pullRepo(iId)`

Follow the same JSON response patterns as `src/api/routes/IdpRoutes.cpp` (200 with list/object, 201 with `{id, name}` on create, 204 on delete, etc.).

**Step 3: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 4: Commit**

```bash
git add include/api/routes/GitRepoRoutes.hpp src/api/routes/GitRepoRoutes.cpp
git commit -m "feat(api): add GitRepoRoutes for git repo CRUD, test, and sync"
```

---

### Task 17: GitRepoRoutes Tests

**Files:**
- Create: `tests/integration/test_git_repo_routes.cpp`

Integration tests for the API endpoints. Follow the pattern from `tests/integration/test_settings_routes.cpp` or `tests/integration/test_crud_routes.cpp`.

**Step 1: Write tests for each endpoint**

Test:
1. `GET /api/v1/git-repos` — returns empty list, then list after create
2. `POST /api/v1/git-repos` — create with valid/invalid data
3. `GET /api/v1/git-repos/{id}` — returns repo, credentials masked
4. `PUT /api/v1/git-repos/{id}` — update fields
5. `DELETE /api/v1/git-repos/{id}` — delete existing, 404 on missing
6. `POST /api/v1/git-repos/{id}/test` — test connection
7. `POST /api/v1/git-repos/{id}/sync` — manual sync
8. Permission checks — verify `repos.view`, `repos.create`, `repos.edit`, `repos.delete` are enforced

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitRepoRoutes*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_git_repo_routes.cpp
git commit -m "test(api): add GitRepoRoutes integration tests"
```

---

### Task 18: ZoneRoutes Update

**Files:**
- Modify: `src/api/routes/ZoneRoutes.cpp`

Accept `git_repo_id` and `git_branch` in zone create and update endpoints.

**Step 1: Update the POST handler**

In the `POST /api/v1/zones` handler, read optional `git_repo_id` (integer or null) and `git_branch` (string or null) from the JSON body. Pass to `ZoneRepository::create()`.

```cpp
std::optional<int64_t> oGitRepoId;
if (jBody.contains("git_repo_id") && !jBody["git_repo_id"].is_null()) {
  oGitRepoId = jBody["git_repo_id"].get<int64_t>();
}
std::optional<std::string> oGitBranch;
if (jBody.contains("git_branch") && !jBody["git_branch"].is_null()) {
  oGitBranch = jBody["git_branch"].get<std::string>();
  RequestValidator::validateGitBranch(*oGitBranch);
}
```

**Step 2: Update the PUT handler**

Same pattern for the `PUT /api/v1/zones/<int>` handler. Pass git fields to `ZoneRepository::update()`.

**Step 3: Update the GET responses**

In both list and get-by-id responses, include `git_repo_id` and `git_branch` fields in the JSON output:

```cpp
jZone["git_repo_id"] = zone.oGitRepoId.has_value() ? nlohmann::json(*zone.oGitRepoId)
                                                    : nlohmann::json(nullptr);
jZone["git_branch"] = zone.oGitBranch.has_value() ? nlohmann::json(*zone.oGitBranch)
                                                  : nlohmann::json(nullptr);
```

**Step 4: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -5`
Expected: Successful build.

**Step 5: Commit**

```bash
git add src/api/routes/ZoneRoutes.cpp
git commit -m "feat(api): accept git_repo_id and git_branch in zone CRUD endpoints"
```

---

### Task 19: ZoneRoutes Tests

**Files:**
- Modify: `tests/integration/test_crud_routes.cpp`

Add tests verifying zones can be created/updated with `git_repo_id` and `git_branch`, and that these fields appear in GET responses.

**Step 1: Add new test cases**

```cpp
TEST_F(CrudRoutesTest, ZoneCreateWithGitRepoId) {
  // First create a git_repos row
  // Then create a zone referencing it
  // Verify the zone GET response includes git_repo_id and git_branch
}

TEST_F(CrudRoutesTest, ZoneUpdateGitBranch) {
  // Create zone, then update git_branch
  // Verify the change persists
}
```

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter='CrudRoutes*Zone*' 2>&1 | tail -20`
Expected: All tests PASS (or SKIP).

**Step 3: Commit**

```bash
git add tests/integration/test_crud_routes.cpp
git commit -m "test(api): add zone git_repo_id/git_branch CRUD tests"
```

---

### Task 20: main.cpp Wiring

**Files:**
- Modify: `src/main.cpp` (multiple sections)

Replace the `GitOpsMirror` singleton setup with `GitRepoManager` initialization.

**Step 1: Update includes**

Replace:
```cpp
#include "gitops/GitOpsMirror.hpp"
```
With:
```cpp
#include "gitops/GitRepoManager.hpp"
#include "gitops/GitOpsMigration.hpp"
#include "dal/GitRepoRepository.hpp"
```

**Step 2: Add GitRepoRepository construction**

After the other repository constructions in Step 7a (around line 291), add:

```cpp
auto gitRepoRepo = std::make_unique<dns::dal::GitRepoRepository>(*cpPool, *csService);
```

**Step 3: Replace GitOpsMirror with GitRepoManager**

Replace the Step 6 block (lines 264-378) which currently does:
```cpp
std::unique_ptr<dns::gitops::GitOpsMirror> upGitMirror;
// ... GitOpsMirror initialization using env vars ...
```

With:
```cpp
// ── Step 6a: Migrate legacy env vars to git_repos table ──────────────
dns::gitops::GitOpsMigration::migrateIfNeeded(*gitRepoRepo, *zrRepo);

// ── Step 6b: Initialize GitRepoManager ───────────────────────────────
std::string sGitBasePath = settingsRepo->getValue("gitops.base_path",
                                                  "/var/meridian-dns/repos");
auto upGitRepoManager = std::make_unique<dns::gitops::GitRepoManager>(
    *gitRepoRepo, *zrRepo, *vrRepo, *rrRepo, *veEngine, sGitBasePath);
upGitRepoManager->initialize();
spLog->info("Step 6: GitRepoManager initialized");
```

**Step 4: Update DeploymentEngine construction**

Change the `DeploymentEngine` construction (line 380) from:
```cpp
auto depEngine = std::make_unique<dns::core::DeploymentEngine>(
    *deEngine, *veEngine, *zrRepo, *vrRepo, *rrRepo, *prRepo,
    *drRepo, *arRepo, upGitMirror.get(), cfgApp.iDeploymentRetentionCount);
```
To:
```cpp
auto depEngine = std::make_unique<dns::core::DeploymentEngine>(
    *deEngine, *veEngine, *zrRepo, *vrRepo, *rrRepo, *prRepo,
    *drRepo, *arRepo, upGitRepoManager.get(), cfgApp.iDeploymentRetentionCount);
```

**Step 5: Add GitRepoRoutes construction and registration**

After the other routes construction (around line 444), add:

```cpp
auto gitRepoRoutes = std::make_unique<dns::api::routes::GitRepoRoutes>(
    *gitRepoRepo, *amMiddleware, *upGitRepoManager);
```

After the other `registerRoutes()` calls (around line 465), add:

```cpp
gitRepoRoutes->registerRoutes(crowApp);
```

**Step 6: Add `#include "api/routes/GitRepoRoutes.hpp"` to the top of the file**

**Step 7: Remove unused Config GitOps fields**

Remove or leave as deprecated the `oGitRemoteUrl`, `sGitLocalPath`, `oGitSshKeyPath`, `oGitKnownHostsFile` fields from `Config`. These are only read by `GitOpsMigration` now via `std::getenv()` directly.

**Step 8: Verify build**

Run: `cmake --build build --parallel 2>&1 | tail -10`
Expected: Successful build.

**Step 9: Commit**

```bash
git add src/main.cpp include/common/Config.hpp
git commit -m "feat(main): wire GitRepoManager, migration, and GitRepoRoutes"
```

---

### Task 21: UI — Git Repos API Client

**Files:**
- Create: `ui/src/api/gitRepos.ts`

TypeScript API module for git repo endpoints. Follow the pattern from `ui/src/api/identityProviders.ts`.

**Step 1: Create the API module**

Create `ui/src/api/gitRepos.ts`:

```typescript
import { get, post, put, del } from './client'
import type { GitRepo, GitRepoCreate, GitRepoUpdate, GitRepoTestResult } from '../types'

export function listGitRepos(): Promise<GitRepo[]> {
  return get('/git-repos')
}

export function getGitRepo(id: number): Promise<GitRepo> {
  return get(`/git-repos/${id}`)
}

export function createGitRepo(data: GitRepoCreate): Promise<{ id: number; name: string }> {
  return post('/git-repos', data)
}

export function updateGitRepo(id: number, data: GitRepoUpdate): Promise<{ message: string }> {
  return put(`/git-repos/${id}`, data)
}

export function deleteGitRepo(id: number): Promise<void> {
  return del(`/git-repos/${id}`)
}

export function testGitRepoConnection(id: number): Promise<GitRepoTestResult> {
  return post(`/git-repos/${id}/test`)
}

export function syncGitRepo(id: number): Promise<{ message: string }> {
  return post(`/git-repos/${id}/sync`)
}
```

**Step 2: Commit**

```bash
git add ui/src/api/gitRepos.ts
git commit -m "feat(ui): add git repos API client module"
```

---

### Task 22: UI — Types

**Files:**
- Modify: `ui/src/types/index.ts`

Add TypeScript interfaces for git repo entities.

**Step 1: Add interfaces**

Add to `ui/src/types/index.ts`:

```typescript
export interface GitRepo {
  id: number
  name: string
  remote_url: string
  auth_type: 'ssh' | 'https' | 'none'
  has_credentials: boolean
  default_branch: string
  local_path: string
  known_hosts: string
  is_enabled: boolean
  last_sync_at: string | null
  last_sync_status: string | null
  last_sync_error: string | null
  created_at: string
  updated_at: string
}

export interface GitRepoCreate {
  name: string
  remote_url: string
  auth_type: 'ssh' | 'https' | 'none'
  credentials?: string
  default_branch?: string
  local_path?: string
  known_hosts?: string
}

export interface GitRepoUpdate {
  name: string
  remote_url: string
  auth_type: 'ssh' | 'https' | 'none'
  credentials?: string
  default_branch?: string
  local_path?: string
  known_hosts?: string
  is_enabled: boolean
}

export interface GitRepoTestResult {
  success: boolean
  message: string
}
```

**Step 2: Update Zone and ZoneCreate interfaces**

Add git fields to the existing `Zone` interface:

```typescript
export interface Zone {
  // ... existing fields ...
  git_repo_id: number | null
  git_branch: string | null
}

export interface ZoneCreate {
  // ... existing fields ...
  git_repo_id?: number | null
  git_branch?: string | null
}
```

**Step 3: Commit**

```bash
git add ui/src/types/index.ts
git commit -m "feat(ui): add GitRepo TypeScript interfaces and update Zone types"
```

---

### Task 23: UI — GitReposView Page

**Files:**
- Create: `ui/src/views/GitReposView.vue`

DataTable CRUD with drawer form. Follow the pattern from `ui/src/views/IdentityProvidersView.vue`.

**Step 1: Create the view component**

Create `ui/src/views/GitReposView.vue` with:

- **DataTable** listing all repos with columns: Name, Remote URL, Auth Type, Default Branch, Enabled (toggle), Last Sync Status (tag), Actions (edit/delete)
- **Dialog form** with fields:
  - Name (InputText)
  - Remote URL (InputText)
  - Auth Type (Select: SSH/HTTPS/None)
  - Credentials (conditional):
    - SSH: Private Key (Textarea), Passphrase (Password)
    - HTTPS: Username (InputText), Token (Password)
  - Default Branch (InputText, default "main")
  - Known Hosts (Textarea, shown only for SSH)
  - Enabled (ToggleSwitch)
- **Action buttons**: Test Connection, Save, Cancel
- **Status indicators**: Last sync at timestamp, success/failed tag with error tooltip
- **Test connection button**: calls `testGitRepoConnection()`, shows success/error message

Use the same component imports and composables as `IdentityProvidersView.vue` (`useConfirmAction`, `useNotificationStore`, `PageHeader`).

**Step 2: Commit**

```bash
git add ui/src/views/GitReposView.vue
git commit -m "feat(ui): add GitReposView page with DataTable CRUD"
```

---

### Task 24: UI — Zone Form Update

**Files:**
- Modify: `ui/src/views/ZonesView.vue` (zone create/edit dialog)
- Modify: `ui/src/views/ZoneDetailView.vue` (zone detail display)
- Modify: `ui/src/api/zones.ts` (create/update payloads)

Add git repo dropdown and branch override field to the zone form.

**Step 1: Update zone API calls**

In `ui/src/api/zones.ts`, update the `createZone()` and `updateZone()` function signatures to include `git_repo_id` and `git_branch` in the request body.

**Step 2: Update ZonesView.vue**

In the zone create/edit dialog:
1. Add a `Select` dropdown for git repo (populated from `listGitRepos()`)
2. Add an `InputText` for branch override (placeholder: "Use repo default")
3. Pass these fields in the create/update API calls

```typescript
import { listGitRepos } from '../api/gitRepos'
import type { GitRepo } from '../types'

const gitRepos = ref<GitRepo[]>([])

onMounted(async () => {
  gitRepos.value = await listGitRepos()
})
```

**Step 3: Update ZoneDetailView.vue**

Display the assigned git repo name and branch in the zone detail view header or info section. Show "No GitOps repo" if `git_repo_id` is null.

**Step 4: Commit**

```bash
git add ui/src/views/ZonesView.vue ui/src/views/ZoneDetailView.vue ui/src/api/zones.ts
git commit -m "feat(ui): add git repo selector and branch override to zone form"
```

---

### Task 25: UI — Routing + Sidebar

**Files:**
- Modify: `ui/src/router/index.ts`
- Modify: `ui/src/components/layout/AppSidebar.vue`

Add the git repos page to navigation.

**Step 1: Add the route**

In `ui/src/router/index.ts`, add to the children array (after the "Variables" route):

```typescript
{
  path: 'git-repos',
  name: 'git-repos',
  component: () => import('../views/GitReposView.vue'),
},
```

**Step 2: Add sidebar nav item**

In `ui/src/components/layout/AppSidebar.vue`, add to the `mainNavItems` array after "Variables":

```typescript
{ label: 'Git Repos', icon: 'pi pi-github', to: '/git-repos' },
```

Note: If `pi-github` is not available in PrimeIcons, use `pi pi-bookmark` or `pi pi-link` as a fallback.

**Step 3: Verify the UI**

Run: `cd ui && npm run build`
Expected: Successful build.

**Step 4: Commit**

```bash
git add ui/src/router/index.ts ui/src/components/layout/AppSidebar.vue
git commit -m "feat(ui): add git repos route and sidebar navigation"
```

---

### Task 26: Full Verification Pass

**Files:** None (verification only)

**Step 1: Full C++ build**

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
Expected: Clean build with zero errors.

**Step 2: Run all tests**

Run: `build/tests/dns-tests 2>&1 | tail -30`
Expected: All tests pass (or skip for DB tests without `DNS_DB_URL`).

**Step 3: Run DB integration tests (if `DNS_DB_URL` available)**

Run: `DNS_DB_URL="..." build/tests/dns-tests --gtest_filter='GitRepo*:GitOpsMigration*:ZoneRepository*GitRepo*' 2>&1`
Expected: All git-related tests pass.

**Step 4: UI build**

Run: `cd ui && npm run build`
Expected: Successful production build.

**Step 5: Review the old GitOpsMirror status**

The old `GitOpsMirror` class (`include/gitops/GitOpsMirror.hpp`, `src/gitops/GitOpsMirror.cpp`) is no longer used by `main.cpp` or `DeploymentEngine`. It can be left in place as dead code for now (tests still reference it in `test_gitops_mirror.cpp`). A cleanup commit can remove it and update the test to use `GitRepoMirror` instead.

**Step 6: Commit cleanup (optional)**

```bash
git rm include/gitops/GitOpsMirror.hpp src/gitops/GitOpsMirror.cpp
# Update tests/integration/test_gitops_mirror.cpp to use GitRepoMirror or remove
git commit -m "chore: remove deprecated GitOpsMirror singleton"
```

**Step 7: Final commit**

```bash
git add -A
git commit -m "feat: workstream 5 complete — multi-repo GitOps"
```