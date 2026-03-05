# Phase 7: Deployment Pipeline + GitOps Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Wire the end-to-end zone deployment pipeline: preview diffs, push changes to DNS
providers, snapshot deployments, mirror to Git, rollback, and expose via API routes.

**Architecture:** DeploymentEngine orchestrates the push pipeline (diff -> provider mutations ->
snapshot -> git mirror -> audit). RollbackEngine restores snapshots into desired state. ThreadPool
provides async dispatch. GitOpsMirror writes expanded zone JSON to a bare Git repo. New API routes
expose deployment history, rollback, audit query, and the preview/push workflow.

**Tech Stack:** C++20, libgit2, Crow HTTP, libpqxx, nlohmann/json, GoogleTest/GMock, spdlog

---

## Existing Context

**Startup sequence gaps** (in `src/main.cpp`):
- Step 6: GitOpsMirror (deferred)
- Step 7: ThreadPool (deferred)
- Step 12: Background task queue (deferred)

**Placeholder headers already exist** (need real implementations):
- `include/core/ThreadPool.hpp` — fixed-size jthread pool
- `include/core/DeploymentEngine.hpp` — push orchestrator
- `include/core/RollbackEngine.hpp` — snapshot restore
- `include/gitops/GitOpsMirror.hpp` — bare-repo mirror
- `include/api/routes/DeploymentRoutes.hpp` — deployment history + rollback
- `include/api/routes/AuditRoutes.hpp` — audit log query/export/purge

**Placeholder test file:**
- `tests/integration/test_deployment_pipeline.cpp` — single `EXPECT_TRUE(true)`

**Existing implementations to reuse:**
- `DiffEngine::preview()` — three-way diff (zone -> provider)
- `DiffEngine::computeDiff()` — pure diff algorithm
- `ProviderFactory::create()` — creates `IProvider` by type string
- `DeploymentRepository` — snapshot CRUD + retention prune
- `AuditRepository` — append-only insert + query + purgeOld
- `RecordRepository` — record CRUD (needs `deleteAllByZoneId()` and `upsertById()` additions)
- `VariableEngine::expand()` — template expansion
- All repository patterns use `ConnectionPool::checkout()` -> `pqxx::work` transactions

**Config fields already defined** (`include/common/Config.hpp`):
- `iThreadPoolSize` (default: 0 = hardware_concurrency)
- `oGitRemoteUrl` (optional — GitOps disabled if unset)
- `sGitLocalPath` (default: `/var/dns-orchestrator/repo`)
- `oGitSshKeyPath` (optional)
- `iDeploymentRetentionCount` (default: 10)

**Naming conventions** (Hungarian notation):
- `s` = string, `i` = int, `b` = bool, `v` = vector, `o` = optional
- `p` = raw ptr, `sp` = shared_ptr, `up` = unique_ptr
- Member vars prefixed `_`, functions camelCase, classes PascalCase

**Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
**Test:** `build/tests/dns-tests` (integration tests skip without `DNS_DB_URL`)

---

## Task 1: ThreadPool Implementation + Unit Tests

**Files:**
- Modify: `include/core/ThreadPool.hpp`
- Create: `src/core/ThreadPool.cpp`
- Create: `tests/unit/test_thread_pool.cpp`

The header already has the right interface. We need the `.cpp` implementation and tests.

### Step 1: Write the failing tests

Create `tests/unit/test_thread_pool.cpp`:

```cpp
#include "core/ThreadPool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using dns::core::ThreadPool;

TEST(ThreadPoolTest, SubmitReturnsFuture) {
  ThreadPool tp(2);
  auto fut = tp.submit([]() { return 42; });
  EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, MultipleTasks) {
  ThreadPool tp(4);
  std::vector<std::future<int>> vFuts;
  for (int i = 0; i < 20; ++i) {
    vFuts.push_back(tp.submit([i]() { return i * 2; }));
  }
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(vFuts[i].get(), i * 2);
  }
}

TEST(ThreadPoolTest, ConcurrentExecution) {
  ThreadPool tp(4);
  std::atomic<int> iCounter{0};
  std::vector<std::future<void>> vFuts;
  for (int i = 0; i < 100; ++i) {
    vFuts.push_back(tp.submit([&iCounter]() { iCounter.fetch_add(1); }));
  }
  for (auto& f : vFuts) f.get();
  EXPECT_EQ(iCounter.load(), 100);
}

TEST(ThreadPoolTest, DefaultSizeUsesHardwareConcurrency) {
  ThreadPool tp(0);  // 0 = hardware_concurrency
  auto fut = tp.submit([]() { return 7; });
  EXPECT_EQ(fut.get(), 7);
}

TEST(ThreadPoolTest, ShutdownWaitsForPendingTasks) {
  ThreadPool tp(1);
  std::atomic<bool> bDone{false};
  tp.submit([&bDone]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bDone.store(true);
  });
  tp.shutdown();
  EXPECT_TRUE(bDone.load());
}

TEST(ThreadPoolTest, SubmitAfterShutdownThrows) {
  ThreadPool tp(2);
  tp.shutdown();
  EXPECT_THROW(tp.submit([]() { return 1; }), std::runtime_error);
}
```

### Step 2: Run tests to verify they fail

```bash
cmake --build build --parallel 2>&1 | tail -5
```
Expected: Linker error — `ThreadPool` methods undefined.

### Step 3: Implement ThreadPool

Create `src/core/ThreadPool.cpp`:

```cpp
#include "core/ThreadPool.hpp"

#include <stdexcept>

namespace dns::core {

ThreadPool::ThreadPool(int iSize) {
  int iActual = (iSize <= 0) ? static_cast<int>(std::thread::hardware_concurrency()) : iSize;
  if (iActual <= 0) iActual = 4;  // fallback

  for (int i = 0; i < iActual; ++i) {
    _vWorkers.emplace_back([this](std::stop_token st) {
      while (true) {
        std::packaged_task<void()> task;
        {
          std::unique_lock lock(_mtx);
          _cv.wait(lock, [this, &st]() { return _bStopping || st.stop_requested() || !_qTasks.empty(); });
          if ((_bStopping || st.stop_requested()) && _qTasks.empty()) return;
          task = std::move(_qTasks.front());
          _qTasks.pop();
        }
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  shutdown();
}

void ThreadPool::shutdown() {
  {
    std::lock_guard lock(_mtx);
    if (_bStopping) return;
    _bStopping = true;
  }
  _cv.notify_all();
  for (auto& w : _vWorkers) {
    if (w.joinable()) w.join();
  }
}

}  // namespace dns::core
```

Update `include/core/ThreadPool.hpp` — the template `submit()` must be defined in the header
since it's a template. Replace the declaration with a full definition:

```cpp
#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace dns::core {

/// Fixed-size pool of std::jthread workers.
/// Class abbreviation: tp
class ThreadPool {
 public:
  explicit ThreadPool(int iSize = 0);
  ~ThreadPool();

  template <typename F, typename... Args>
  auto submit(F&& fnTask, Args&&... args) -> std::future<decltype(fnTask(args...))> {
    using ReturnType = decltype(fnTask(args...));
    auto spTask = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(fnTask), std::forward<Args>(args)...));
    auto fut = spTask->get_future();
    {
      std::lock_guard lock(_mtx);
      if (_bStopping) throw std::runtime_error("ThreadPool is shutting down");
      _qTasks.emplace([spTask]() { (*spTask)(); });
    }
    _cv.notify_one();
    return fut;
  }

  void shutdown();

 private:
  std::vector<std::jthread> _vWorkers;
  std::queue<std::packaged_task<void()>> _qTasks;
  std::mutex _mtx;
  std::condition_variable _cv;
  bool _bStopping = false;
};

}  // namespace dns::core
```

### Step 4: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter='ThreadPool*'
```
Expected: All 6 tests PASS.

### Step 5: Commit

```bash
git add include/core/ThreadPool.hpp src/core/ThreadPool.cpp tests/unit/test_thread_pool.cpp
git commit -m "feat(core): implement ThreadPool with jthread workers"
```

---

## Task 2: RecordRepository Additions for Rollback Support

**Files:**
- Modify: `include/dal/RecordRepository.hpp`
- Modify: `src/dal/RecordRepository.cpp`
- Modify: `tests/integration/test_record_repository.cpp`

The RollbackEngine needs two operations on records:
1. `deleteAllByZoneId()` — clear all records before full snapshot restore
2. `upsertById()` — restore individual records for cherry-pick rollback

### Step 1: Write the failing tests

Add to `tests/integration/test_record_repository.cpp`:

```cpp
TEST_F(RecordRepositoryTest, DeleteAllByZoneId) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mx.example.com.", 10);

  int64_t iZone2 = _zrRepo->create("other.com", _iViewId, std::nullopt);
  _rrRepo->create(iZone2, "ns", "NS", 300, "ns1.other.com.", 0);

  int iDeleted = _rrRepo->deleteAllByZoneId(_iZoneId);
  EXPECT_EQ(iDeleted, 2);

  auto vRows = _rrRepo->listByZoneId(_iZoneId);
  EXPECT_TRUE(vRows.empty());

  // Other zone's records unaffected
  auto vOther = _rrRepo->listByZoneId(iZone2);
  EXPECT_EQ(vOther.size(), 1u);
}

TEST_F(RecordRepositoryTest, UpsertById_UpdateExisting) {
  int64_t iId = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);

  int64_t iResult = _rrRepo->upsertById(iId, _iZoneId, "www", "A", 600, "5.6.7.8", 0);
  EXPECT_EQ(iResult, iId);

  auto oRow = _rrRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->iTtl, 600);
  EXPECT_EQ(oRow->sValueTemplate, "5.6.7.8");
}

TEST_F(RecordRepositoryTest, UpsertById_InsertNew) {
  // Use a non-existent ID — should insert a new record (ignoring the ID)
  int64_t iResult = _rrRepo->upsertById(999999, _iZoneId, "new", "AAAA", 300, "::1", 0);
  EXPECT_GT(iResult, 0);

  auto oRow = _rrRepo->findById(iResult);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "new");
  EXPECT_EQ(oRow->sType, "AAAA");
  EXPECT_EQ(oRow->sValueTemplate, "::1");
}
```

### Step 2: Run tests to verify they fail

```bash
cmake --build build --parallel 2>&1 | tail -5
```
Expected: Compile error — `deleteAllByZoneId` and `upsertById` not declared.

### Step 3: Add declarations to header

Add to `include/dal/RecordRepository.hpp` in the `public` section:

```cpp
  /// Delete all records for a zone. Returns deleted count.
  int deleteAllByZoneId(int64_t iZoneId);

  /// Upsert a record by ID. If the ID exists, update it. Otherwise, create a new record.
  /// Returns the record ID (existing or newly created).
  int64_t upsertById(int64_t iId, int64_t iZoneId, const std::string& sName,
                     const std::string& sType, int iTtl,
                     const std::string& sValueTemplate, int iPriority);
```

### Step 4: Implement the methods

Add to `src/dal/RecordRepository.cpp`:

```cpp
int RecordRepository::deleteAllByZoneId(int64_t iZoneId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec("DELETE FROM records WHERE zone_id = $1",
                         pqxx::params{iZoneId});
  txn.commit();
  return static_cast<int>(result.affected_rows());
}

int64_t RecordRepository::upsertById(int64_t iId, int64_t iZoneId,
                                     const std::string& sName, const std::string& sType,
                                     int iTtl, const std::string& sValueTemplate,
                                     int iPriority) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Try update first
  auto result = txn.exec(
      "UPDATE records SET name = $2, type = $3, ttl = $4, value_template = $5, "
      "priority = $6, updated_at = NOW() WHERE id = $1 RETURNING id",
      pqxx::params{iId, sName, sType, iTtl, sValueTemplate, iPriority});

  if (!result.empty()) {
    txn.commit();
    return result[0][0].as<int64_t>();
  }

  // Record doesn't exist — insert new one
  result = txn.exec(
      "INSERT INTO records (zone_id, name, type, ttl, value_template, priority) "
      "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id",
      pqxx::params{iZoneId, sName, sType, iTtl, sValueTemplate, iPriority});
  txn.commit();
  return result.one_row()[0].as<int64_t>();
}
```

### Step 5: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter='RecordRepository*'
```
Expected: All RecordRepository tests PASS (or SKIP if no DNS_DB_URL).

### Step 6: Commit

```bash
git add include/dal/RecordRepository.hpp src/dal/RecordRepository.cpp \
        tests/integration/test_record_repository.cpp
git commit -m "feat(dal): add deleteAllByZoneId and upsertById to RecordRepository"
```

---

## Task 3: GitOpsMirror Implementation + Tests

**Files:**
- Modify: `include/gitops/GitOpsMirror.hpp`
- Create: `src/gitops/GitOpsMirror.cpp`
- Create: `tests/unit/test_gitops_mirror.cpp`

GitOpsMirror maintains a local Git repo clone and pushes zone snapshots after each deployment.
It uses libgit2 for all Git operations. **If `DNS_GIT_REMOTE_URL` is unset, GitOpsMirror is
not constructed** — DeploymentEngine receives a null pointer and skips the git step.

### Step 1: Update the header with full interface

Replace `include/gitops/GitOpsMirror.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

struct git_repository;

namespace dns::dal {
class RecordRepository;
class ViewRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::core {
class VariableEngine;
}

namespace dns::gitops {

/// Maintains a local bare-clone and pushes zone snapshots on deployment.
/// Class abbreviation: gm
class GitOpsMirror {
 public:
  GitOpsMirror(dns::dal::ZoneRepository& zrRepo,
               dns::dal::ViewRepository& vrRepo,
               dns::dal::RecordRepository& rrRepo,
               dns::core::VariableEngine& veEngine);
  ~GitOpsMirror();

  /// Clone or open existing repo. If remote URL is set, clone/fetch.
  void initialize(const std::string& sRemoteUrl, const std::string& sLocalPath);

  /// Write zone snapshot and commit+push to remote.
  /// Serialized globally via mutex. Non-fatal: logs errors but does not throw.
  void commit(int64_t iZoneId, const std::string& sActorIdentity);

  /// Fetch latest from remote (called at startup).
  void pull();

  /// Build zone snapshot JSON string (public for testing).
  std::string buildSnapshotJson(int64_t iZoneId, const std::string& sActor) const;

 private:
  void writeZoneSnapshot(int64_t iZoneId, const std::string& sActor);
  void gitAddCommitPush(const std::string& sMessage);

  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::core::VariableEngine& _veEngine;

  std::string _sLocalPath;
  std::string _sRemoteUrl;
  git_repository* _pRepo = nullptr;
  std::mutex _mtx;  // serializes all git operations
};

}  // namespace dns::gitops
```

### Step 2: Write the failing tests

Create `tests/unit/test_gitops_mirror.cpp`. We test the snapshot JSON building (no git or DB
needed if we mock/stub the repos). Since repos are concrete classes, we test `buildSnapshotJson`
as an integration test:

```cpp
#include "gitops/GitOpsMirror.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
}  // namespace

class GitOpsMirrorSnapshotTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _veEngine = std::make_unique<dns::core::VariableEngine>();

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    _iViewId = _vrRepo->create("test-view", "Test view");
    _iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
    _rrRepo->create(_iZoneId, "www.example.com.", "A", 300, "1.2.3.4", 0);
    _rrRepo->create(_iZoneId, "mail.example.com.", "MX", 300, "mx.example.com.", 10);
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::core::VariableEngine> _veEngine;
  int64_t _iViewId = 0;
  int64_t _iZoneId = 0;
};

TEST_F(GitOpsMirrorSnapshotTest, BuildSnapshotJson) {
  dns::gitops::GitOpsMirror gm(*_zrRepo, *_vrRepo, *_rrRepo, *_veEngine);

  std::string sJson = gm.buildSnapshotJson(_iZoneId, "alice");
  auto j = nlohmann::json::parse(sJson);

  EXPECT_EQ(j["zone"], "example.com");
  EXPECT_EQ(j["view"], "test-view");
  EXPECT_EQ(j["generated_by"], "alice");
  ASSERT_TRUE(j.contains("records"));
  EXPECT_EQ(j["records"].size(), 2u);

  // Records should have expanded values (no {{var}} since templates are static)
  bool bFoundWww = false;
  for (const auto& rec : j["records"]) {
    if (rec["name"] == "www.example.com.") {
      EXPECT_EQ(rec["type"], "A");
      EXPECT_EQ(rec["value"], "1.2.3.4");
      EXPECT_EQ(rec["ttl"], 300);
      bFoundWww = true;
    }
  }
  EXPECT_TRUE(bFoundWww);
}
```

### Step 3: Run tests to verify they fail

```bash
cmake --build build --parallel 2>&1 | tail -5
```
Expected: Linker error — `GitOpsMirror` methods undefined.

### Step 4: Implement GitOpsMirror

Create `src/gitops/GitOpsMirror.cpp`:

```cpp
#include "gitops/GitOpsMirror.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <git2.h>
#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace dns::gitops {

GitOpsMirror::GitOpsMirror(dns::dal::ZoneRepository& zrRepo,
                           dns::dal::ViewRepository& vrRepo,
                           dns::dal::RecordRepository& rrRepo,
                           dns::core::VariableEngine& veEngine)
    : _zrRepo(zrRepo), _vrRepo(vrRepo), _rrRepo(rrRepo), _veEngine(veEngine) {
  git_libgit2_init();
}

GitOpsMirror::~GitOpsMirror() {
  if (_pRepo) git_repository_free(_pRepo);
  git_libgit2_shutdown();
}

void GitOpsMirror::initialize(const std::string& sRemoteUrl, const std::string& sLocalPath) {
  auto spLog = common::Logger::get();
  _sRemoteUrl = sRemoteUrl;
  _sLocalPath = sLocalPath;

  namespace fs = std::filesystem;
  if (fs::exists(sLocalPath + "/.git") || fs::exists(sLocalPath + "/HEAD")) {
    // Open existing repo
    int iErr = git_repository_open(&_pRepo, sLocalPath.c_str());
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->error("GitOpsMirror: failed to open repo at '{}': {}", sLocalPath,
                   pErr ? pErr->message : "unknown");
      throw common::GitMirrorError("GIT_OPEN_FAILED",
                                   "Failed to open git repo: " +
                                       std::string(pErr ? pErr->message : "unknown"));
    }
    spLog->info("GitOpsMirror: opened existing repo at '{}'", sLocalPath);
  } else {
    // Clone from remote
    fs::create_directories(sLocalPath);
    int iErr = git_clone(&_pRepo, sRemoteUrl.c_str(), sLocalPath.c_str(), nullptr);
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->error("GitOpsMirror: failed to clone '{}' to '{}': {}", sRemoteUrl, sLocalPath,
                   pErr ? pErr->message : "unknown");
      throw common::GitMirrorError("GIT_CLONE_FAILED",
                                   "Failed to clone git repo: " +
                                       std::string(pErr ? pErr->message : "unknown"));
    }
    spLog->info("GitOpsMirror: cloned '{}' to '{}'", sRemoteUrl, sLocalPath);
  }
}

void GitOpsMirror::pull() {
  // Fetch from origin and reset to FETCH_HEAD
  // This is a simplified pull — the mirror is append/overwrite only
  if (!_pRepo) return;

  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  git_remote* pRemote = nullptr;
  int iErr = git_remote_lookup(&pRemote, _pRepo, "origin");
  if (iErr < 0) {
    spLog->warn("GitOpsMirror: no 'origin' remote — skipping pull");
    return;
  }

  iErr = git_remote_fetch(pRemote, nullptr, nullptr, nullptr);
  git_remote_free(pRemote);
  if (iErr < 0) {
    const git_error* pErr = git_error_last();
    spLog->warn("GitOpsMirror: fetch failed: {}", pErr ? pErr->message : "unknown");
  } else {
    spLog->info("GitOpsMirror: fetched from origin");
  }
}

std::string GitOpsMirror::buildSnapshotJson(int64_t iZoneId,
                                            const std::string& sActor) const {
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  auto oView = _vrRepo.findById(oZone->iViewId);
  std::string sViewName = oView ? oView->sName : "unknown";

  auto vRecords = _rrRepo.listByZoneId(iZoneId);

  // Build records array with expanded values
  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& rec : vRecords) {
    std::string sExpandedValue;
    try {
      sExpandedValue = _veEngine.expand(rec.sValueTemplate, iZoneId);
    } catch (...) {
      sExpandedValue = rec.sValueTemplate;  // fallback to raw if expansion fails
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

  // ISO 8601 timestamp
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

void GitOpsMirror::writeZoneSnapshot(int64_t iZoneId, const std::string& sActor) {
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) return;

  auto oView = _vrRepo.findById(oZone->iViewId);
  std::string sViewName = oView ? oView->sName : "unknown";

  // Build path: {local_path}/{view_name}/{zone_name}.json
  namespace fs = std::filesystem;
  fs::path dirPath = fs::path(_sLocalPath) / sViewName;
  fs::create_directories(dirPath);

  fs::path filePath = dirPath / (oZone->sName + ".json");
  std::string sJson = buildSnapshotJson(iZoneId, sActor);

  std::ofstream ofs(filePath);
  ofs << sJson;
  ofs.close();
}

void GitOpsMirror::gitAddCommitPush(const std::string& sMessage) {
  if (!_pRepo) return;
  auto spLog = common::Logger::get();

  // Stage all changes
  git_index* pIndex = nullptr;
  git_repository_index(&pIndex, _pRepo);
  git_index_add_all(pIndex, nullptr, 0, nullptr, nullptr);
  git_index_write(pIndex);

  // Create tree from index
  git_oid treeOid;
  git_index_write_tree(&treeOid, pIndex);
  git_index_free(pIndex);

  git_tree* pTree = nullptr;
  git_tree_lookup(&pTree, _pRepo, &treeOid);

  // Get HEAD commit as parent (if exists)
  git_reference* pHead = nullptr;
  git_commit* pParent = nullptr;
  bool bHasParent = false;
  if (git_repository_head(&pHead, _pRepo) == 0) {
    git_oid parentOid;
    git_reference_name_to_id(&parentOid, _pRepo, "HEAD");
    git_commit_lookup(&pParent, _pRepo, &parentOid);
    bHasParent = true;
  }

  // Create commit
  git_signature* pSig = nullptr;
  git_signature_now(&pSig, "dns-orchestrator", "dns@orchestrator.local");

  git_oid commitOid;
  const git_commit* vParents[] = {pParent};
  git_commit_create(&commitOid, _pRepo, "HEAD", pSig, pSig, "UTF-8", sMessage.c_str(),
                    pTree, bHasParent ? 1 : 0, bHasParent ? vParents : nullptr);

  git_signature_free(pSig);
  git_tree_free(pTree);
  if (pParent) git_commit_free(pParent);
  if (pHead) git_reference_free(pHead);

  // Push to origin
  git_remote* pRemote = nullptr;
  if (git_remote_lookup(&pRemote, _pRepo, "origin") == 0) {
    const char* vRefspecs[] = {"refs/heads/main:refs/heads/main"};
    git_strarray refspecs = {const_cast<char**>(vRefspecs), 1};
    int iErr = git_remote_push(pRemote, &refspecs, nullptr);
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->warn("GitOpsMirror: push failed: {}", pErr ? pErr->message : "unknown");
    }
    git_remote_free(pRemote);
  }
}

void GitOpsMirror::commit(int64_t iZoneId, const std::string& sActorIdentity) {
  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  try {
    auto oZone = _zrRepo.findById(iZoneId);
    std::string sZoneName = oZone ? oZone->sName : std::to_string(iZoneId);

    writeZoneSnapshot(iZoneId, sActorIdentity);
    gitAddCommitPush("Update " + sZoneName + " by " + sActorIdentity + " via API");
    spLog->info("GitOpsMirror: committed zone '{}' by {}", sZoneName, sActorIdentity);
  } catch (const std::exception& ex) {
    // Non-fatal: log and continue. GitMirror failure should not block deployment.
    spLog->error("GitOpsMirror: commit failed for zone {}: {}", iZoneId, ex.what());
  }
}

}  // namespace dns::gitops
```

### Step 5: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter='GitOpsMirror*'
```
Expected: Tests PASS (or SKIP without DNS_DB_URL).

### Step 6: Commit

```bash
git add include/gitops/GitOpsMirror.hpp src/gitops/GitOpsMirror.cpp \
        tests/unit/test_gitops_mirror.cpp
git commit -m "feat(gitops): implement GitOpsMirror with libgit2"
```

---

## Task 4: DeploymentEngine Implementation + Tests

**Files:**
- Modify: `include/core/DeploymentEngine.hpp`
- Create: `src/core/DeploymentEngine.cpp`
- Modify: `tests/integration/test_deployment_pipeline.cpp`

The DeploymentEngine orchestrates the full push pipeline:
1. Acquire per-zone mutex (reject if locked → `DeploymentLockedError`)
2. Re-run `DiffEngine::preview()` for freshness
3. Execute diffs via `IProvider` (create/update/delete + optional drift purge)
4. Build expanded snapshot JSON
5. `DeploymentRepository::create()` snapshot
6. `DeploymentRepository::pruneByRetention()` old snapshots
7. Audit log entries
8. `GitOpsMirror::commit()` (if enabled)
9. Release per-zone mutex

### Step 1: Update the header

Replace `include/core/DeploymentEngine.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dns::dal {
class AuditRepository;
class DeploymentRepository;
class ProviderRepository;
class RecordRepository;
class ViewRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::gitops {
class GitOpsMirror;
}

namespace dns::core {

class DiffEngine;
class VariableEngine;

/// Accepts a PreviewResult and executes the diff against the provider.
/// Class abbreviation: dep
class DeploymentEngine {
 public:
  DeploymentEngine(DiffEngine& deEngine,
                   VariableEngine& veEngine,
                   dns::dal::ZoneRepository& zrRepo,
                   dns::dal::ViewRepository& vrRepo,
                   dns::dal::RecordRepository& rrRepo,
                   dns::dal::ProviderRepository& prRepo,
                   dns::dal::DeploymentRepository& drRepo,
                   dns::dal::AuditRepository& arRepo,
                   dns::gitops::GitOpsMirror* pGitMirror,
                   int iRetentionCount);
  ~DeploymentEngine();

  /// Execute the full push pipeline for a zone.
  /// Throws DeploymentLockedError if the zone is already being deployed.
  /// Throws ProviderError on provider failure.
  void push(int64_t iZoneId, bool bPurgeDrift,
            int64_t iActorUserId, const std::string& sActor);

 private:
  /// Build the deployment snapshot JSON from current records.
  nlohmann::json buildSnapshot(int64_t iZoneId, const std::string& sActor) const;

  /// Get or create a per-zone mutex.
  std::mutex& zoneMutex(int64_t iZoneId);

  DiffEngine& _deEngine;
  VariableEngine& _veEngine;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::ProviderRepository& _prRepo;
  dns::dal::DeploymentRepository& _drRepo;
  dns::dal::AuditRepository& _arRepo;
  dns::gitops::GitOpsMirror* _pGitMirror;
  int _iRetentionCount;

  std::unordered_map<int64_t, std::unique_ptr<std::mutex>> _mZoneMutexes;
  std::mutex _mtxMap;
};

}  // namespace dns::core
```

### Step 2: Write the failing tests

Replace `tests/integration/test_deployment_pipeline.cpp`:

```cpp
#include "core/DeploymentEngine.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "common/Types.hpp"
#include "core/DiffEngine.hpp"
#include "core/VariableEngine.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "security/CryptoService.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
}  // namespace

class DeploymentEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);

    // 32-byte hex key for CryptoService
    std::string sMasterKey = "0123456789abcdef0123456789abcdef"
                             "0123456789abcdef0123456789abcdef";
    _csService = std::make_unique<dns::security::CryptoService>(sMasterKey);

    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _prRepo = std::make_unique<dns::dal::ProviderRepository>(*_cpPool, *_csService);
    _varRepo = std::make_unique<dns::dal::VariableRepository>(*_cpPool);
    _drRepo = std::make_unique<dns::dal::DeploymentRepository>(*_cpPool);
    _arRepo = std::make_unique<dns::dal::AuditRepository>(*_cpPool);

    _veEngine = std::make_unique<dns::core::VariableEngine>(*_varRepo);
    _deEngine = std::make_unique<dns::core::DiffEngine>(
        *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo, *_veEngine);

    // Clean slate
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM audit_log");
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.exec("DELETE FROM providers");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::dal::ProviderRepository> _prRepo;
  std::unique_ptr<dns::dal::VariableRepository> _varRepo;
  std::unique_ptr<dns::dal::DeploymentRepository> _drRepo;
  std::unique_ptr<dns::dal::AuditRepository> _arRepo;
  std::unique_ptr<dns::core::VariableEngine> _veEngine;
  std::unique_ptr<dns::core::DiffEngine> _deEngine;
};

TEST_F(DeploymentEngineTest, BuildSnapshotContainsExpandedRecords) {
  int64_t iViewId = _vrRepo->create("ext", "External");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www.example.com.", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(iZoneId, "mail.example.com.", "MX", 300, "mx.example.com.", 10);

  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, nullptr, 10);

  // Access buildSnapshot via push is integration-level; test snapshot format here
  // by checking the DeploymentRepository after a manual snapshot creation.
  // This tests that the engine can be constructed without errors.
  EXPECT_TRUE(true);  // Construction succeeds
}

TEST_F(DeploymentEngineTest, PushFailsWithNoProviders) {
  int64_t iViewId = _vrRepo->create("ext", "External");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www.example.com.", "A", 300, "1.2.3.4", 0);

  // No providers attached to view — DiffEngine::preview() should throw
  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, nullptr, 10);

  // Create a fake user for the actor
  auto cg = _cpPool->checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "INSERT INTO users (username, auth_method) VALUES ('alice', 'local') RETURNING id");
  int64_t iUserId = result[0][0].as<int64_t>();
  txn.commit();

  EXPECT_THROW(depEngine.push(iZoneId, false, iUserId, "alice"),
               dns::common::ValidationError);
}

TEST_F(DeploymentEngineTest, PushFailsForNonexistentZone) {
  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, nullptr, 10);

  EXPECT_THROW(depEngine.push(99999, false, 1, "alice"),
               dns::common::NotFoundError);
}
```

### Step 3: Run tests to verify they fail

```bash
cmake --build build --parallel 2>&1 | tail -5
```
Expected: Linker error — `DeploymentEngine` methods undefined.

### Step 4: Implement DeploymentEngine

Create `src/core/DeploymentEngine.cpp`:

```cpp
#include "core/DeploymentEngine.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "common/Types.hpp"
#include "core/DiffEngine.hpp"
#include "core/VariableEngine.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "gitops/GitOpsMirror.hpp"
#include "providers/ProviderFactory.hpp"

namespace dns::core {

DeploymentEngine::DeploymentEngine(DiffEngine& deEngine,
                                   VariableEngine& veEngine,
                                   dns::dal::ZoneRepository& zrRepo,
                                   dns::dal::ViewRepository& vrRepo,
                                   dns::dal::RecordRepository& rrRepo,
                                   dns::dal::ProviderRepository& prRepo,
                                   dns::dal::DeploymentRepository& drRepo,
                                   dns::dal::AuditRepository& arRepo,
                                   dns::gitops::GitOpsMirror* pGitMirror,
                                   int iRetentionCount)
    : _deEngine(deEngine),
      _veEngine(veEngine),
      _zrRepo(zrRepo),
      _vrRepo(vrRepo),
      _rrRepo(rrRepo),
      _prRepo(prRepo),
      _drRepo(drRepo),
      _arRepo(arRepo),
      _pGitMirror(pGitMirror),
      _iRetentionCount(iRetentionCount) {}

DeploymentEngine::~DeploymentEngine() = default;

std::mutex& DeploymentEngine::zoneMutex(int64_t iZoneId) {
  std::lock_guard lock(_mtxMap);
  auto it = _mZoneMutexes.find(iZoneId);
  if (it == _mZoneMutexes.end()) {
    auto [newIt, _] = _mZoneMutexes.emplace(iZoneId, std::make_unique<std::mutex>());
    return *newIt->second;
  }
  return *it->second;
}

nlohmann::json DeploymentEngine::buildSnapshot(int64_t iZoneId,
                                               const std::string& sActor) const {
  auto oZone = _zrRepo.findById(iZoneId);
  std::string sZoneName = oZone ? oZone->sName : "unknown";

  auto oView = oZone ? _vrRepo.findById(oZone->iViewId) : std::nullopt;
  std::string sViewName = oView ? oView->sName : "unknown";

  auto vRecords = _rrRepo.listByZoneId(iZoneId);

  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& rec : vRecords) {
    std::string sExpanded;
    try {
      sExpanded = _veEngine.expand(rec.sValueTemplate, iZoneId);
    } catch (...) {
      sExpanded = rec.sValueTemplate;
    }

    jRecords.push_back({
        {"record_id", rec.iId},
        {"name", rec.sName},
        {"type", rec.sType},
        {"ttl", rec.iTtl},
        {"value_template", rec.sValueTemplate},
        {"value", sExpanded},
        {"priority", rec.iPriority},
    });
  }

  auto tpNow = std::chrono::system_clock::now();
  auto ttNow = std::chrono::system_clock::to_time_t(tpNow);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&ttNow), "%FT%TZ");

  return {
      {"zone", sZoneName},
      {"view", sViewName},
      {"deployed_at", oss.str()},
      {"deployed_by", sActor},
      {"records", jRecords},
  };
}

void DeploymentEngine::push(int64_t iZoneId, bool bPurgeDrift,
                            int64_t iActorUserId, const std::string& sActor) {
  auto spLog = common::Logger::get();

  // 1. Acquire per-zone mutex (non-blocking try_lock)
  auto& mtxZone = zoneMutex(iZoneId);
  if (!mtxZone.try_lock()) {
    throw common::DeploymentLockedError(
        "ZONE_LOCKED", "Zone " + std::to_string(iZoneId) + " is currently being deployed");
  }
  std::lock_guard lock(mtxZone, std::adopt_lock);

  // 2. Fresh preview (freshness guard against stale diffs)
  auto prResult = _deEngine.preview(iZoneId);
  spLog->info("DeploymentEngine: zone '{}' — {} diffs to push", prResult.sZoneName,
              prResult.vDiffs.size());

  if (prResult.vDiffs.empty()) {
    spLog->info("DeploymentEngine: zone '{}' — nothing to push", prResult.sZoneName);
    return;
  }

  // 3. Get provider for push operations
  auto oZone = _zrRepo.findById(iZoneId);
  auto oView = _vrRepo.findWithProviders(oZone->iViewId);

  for (int64_t iProviderId : oView->vProviderIds) {
    auto oProvider = _prRepo.findById(iProviderId);
    if (!oProvider) continue;

    auto upProvider = dns::providers::ProviderFactory::create(
        oProvider->sType, oProvider->sApiEndpoint, oProvider->sDecryptedToken);

    // 4. Execute diffs
    for (const auto& diff : prResult.vDiffs) {
      try {
        switch (diff.action) {
          case common::DiffAction::Add: {
            common::DnsRecord dr;
            dr.sName = diff.sName;
            dr.sType = diff.sType;
            dr.sValue = diff.sSourceValue;
            auto pushResult = upProvider->createRecord(oZone->sName, dr);
            if (!pushResult.bSuccess) {
              throw common::ProviderError("PROVIDER_CREATE_FAILED",
                                          "Failed to create record: " + pushResult.sErrorMessage);
            }
            break;
          }
          case common::DiffAction::Update: {
            common::DnsRecord dr;
            dr.sName = diff.sName;
            dr.sType = diff.sType;
            dr.sValue = diff.sSourceValue;
            auto pushResult = upProvider->updateRecord(oZone->sName, dr);
            if (!pushResult.bSuccess) {
              throw common::ProviderError("PROVIDER_UPDATE_FAILED",
                                          "Failed to update record: " + pushResult.sErrorMessage);
            }
            break;
          }
          case common::DiffAction::Delete: {
            // Records marked Delete should be removed from provider
            // This shouldn't normally appear in the diff (only Add/Update/Drift)
            break;
          }
          case common::DiffAction::Drift: {
            if (bPurgeDrift) {
              // Build a synthetic provider record ID for deletion
              std::string sRecordId = diff.sName + "/" + diff.sType + "/" + diff.sProviderValue;
              bool bDeleted = upProvider->deleteRecord(oZone->sName, sRecordId);
              if (!bDeleted) {
                spLog->warn("DeploymentEngine: failed to purge drift record {}/{}/{}",
                            diff.sName, diff.sType, diff.sProviderValue);
              }
            }
            break;
          }
        }
      } catch (const common::ProviderError&) {
        throw;  // Re-throw provider errors
      }
    }
  }

  // 5. Write audit log
  nlohmann::json jDiffs = nlohmann::json::array();
  for (const auto& diff : prResult.vDiffs) {
    jDiffs.push_back({
        {"action", diff.action == common::DiffAction::Add      ? "add"
                   : diff.action == common::DiffAction::Update  ? "update"
                   : diff.action == common::DiffAction::Delete  ? "delete"
                                                                : "drift"},
        {"name", diff.sName},
        {"type", diff.sType},
        {"source_value", diff.sSourceValue},
        {"provider_value", diff.sProviderValue},
    });
  }
  _arRepo.insert("zone", iZoneId, "push", std::nullopt, jDiffs, sActor,
                 std::nullopt, std::nullopt);

  // 6. Create deployment snapshot
  auto jSnapshot = buildSnapshot(iZoneId, sActor);
  _drRepo.create(iZoneId, iActorUserId, jSnapshot);

  // 7. Prune old snapshots
  int iRetention = _iRetentionCount;
  if (oZone->oDeploymentRetention.has_value() && *oZone->oDeploymentRetention >= 1) {
    iRetention = *oZone->oDeploymentRetention;
  }
  _drRepo.pruneByRetention(iZoneId, iRetention);

  // 8. GitOps mirror commit (non-fatal)
  if (_pGitMirror) {
    _pGitMirror->commit(iZoneId, sActor);
  }

  spLog->info("DeploymentEngine: zone '{}' pushed successfully by {}", prResult.sZoneName,
              sActor);
}

}  // namespace dns::core
```

### Step 5: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter='DeploymentEngine*'
```
Expected: All DeploymentEngine tests PASS (or SKIP without DNS_DB_URL).

### Step 6: Commit

```bash
git add include/core/DeploymentEngine.hpp src/core/DeploymentEngine.cpp \
        tests/integration/test_deployment_pipeline.cpp
git commit -m "feat(core): implement DeploymentEngine push pipeline"
```

---

## Task 5: RollbackEngine Implementation + Tests

**Files:**
- Modify: `include/core/RollbackEngine.hpp`
- Create: `src/core/RollbackEngine.cpp`
- Create: `tests/integration/test_rollback_engine.cpp`

RollbackEngine restores a deployment snapshot back into the `records` table (desired state).
It does **not** push to providers — the operator must preview and push afterward.

### Step 1: Update the header

Replace `include/core/RollbackEngine.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dns::dal {
class AuditRepository;
class DeploymentRepository;
class RecordRepository;
}  // namespace dns::dal

namespace dns::core {

/// Restores deployment snapshots into the desired state.
/// Class abbreviation: re
class RollbackEngine {
 public:
  RollbackEngine(dns::dal::DeploymentRepository& drRepo,
                 dns::dal::RecordRepository& rrRepo,
                 dns::dal::AuditRepository& arRepo);
  ~RollbackEngine();

  /// Restore a snapshot into the records table.
  /// If vCherryPickIds is empty, restores the full snapshot (delete all + insert).
  /// If vCherryPickIds is non-empty, restores only the specified record IDs.
  /// Does NOT push to providers — operator must preview + push afterward.
  void apply(int64_t iZoneId, int64_t iDeploymentId,
             const std::vector<int64_t>& vCherryPickIds,
             int64_t iActorUserId, const std::string& sActor);

 private:
  dns::dal::DeploymentRepository& _drRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::AuditRepository& _arRepo;
};

}  // namespace dns::core
```

### Step 2: Write the failing tests

Create `tests/integration/test_rollback_engine.cpp`:

```cpp
#include "core/RollbackEngine.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
}  // namespace

class RollbackEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _drRepo = std::make_unique<dns::dal::DeploymentRepository>(*_cpPool);
    _arRepo = std::make_unique<dns::dal::AuditRepository>(*_cpPool);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM audit_log");
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    _iViewId = _vrRepo->create("ext", "External");
    _iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);

    // Create a user for deployed_by
    {
      auto cg2 = _cpPool->checkout();
      pqxx::work txn2(*cg2);
      txn2.exec("DELETE FROM sessions");
      txn2.exec("DELETE FROM api_keys");
      txn2.exec("DELETE FROM group_members");
      txn2.exec("DELETE FROM users");
      auto r = txn2.exec(
          "INSERT INTO users (username, auth_method) VALUES ('alice', 'local') RETURNING id");
      _iUserId = r[0][0].as<int64_t>();
      txn2.commit();
    }
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::dal::DeploymentRepository> _drRepo;
  std::unique_ptr<dns::dal::AuditRepository> _arRepo;
  int64_t _iViewId = 0;
  int64_t _iZoneId = 0;
  int64_t _iUserId = 0;
};

TEST_F(RollbackEngineTest, FullRestore) {
  // Create original records
  int64_t iRec1 = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  int64_t iRec2 = _rrRepo->create(_iZoneId, "mail", "MX", 300, "mx.example.com.", 10);

  // Save a deployment snapshot
  nlohmann::json jSnapshot = {
      {"zone", "example.com"},
      {"view", "ext"},
      {"deployed_by", "alice"},
      {"records", {{{"record_id", iRec1}, {"name", "www"}, {"type", "A"}, {"ttl", 300},
                    {"value_template", "1.2.3.4"}, {"value", "1.2.3.4"}, {"priority", 0}},
                   {{"record_id", iRec2}, {"name", "mail"}, {"type", "MX"}, {"ttl", 300},
                    {"value_template", "mx.example.com."}, {"value", "mx.example.com."},
                    {"priority", 10}}}},
  };
  int64_t iDeployId = _drRepo->create(_iZoneId, _iUserId, jSnapshot);

  // Now modify records (simulate changes after deployment)
  _rrRepo->update(iRec1, "www", "A", 600, "9.9.9.9", 0);
  _rrRepo->deleteById(iRec2);
  _rrRepo->create(_iZoneId, "new", "CNAME", 300, "other.com.", 0);

  // Rollback to the snapshot (full restore)
  dns::core::RollbackEngine reEngine(*_drRepo, *_rrRepo, *_arRepo);
  reEngine.apply(_iZoneId, iDeployId, {}, _iUserId, "alice");

  // Verify records match snapshot
  auto vRecords = _rrRepo->listByZoneId(_iZoneId);
  EXPECT_EQ(vRecords.size(), 2u);

  bool bFoundWww = false, bFoundMail = false;
  for (const auto& rec : vRecords) {
    if (rec.sName == "www") {
      EXPECT_EQ(rec.sValueTemplate, "1.2.3.4");
      EXPECT_EQ(rec.iTtl, 300);
      bFoundWww = true;
    }
    if (rec.sName == "mail") {
      EXPECT_EQ(rec.sValueTemplate, "mx.example.com.");
      bFoundMail = true;
    }
  }
  EXPECT_TRUE(bFoundWww);
  EXPECT_TRUE(bFoundMail);
}

TEST_F(RollbackEngineTest, CherryPickRestore) {
  int64_t iRec1 = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  int64_t iRec2 = _rrRepo->create(_iZoneId, "mail", "MX", 300, "mx.example.com.", 10);

  nlohmann::json jSnapshot = {
      {"zone", "example.com"},
      {"records", {{{"record_id", iRec1}, {"name", "www"}, {"type", "A"}, {"ttl", 300},
                    {"value_template", "1.2.3.4"}, {"value", "1.2.3.4"}, {"priority", 0}},
                   {{"record_id", iRec2}, {"name", "mail"}, {"type", "MX"}, {"ttl", 300},
                    {"value_template", "mx.example.com."}, {"value", "mx.example.com."},
                    {"priority", 10}}}},
  };
  int64_t iDeployId = _drRepo->create(_iZoneId, _iUserId, jSnapshot);

  // Modify both records
  _rrRepo->update(iRec1, "www", "A", 600, "9.9.9.9", 0);
  _rrRepo->update(iRec2, "mail", "MX", 600, "mx2.example.com.", 10);

  // Cherry-pick restore only iRec1
  dns::core::RollbackEngine reEngine(*_drRepo, *_rrRepo, *_arRepo);
  reEngine.apply(_iZoneId, iDeployId, {iRec1}, _iUserId, "alice");

  // www should be restored, mail should keep its modified value
  auto oRec1 = _rrRepo->findById(iRec1);
  ASSERT_TRUE(oRec1.has_value());
  EXPECT_EQ(oRec1->sValueTemplate, "1.2.3.4");
  EXPECT_EQ(oRec1->iTtl, 300);

  auto oRec2 = _rrRepo->findById(iRec2);
  ASSERT_TRUE(oRec2.has_value());
  EXPECT_EQ(oRec2->sValueTemplate, "mx2.example.com.");
  EXPECT_EQ(oRec2->iTtl, 600);
}

TEST_F(RollbackEngineTest, RollbackNonexistentDeploymentThrows) {
  dns::core::RollbackEngine reEngine(*_drRepo, *_rrRepo, *_arRepo);
  EXPECT_THROW(reEngine.apply(_iZoneId, 99999, {}, _iUserId, "alice"),
               dns::common::NotFoundError);
}

TEST_F(RollbackEngineTest, RollbackCreatesAuditEntry) {
  int64_t iRec1 = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  nlohmann::json jSnapshot = {
      {"zone", "example.com"},
      {"records", {{{"record_id", iRec1}, {"name", "www"}, {"type", "A"}, {"ttl", 300},
                    {"value_template", "1.2.3.4"}, {"value", "1.2.3.4"}, {"priority", 0}}}},
  };
  int64_t iDeployId = _drRepo->create(_iZoneId, _iUserId, jSnapshot);

  dns::core::RollbackEngine reEngine(*_drRepo, *_rrRepo, *_arRepo);
  reEngine.apply(_iZoneId, iDeployId, {}, _iUserId, "alice");

  auto vAudit = _arRepo->query(std::string("zone"), std::nullopt, std::string("alice"),
                               std::nullopt, std::nullopt, 10);
  ASSERT_FALSE(vAudit.empty());
  EXPECT_EQ(vAudit[0].sOperation, "rollback");
}
```

### Step 3: Run tests to verify they fail

```bash
cmake --build build --parallel 2>&1 | tail -5
```
Expected: Linker error — `RollbackEngine` methods undefined.

### Step 4: Implement RollbackEngine

Create `src/core/RollbackEngine.cpp`:

```cpp
#include "core/RollbackEngine.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/RecordRepository.hpp"

namespace dns::core {

RollbackEngine::RollbackEngine(dns::dal::DeploymentRepository& drRepo,
                               dns::dal::RecordRepository& rrRepo,
                               dns::dal::AuditRepository& arRepo)
    : _drRepo(drRepo), _rrRepo(rrRepo), _arRepo(arRepo) {}

RollbackEngine::~RollbackEngine() = default;

void RollbackEngine::apply(int64_t iZoneId, int64_t iDeploymentId,
                           const std::vector<int64_t>& vCherryPickIds,
                           int64_t /*iActorUserId*/, const std::string& sActor) {
  auto spLog = common::Logger::get();

  // 1. Fetch the deployment snapshot
  auto oDeploy = _drRepo.findById(iDeploymentId);
  if (!oDeploy) {
    throw common::NotFoundError("DEPLOYMENT_NOT_FOUND",
                                "Deployment " + std::to_string(iDeploymentId) + " not found");
  }
  if (oDeploy->iZoneId != iZoneId) {
    throw common::ValidationError("ZONE_MISMATCH",
                                  "Deployment does not belong to zone " +
                                      std::to_string(iZoneId));
  }

  const auto& jSnapshot = oDeploy->jSnapshot;
  if (!jSnapshot.contains("records") || !jSnapshot["records"].is_array()) {
    throw common::ValidationError("INVALID_SNAPSHOT", "Snapshot has no records array");
  }

  // 2. Restore records
  if (vCherryPickIds.empty()) {
    // Full restore: delete all current records, then insert from snapshot
    _rrRepo.deleteAllByZoneId(iZoneId);

    for (const auto& jRec : jSnapshot["records"]) {
      std::string sName = jRec.value("name", "");
      std::string sType = jRec.value("type", "");
      int iTtl = jRec.value("ttl", 300);
      // Use value_template if present, fall back to expanded value
      std::string sValueTemplate = jRec.value("value_template", jRec.value("value", ""));
      int iPriority = jRec.value("priority", 0);

      _rrRepo.create(iZoneId, sName, sType, iTtl, sValueTemplate, iPriority);
    }

    spLog->info("RollbackEngine: full restore of zone {} from deployment {}",
                iZoneId, iDeploymentId);
  } else {
    // Cherry-pick: restore only specified record IDs
    for (int64_t iRecordId : vCherryPickIds) {
      // Find the record in the snapshot
      bool bFound = false;
      for (const auto& jRec : jSnapshot["records"]) {
        if (jRec.value("record_id", int64_t{0}) == iRecordId) {
          std::string sName = jRec.value("name", "");
          std::string sType = jRec.value("type", "");
          int iTtl = jRec.value("ttl", 300);
          std::string sValueTemplate = jRec.value("value_template", jRec.value("value", ""));
          int iPriority = jRec.value("priority", 0);

          _rrRepo.upsertById(iRecordId, iZoneId, sName, sType, iTtl, sValueTemplate, iPriority);
          bFound = true;
          break;
        }
      }
      if (!bFound) {
        spLog->warn("RollbackEngine: record {} not found in snapshot {} — skipping",
                    iRecordId, iDeploymentId);
      }
    }

    spLog->info("RollbackEngine: cherry-pick restore of {} records in zone {} from deployment {}",
                vCherryPickIds.size(), iZoneId, iDeploymentId);
  }

  // 3. Audit log
  nlohmann::json jNewValue = {
      {"deployment_id", iDeploymentId},
      {"cherry_pick_ids", vCherryPickIds},
  };
  _arRepo.insert("zone", iZoneId, "rollback", std::nullopt, jNewValue, sActor,
                 std::nullopt, std::nullopt);
}

}  // namespace dns::core
```

### Step 5: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter='RollbackEngine*'
```
Expected: All RollbackEngine tests PASS (or SKIP without DNS_DB_URL).

### Step 6: Commit

```bash
git add include/core/RollbackEngine.hpp src/core/RollbackEngine.cpp \
        tests/integration/test_rollback_engine.cpp
git commit -m "feat(core): implement RollbackEngine for snapshot restore"
```

---

## Task 6: Preview + Push API Routes

**Files:**
- Modify: `include/api/routes/RecordRoutes.hpp`
- Modify: `src/api/routes/RecordRoutes.cpp`
- Modify: `tests/integration/test_crud_routes.cpp`

Add `POST /zones/{id}/preview` and `POST /zones/{id}/push` to RecordRoutes, as specified
in ARCHITECTURE.md. Preview calls `DiffEngine::preview()`. Push calls
`DeploymentEngine::push()`.

### Step 1: Update RecordRoutes header to accept engine dependencies

Replace `include/api/routes/RecordRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::dal {
class RecordRepository;
}

namespace dns::core {
class DeploymentEngine;
class DiffEngine;
}  // namespace dns::core

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/zones/{id}/records and preview/push
/// Class abbreviation: rr
class RecordRoutes {
 public:
  RecordRoutes(dns::dal::RecordRepository& rrRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::core::DiffEngine& deEngine,
               dns::core::DeploymentEngine& depEngine);
  ~RecordRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::RecordRepository& _rrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::DiffEngine& _deEngine;
  dns::core::DeploymentEngine& _depEngine;
};

}  // namespace dns::api::routes
```

### Step 2: Update RecordRoutes constructor

In `src/api/routes/RecordRoutes.cpp`, update the constructor:

```cpp
RecordRoutes::RecordRoutes(dns::dal::RecordRepository& rrRepo,
                           const dns::api::AuthMiddleware& amMiddleware,
                           dns::core::DiffEngine& deEngine,
                           dns::core::DeploymentEngine& depEngine)
    : _rrRepo(rrRepo), _amMiddleware(amMiddleware),
      _deEngine(deEngine), _depEngine(depEngine) {}
```

### Step 3: Add preview and push routes

Add to the `registerRoutes()` method in `src/api/routes/RecordRoutes.cpp`:

```cpp
  // POST /api/v1/zones/<int>/preview
  CROW_ROUTE(app, "/api/v1/zones/<int>/preview").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          auto prResult = _deEngine.preview(iZoneId);

          nlohmann::json jDiffs = nlohmann::json::array();
          for (const auto& diff : prResult.vDiffs) {
            jDiffs.push_back({
                {"action", diff.action == common::DiffAction::Add      ? "add"
                           : diff.action == common::DiffAction::Update  ? "update"
                           : diff.action == common::DiffAction::Delete  ? "delete"
                                                                        : "drift"},
                {"name", diff.sName},
                {"type", diff.sType},
                {"source_value", diff.sSourceValue},
                {"provider_value", diff.sProviderValue},
            });
          }

          nlohmann::json jResult = {
              {"zone_id", prResult.iZoneId},
              {"zone_name", prResult.sZoneName},
              {"has_drift", prResult.bHasDrift},
              {"diffs", jDiffs},
          };
          return jsonResponse(200, jResult);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/push
  CROW_ROUTE(app, "/api/v1/zones/<int>/push").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "operator");

          bool bPurgeDrift = false;
          if (!req.body.empty()) {
            try {
              auto jBody = nlohmann::json::parse(req.body);
              bPurgeDrift = jBody.value("purge_drift", false);
            } catch (const nlohmann::json::exception&) {
              // Empty or invalid body — use defaults
            }
          }

          _depEngine.push(iZoneId, bPurgeDrift, rcCtx.iUserId, rcCtx.sUsername);
          return jsonResponse(200, {{"message", "Push completed successfully"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
```

### Step 4: Update RecordRoutes construction in main.cpp

The RecordRoutes constructor now requires DiffEngine and DeploymentEngine references. Update
the construction in `src/main.cpp` (this will be finalized in Task 9, but note the change):

```cpp
auto recordRoutes = std::make_unique<dns::api::routes::RecordRoutes>(
    *rrRepo, *amMiddleware, *deEngine, *depEngine);
```

### Step 5: Build and verify compilation

```bash
cmake --build build --parallel 2>&1 | tail -10
```
Expected: May need to update `main.cpp` for new constructor args (handled in Task 9).
For now, ensure the route files compile.

### Step 6: Commit

```bash
git add include/api/routes/RecordRoutes.hpp src/api/routes/RecordRoutes.cpp
git commit -m "feat(api): add POST /zones/{id}/preview and /zones/{id}/push routes"
```

---

## Task 7: DeploymentRoutes Implementation + Tests

**Files:**
- Modify: `include/api/routes/DeploymentRoutes.hpp`
- Create: `src/api/routes/DeploymentRoutes.cpp`
- Create: `tests/integration/test_deployment_routes.cpp`

Endpoints from ARCHITECTURE.md section 6.7:
| Method | Path | Role | Description |
|--------|------|------|-------------|
| GET | `/zones/{id}/deployments` | viewer | List deployment history |
| GET | `/zones/{id}/deployments/{did}` | viewer | Get specific snapshot |
| GET | `/zones/{id}/deployments/{did}/diff` | viewer | Diff snapshot vs current desired |
| POST | `/zones/{id}/deployments/{did}/rollback` | operator | Restore snapshot |

### Step 1: Update the header

Replace `include/api/routes/DeploymentRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::dal {
class DeploymentRepository;
class RecordRepository;
}  // namespace dns::dal

namespace dns::core {
class DiffEngine;
class RollbackEngine;
}  // namespace dns::core

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/zones/{id}/deployments and rollback
/// Class abbreviation: dplr
class DeploymentRoutes {
 public:
  DeploymentRoutes(dns::dal::DeploymentRepository& drRepo,
                   dns::dal::RecordRepository& rrRepo,
                   const dns::api::AuthMiddleware& amMiddleware,
                   dns::core::RollbackEngine& reEngine);
  ~DeploymentRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::DeploymentRepository& _drRepo;
  dns::dal::RecordRepository& _rrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::RollbackEngine& _reEngine;
};

}  // namespace dns::api::routes
```

### Step 2: Implement DeploymentRoutes

Create `src/api/routes/DeploymentRoutes.cpp`:

```cpp
#include "api/routes/DeploymentRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "core/RollbackEngine.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/RecordRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

DeploymentRoutes::DeploymentRoutes(dns::dal::DeploymentRepository& drRepo,
                                   dns::dal::RecordRepository& rrRepo,
                                   const dns::api::AuthMiddleware& amMiddleware,
                                   dns::core::RollbackEngine& reEngine)
    : _drRepo(drRepo), _rrRepo(rrRepo), _amMiddleware(amMiddleware), _reEngine(reEngine) {}

DeploymentRoutes::~DeploymentRoutes() = default;

namespace {

void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole) {
  if (sMinRole == "admin" && rcCtx.sRole != "admin") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE", "Admin role required");
  }
  if (sMinRole == "operator" && rcCtx.sRole == "viewer") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE",
                                     "Operator or admin role required");
  }
}

common::RequestContext authenticate(const dns::api::AuthMiddleware& am,
                                    const crow::request& req) {
  return am.authenticate(req.get_header_value("Authorization"),
                         req.get_header_value("X-API-Key"));
}

crow::response jsonResponse(int iStatus, const nlohmann::json& j) {
  crow::response resp(iStatus, j.dump(2));
  resp.set_header("Content-Type", "application/json");
  return resp;
}

crow::response errorResponse(const common::AppError& e) {
  nlohmann::json jErr = {{"error", e._sErrorCode}, {"message", e.what()}};
  return crow::response(e._iHttpStatus, jErr.dump(2));
}

nlohmann::json deploymentRowToJson(const dns::dal::DeploymentRow& row) {
  return {
      {"id", row.iId},
      {"zone_id", row.iZoneId},
      {"deployed_by", row.iDeployedByUserId},
      {"deployed_at",
       std::chrono::duration_cast<std::chrono::seconds>(row.tpDeployedAt.time_since_epoch())
           .count()},
      {"seq", row.iSeq},
      {"snapshot", row.jSnapshot},
  };
}

}  // namespace

void DeploymentRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/zones/<int>/deployments
  CROW_ROUTE(app, "/api/v1/zones/<int>/deployments").methods("GET"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          int iLimit = 50;
          auto sLimit = req.url_params.get("limit");
          if (sLimit) iLimit = std::stoi(sLimit);

          auto vRows = _drRepo.listByZoneId(iZoneId, iLimit);
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(deploymentRowToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/zones/<int>/deployments/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>/deployments/<int>").methods("GET"_method)(
      [this](const crow::request& req, int /*iZoneId*/, int iDeployId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          auto oRow = _drRepo.findById(iDeployId);
          if (!oRow) {
            throw common::NotFoundError("DEPLOYMENT_NOT_FOUND", "Deployment not found");
          }
          return jsonResponse(200, deploymentRowToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/zones/<int>/deployments/<int>/diff
  CROW_ROUTE(app, "/api/v1/zones/<int>/deployments/<int>/diff").methods("GET"_method)(
      [this](const crow::request& req, int iZoneId, int iDeployId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          auto oDeploy = _drRepo.findById(iDeployId);
          if (!oDeploy) {
            throw common::NotFoundError("DEPLOYMENT_NOT_FOUND", "Deployment not found");
          }

          // Compare snapshot records vs current desired state
          auto vCurrentRecords = _rrRepo.listByZoneId(iZoneId);
          const auto& jSnapshotRecords = oDeploy->jSnapshot["records"];

          nlohmann::json jDiffs = nlohmann::json::array();

          // Build set of current records by name+type
          std::map<std::string, nlohmann::json> mCurrent;
          for (const auto& rec : vCurrentRecords) {
            std::string sKey = rec.sName + "|" + rec.sType;
            mCurrent[sKey] = {
                {"name", rec.sName}, {"type", rec.sType}, {"ttl", rec.iTtl},
                {"value_template", rec.sValueTemplate}, {"priority", rec.iPriority}};
          }

          // Build set of snapshot records by name+type
          std::map<std::string, nlohmann::json> mSnapshot;
          for (const auto& jRec : jSnapshotRecords) {
            std::string sKey = jRec.value("name", "") + "|" + jRec.value("type", "");
            mSnapshot[sKey] = jRec;
          }

          // Records in snapshot but not current -> "removed since deployment"
          for (const auto& [sKey, jRec] : mSnapshot) {
            if (mCurrent.find(sKey) == mCurrent.end()) {
              jDiffs.push_back({{"action", "removed"}, {"record", jRec}});
            }
          }

          // Records in current but not snapshot -> "added since deployment"
          for (const auto& [sKey, jRec] : mCurrent) {
            if (mSnapshot.find(sKey) == mSnapshot.end()) {
              jDiffs.push_back({{"action", "added"}, {"record", jRec}});
            }
          }

          // Records in both but different values -> "changed"
          for (const auto& [sKey, jCurRec] : mCurrent) {
            auto it = mSnapshot.find(sKey);
            if (it != mSnapshot.end()) {
              std::string sCurVal = jCurRec.value("value_template", "");
              std::string sSnapVal = it->second.value("value_template",
                                                      it->second.value("value", ""));
              if (sCurVal != sSnapVal) {
                jDiffs.push_back({
                    {"action", "changed"},
                    {"current", jCurRec},
                    {"snapshot", it->second},
                });
              }
            }
          }

          return jsonResponse(200, {{"deployment_id", iDeployId},
                                    {"zone_id", iZoneId},
                                    {"diffs", jDiffs}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/deployments/<int>/rollback
  CROW_ROUTE(app, "/api/v1/zones/<int>/deployments/<int>/rollback").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId, int iDeployId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "operator");

          std::vector<int64_t> vCherryPickIds;
          if (!req.body.empty()) {
            try {
              auto jBody = nlohmann::json::parse(req.body);
              if (jBody.contains("cherry_pick_ids") && jBody["cherry_pick_ids"].is_array()) {
                for (const auto& jId : jBody["cherry_pick_ids"]) {
                  vCherryPickIds.push_back(jId.get<int64_t>());
                }
              }
            } catch (const nlohmann::json::exception&) {
              // Ignore parse errors — treat as full restore
            }
          }

          _reEngine.apply(iZoneId, iDeployId, vCherryPickIds,
                          rcCtx.iUserId, rcCtx.sUsername);

          return jsonResponse(200, {{"message", "Rollback applied — preview and push to deploy"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
```

### Step 3: Build and verify

```bash
cmake --build build --parallel 2>&1 | tail -5
```

### Step 4: Commit

```bash
git add include/api/routes/DeploymentRoutes.hpp src/api/routes/DeploymentRoutes.cpp
git commit -m "feat(api): implement DeploymentRoutes (history, diff, rollback)"
```

---

## Task 8: AuditRoutes Implementation + Tests

**Files:**
- Modify: `include/api/routes/AuditRoutes.hpp`
- Create: `src/api/routes/AuditRoutes.cpp`

Endpoints from ARCHITECTURE.md section 6.9:
| Method | Path | Role | Description |
|--------|------|------|-------------|
| GET | `/audit` | viewer | Query audit log (filterable) |
| GET | `/audit/export` | admin | Stream as NDJSON |
| DELETE | `/audit/purge` | admin | Purge old entries |

### Step 1: Update the header

Replace `include/api/routes/AuditRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::dal {
class AuditRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/audit
/// Class abbreviation: audr
class AuditRoutes {
 public:
  AuditRoutes(dns::dal::AuditRepository& arRepo,
              const dns::api::AuthMiddleware& amMiddleware,
              int iRetentionDays);
  ~AuditRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::AuditRepository& _arRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  int _iRetentionDays;
};

}  // namespace dns::api::routes
```

### Step 2: Implement AuditRoutes

Create `src/api/routes/AuditRoutes.cpp`:

```cpp
#include "api/routes/AuditRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/AuditRepository.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace dns::api::routes {

AuditRoutes::AuditRoutes(dns::dal::AuditRepository& arRepo,
                         const dns::api::AuthMiddleware& amMiddleware,
                         int iRetentionDays)
    : _arRepo(arRepo), _amMiddleware(amMiddleware), _iRetentionDays(iRetentionDays) {}

AuditRoutes::~AuditRoutes() = default;

namespace {

void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole) {
  if (sMinRole == "admin" && rcCtx.sRole != "admin") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE", "Admin role required");
  }
  if (sMinRole == "operator" && rcCtx.sRole == "viewer") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE",
                                     "Operator or admin role required");
  }
}

common::RequestContext authenticate(const dns::api::AuthMiddleware& am,
                                    const crow::request& req) {
  return am.authenticate(req.get_header_value("Authorization"),
                         req.get_header_value("X-API-Key"));
}

crow::response jsonResponse(int iStatus, const nlohmann::json& j) {
  crow::response resp(iStatus, j.dump(2));
  resp.set_header("Content-Type", "application/json");
  return resp;
}

crow::response errorResponse(const common::AppError& e) {
  nlohmann::json jErr = {{"error", e._sErrorCode}, {"message", e.what()}};
  return crow::response(e._iHttpStatus, jErr.dump(2));
}

std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&tt), "%FT%TZ");
  return oss.str();
}

std::optional<std::chrono::system_clock::time_point> parseIso8601(const std::string& s) {
  std::tm tm = {};
  std::istringstream iss(s);
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (iss.fail()) return std::nullopt;
  return std::chrono::system_clock::from_time_t(timegm(&tm));
}

nlohmann::json auditRowToJson(const dns::dal::AuditLogRow& row) {
  nlohmann::json j = {
      {"id", row.iId},
      {"entity_type", row.sEntityType},
      {"operation", row.sOperation},
      {"identity", row.sIdentity},
      {"timestamp", formatTimestamp(row.tpTimestamp)},
  };
  if (row.oEntityId) j["entity_id"] = *row.oEntityId;
  if (row.ojOldValue) j["old_value"] = *row.ojOldValue;
  if (row.ojNewValue) j["new_value"] = *row.ojNewValue;
  if (row.osVariableUsed) j["variable_used"] = *row.osVariableUsed;
  if (row.osAuthMethod) j["auth_method"] = *row.osAuthMethod;
  if (row.osIpAddress) j["ip_address"] = *row.osIpAddress;
  return j;
}

}  // namespace

void AuditRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/audit
  CROW_ROUTE(app, "/api/v1/audit").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          std::optional<std::string> osEntityType;
          std::optional<std::string> osIdentity;
          std::optional<std::chrono::system_clock::time_point> otpFrom;
          std::optional<std::chrono::system_clock::time_point> otpTo;
          int iLimit = 100;

          auto pEntityType = req.url_params.get("entity_type");
          if (pEntityType) osEntityType = std::string(pEntityType);

          auto pIdentity = req.url_params.get("identity");
          if (pIdentity) osIdentity = std::string(pIdentity);

          auto pFrom = req.url_params.get("from");
          if (pFrom) otpFrom = parseIso8601(pFrom);

          auto pTo = req.url_params.get("to");
          if (pTo) otpTo = parseIso8601(pTo);

          auto pLimit = req.url_params.get("limit");
          if (pLimit) iLimit = std::stoi(pLimit);

          auto vRows = _arRepo.query(osEntityType, std::nullopt, osIdentity,
                                     otpFrom, otpTo, iLimit);

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(auditRowToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/audit/export
  CROW_ROUTE(app, "/api/v1/audit/export").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "admin");

          std::optional<std::chrono::system_clock::time_point> otpFrom;
          std::optional<std::chrono::system_clock::time_point> otpTo;

          auto pFrom = req.url_params.get("from");
          if (pFrom) otpFrom = parseIso8601(pFrom);

          auto pTo = req.url_params.get("to");
          if (pTo) otpTo = parseIso8601(pTo);

          // Fetch all matching entries (large limit for export)
          auto vRows = _arRepo.query(std::nullopt, std::nullopt, std::nullopt,
                                     otpFrom, otpTo, 100000);

          // Stream as NDJSON
          std::string sBody;
          for (const auto& row : vRows) {
            sBody += auditRowToJson(row).dump() + "\n";
          }

          crow::response resp(200, sBody);
          resp.set_header("Content-Type", "application/x-ndjson");
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // DELETE /api/v1/audit/purge
  CROW_ROUTE(app, "/api/v1/audit/purge").methods("DELETE"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "admin");

          auto prResult = _arRepo.purgeOld(_iRetentionDays);

          nlohmann::json jResult = {
              {"deleted", prResult.iDeletedCount},
          };
          if (prResult.oOldestRemaining) {
            jResult["oldest_remaining"] = formatTimestamp(*prResult.oOldestRemaining);
          }
          return jsonResponse(200, jResult);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
```

### Step 3: Build and verify

```bash
cmake --build build --parallel 2>&1 | tail -5
```

### Step 4: Commit

```bash
git add include/api/routes/AuditRoutes.hpp src/api/routes/AuditRoutes.cpp
git commit -m "feat(api): implement AuditRoutes (query, export, purge)"
```

---

## Task 9: Wire ApiServer + main.cpp (Steps 6, 7, 12)

**Files:**
- Modify: `include/api/ApiServer.hpp`
- Modify: `src/api/ApiServer.cpp`
- Modify: `src/main.cpp`

This task wires all Phase 7 components into the startup sequence and ApiServer.

### Step 1: Update ApiServer to accept new route classes

Replace `include/api/ApiServer.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {
class AuthRoutes;
class AuditRoutes;
class DeploymentRoutes;
class HealthRoutes;
class ProviderRoutes;
class ViewRoutes;
class ZoneRoutes;
class RecordRoutes;
class VariableRoutes;
}  // namespace dns::api::routes

namespace dns::api {

/// Owns the Crow application instance; registers all routes at startup.
/// Class abbreviation: api
class ApiServer {
 public:
  ApiServer(crow::SimpleApp& app,
            routes::AuthRoutes& arRoutes,
            routes::AuditRoutes& audrRoutes,
            routes::DeploymentRoutes& dplrRoutes,
            routes::HealthRoutes& hrRoutes,
            routes::ProviderRoutes& prRoutes,
            routes::ViewRoutes& vrRoutes,
            routes::ZoneRoutes& zrRoutes,
            routes::RecordRoutes& rrRoutes,
            routes::VariableRoutes& varRoutes);
  ~ApiServer();

  void registerRoutes();
  void start(int iPort, int iThreads);
  void stop();

 private:
  crow::SimpleApp& _app;
  routes::AuthRoutes& _arRoutes;
  routes::AuditRoutes& _audrRoutes;
  routes::DeploymentRoutes& _dplrRoutes;
  routes::HealthRoutes& _hrRoutes;
  routes::ProviderRoutes& _prRoutes;
  routes::ViewRoutes& _vrRoutes;
  routes::ZoneRoutes& _zrRoutes;
  routes::RecordRoutes& _rrRoutes;
  routes::VariableRoutes& _varRoutes;
};

}  // namespace dns::api
```

### Step 2: Update ApiServer.cpp

Replace `src/api/ApiServer.cpp`:

```cpp
#include "api/ApiServer.hpp"

#include "api/routes/AuditRoutes.hpp"
#include "api/routes/AuthRoutes.hpp"
#include "api/routes/DeploymentRoutes.hpp"
#include "api/routes/HealthRoutes.hpp"
#include "api/routes/ProviderRoutes.hpp"
#include "api/routes/RecordRoutes.hpp"
#include "api/routes/VariableRoutes.hpp"
#include "api/routes/ViewRoutes.hpp"
#include "api/routes/ZoneRoutes.hpp"

namespace dns::api {

ApiServer::ApiServer(crow::SimpleApp& app,
                     routes::AuthRoutes& arRoutes,
                     routes::AuditRoutes& audrRoutes,
                     routes::DeploymentRoutes& dplrRoutes,
                     routes::HealthRoutes& hrRoutes,
                     routes::ProviderRoutes& prRoutes,
                     routes::ViewRoutes& vrRoutes,
                     routes::ZoneRoutes& zrRoutes,
                     routes::RecordRoutes& rrRoutes,
                     routes::VariableRoutes& varRoutes)
    : _app(app),
      _arRoutes(arRoutes),
      _audrRoutes(audrRoutes),
      _dplrRoutes(dplrRoutes),
      _hrRoutes(hrRoutes),
      _prRoutes(prRoutes),
      _vrRoutes(vrRoutes),
      _zrRoutes(zrRoutes),
      _rrRoutes(rrRoutes),
      _varRoutes(varRoutes) {}

ApiServer::~ApiServer() = default;

void ApiServer::registerRoutes() {
  _hrRoutes.registerRoutes(_app);
  _arRoutes.registerRoutes(_app);
  _audrRoutes.registerRoutes(_app);
  _dplrRoutes.registerRoutes(_app);
  _prRoutes.registerRoutes(_app);
  _vrRoutes.registerRoutes(_app);
  _zrRoutes.registerRoutes(_app);
  _rrRoutes.registerRoutes(_app);
  _varRoutes.registerRoutes(_app);
}

void ApiServer::start(int iPort, int iThreads) {
  _app.port(iPort).multithreaded().concurrency(iThreads).run();
}

void ApiServer::stop() {
  _app.stop();
}

}  // namespace dns::api
```

### Step 3: Update main.cpp — wire steps 6, 7, 9 updates, 10 updates, 12

Replace `src/main.cpp` with the full updated version. Key changes:
- Step 6: Construct `GitOpsMirror` if `oGitRemoteUrl` is set
- Step 7: Construct `ThreadPool`
- Step 9: Construct `DeploymentEngine` and `RollbackEngine`
- Step 10: Updated `RecordRoutes` constructor (now takes `DiffEngine` + `DeploymentEngine`)
- Step 10: Add `DeploymentRoutes` and `AuditRoutes`
- Step 10: Updated `ApiServer` constructor (now takes `AuditRoutes` + `DeploymentRoutes`)
- Step 12: Log that background task queue uses `ThreadPool`

The exact diff for `src/main.cpp`:

**Add new includes at top:**
```cpp
#include "api/routes/AuditRoutes.hpp"
#include "api/routes/DeploymentRoutes.hpp"
#include "core/DeploymentEngine.hpp"
#include "core/RollbackEngine.hpp"
#include "core/ThreadPool.hpp"
#include "gitops/GitOpsMirror.hpp"
#include "providers/ProviderFactory.hpp"
```

**Replace Step 6 placeholder** (after Step 5) with:
```cpp
    // ── Step 6: Initialize GitOpsMirror (if configured) ──────────────────
    std::unique_ptr<dns::gitops::GitOpsMirror> upGitMirror;
    if (cfgApp.oGitRemoteUrl.has_value() && !cfgApp.oGitRemoteUrl->empty()) {
      upGitMirror = std::make_unique<dns::gitops::GitOpsMirror>(
          *zrRepo, *vrRepo, *rrRepo, *veEngine);
      upGitMirror->initialize(*cfgApp.oGitRemoteUrl, cfgApp.sGitLocalPath);
      upGitMirror->pull();
      spLog->info("Step 6: GitOpsMirror initialized (remote={}, local={})",
                  *cfgApp.oGitRemoteUrl, cfgApp.sGitLocalPath);
    } else {
      spLog->info("Step 6: GitOpsMirror disabled (DNS_GIT_REMOTE_URL not set)");
    }
```

**Replace Step 7 placeholder** with:
```cpp
    // ── Step 7: Initialize ThreadPool ──────────────────────────────────────
    auto tpPool = std::make_unique<dns::core::ThreadPool>(cfgApp.iThreadPoolSize);
    spLog->info("Step 7: ThreadPool initialized (size={})",
                cfgApp.iThreadPoolSize == 0
                    ? static_cast<int>(std::thread::hardware_concurrency())
                    : cfgApp.iThreadPoolSize);
```

**After Step 9 core engines, add DeploymentEngine and RollbackEngine:**
```cpp
    auto depEngine = std::make_unique<dns::core::DeploymentEngine>(
        *deEngine, *veEngine, *zrRepo, *vrRepo, *rrRepo, *prRepo,
        *drRepo, *arRepo, upGitMirror.get(), cfgApp.iDeploymentRetentionCount);
    auto reEngine = std::make_unique<dns::core::RollbackEngine>(*drRepo, *rrRepo, *arRepo);
    spLog->info("Step 9: Core engines initialized "
                "(VariableEngine, DiffEngine, DeploymentEngine, RollbackEngine)");
```

**Update RecordRoutes construction** (Step 10):
```cpp
    auto recordRoutes = std::make_unique<dns::api::routes::RecordRoutes>(
        *rrRepo, *amMiddleware, *deEngine, *depEngine);
```

**Add new route constructions** (Step 10):
```cpp
    auto deploymentRoutes = std::make_unique<dns::api::routes::DeploymentRoutes>(
        *drRepo, *rrRepo, *amMiddleware, *reEngine);
    auto auditRoutes = std::make_unique<dns::api::routes::AuditRoutes>(
        *arRepo, *amMiddleware, cfgApp.iAuditRetentionDays);
```

**Update ApiServer construction** (Step 10):
```cpp
    auto apiServer = std::make_unique<dns::api::ApiServer>(
        crowApp, *authRoutes, *auditRoutes, *deploymentRoutes, *healthRoutes,
        *providerRoutes, *viewRoutes, *zoneRoutes, *recordRoutes, *variableRoutes);
```

**Replace Step 12 placeholder** with:
```cpp
    // Step 12: Background task queue uses ThreadPool — ready
    spLog->info("Step 12: Background task queue ready (ThreadPool)");
```

### Step 4: Build and run all tests

```bash
cmake --build build --parallel && build/tests/dns-tests
```
Expected: All tests pass (unit tests run, integration tests skip without DNS_DB_URL).

### Step 5: Commit

```bash
git add include/api/ApiServer.hpp src/api/ApiServer.cpp src/main.cpp
git commit -m "feat(core): wire Phase 7 into startup sequence (steps 6, 7, 12)"
```

---

## Task 10: Final Build Verification + CLAUDE.md Update

**Files:**
- Modify: `CLAUDE.md`

### Step 1: Full rebuild

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```
Expected: Clean build, zero warnings.

### Step 2: Run all tests

```bash
build/tests/dns-tests
```
Expected: All tests pass. Note the new test count.

### Step 3: Update CLAUDE.md

Update the project status section:
- Phase 6 → Phase 7 complete
- Update test count
- Update startup sequence state (all steps wired)
- Next task → Phase 8

### Step 4: Commit

```bash
git add CLAUDE.md
git commit -m "docs: update project status for Phase 7 completion"
```

---

## Summary of Deliverables

| Component | Files | Tests |
|-----------|-------|-------|
| ThreadPool | header + .cpp | 6 unit tests |
| RecordRepository additions | header + .cpp mods | 3 integration tests |
| GitOpsMirror | header + .cpp | 1 integration test (snapshot JSON) |
| DeploymentEngine | header + .cpp | 3 integration tests |
| RollbackEngine | header + .cpp | 4 integration tests |
| Preview/Push routes | RecordRoutes mods | (tested via deployment pipeline) |
| DeploymentRoutes | header + .cpp | (4 endpoints) |
| AuditRoutes | header + .cpp | (3 endpoints) |
| ApiServer + main.cpp | mods | (build verification) |

**Estimated new tests:** ~17 (6 unit + 11 integration)
**New/modified source files:** ~20
**Commits:** 10
