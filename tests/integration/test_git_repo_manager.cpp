// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "gitops/GitRepoManager.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <pqxx/pqxx>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/VariableRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "security/CryptoService.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
const std::string kTestMasterKey =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
}  // namespace

class GitRepoManagerTest : public ::testing::Test {
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
    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _varRepo = std::make_unique<dns::dal::VariableRepository>(*_cpPool);
    _veEngine = std::make_unique<dns::core::VariableEngine>(*_varRepo);

    // Clean test data
    {
      auto cg = _cpPool->checkout();
      pqxx::work txn(*cg);
      txn.exec("DELETE FROM records");
      txn.exec("DELETE FROM variables");
      txn.exec("DELETE FROM deployments");
      txn.exec("UPDATE zones SET git_repo_id = NULL");
      txn.exec("DELETE FROM zones");
      txn.exec("DELETE FROM git_repos");
      txn.exec("DELETE FROM view_providers");
      txn.exec("DELETE FROM views");
      txn.commit();
    }

    _sTmpDir = std::filesystem::temp_directory_path() / "grmgr-test";
    std::filesystem::remove_all(_sTmpDir);
    std::filesystem::create_directories(_sTmpDir);
  }

  void TearDown() override {
    std::filesystem::remove_all(_sTmpDir);
  }

  std::string _sDbUrl;
  std::filesystem::path _sTmpDir;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::GitRepoRepository> _grRepo;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::dal::VariableRepository> _varRepo;
  std::unique_ptr<dns::core::VariableEngine> _veEngine;
};

TEST_F(GitRepoManagerTest, InitializeWithNoRepos) {
  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());
  EXPECT_NO_THROW(mgr.initialize());
}

TEST_F(GitRepoManagerTest, CommitZoneSnapshotNoOpWhenNoGitRepoAssigned) {
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());
  mgr.initialize();

  // Should be a no-op since zone has no git_repo_id
  EXPECT_NO_THROW(mgr.commitZoneSnapshot(iZoneId, "test-user"));
}

TEST_F(GitRepoManagerTest, RemoveRepoUnloadsMirror) {
  // Create a local-only repo
  auto sRepoDir = _sTmpDir / "local-repo";
  std::system(("git init " + sRepoDir.string() + " 2>/dev/null").c_str());

  int64_t iRepoId = _grRepo->create("test-repo", "", "none", "",
                                      "main", sRepoDir.string(), "");

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());
  mgr.initialize();

  // Remove should succeed without error
  EXPECT_NO_THROW(mgr.removeRepo(iRepoId));
  // Removing again should also be fine (idempotent)
  EXPECT_NO_THROW(mgr.removeRepo(iRepoId));
}

TEST_F(GitRepoManagerTest, ReadFile_RejectsAbsolutePath) {
  // Create a local-only repo so findById() succeeds
  auto sRepoDir = _sTmpDir / "local-repo";
  std::system(("git init " + sRepoDir.string() + " 2>/dev/null").c_str());

  int64_t iRepoId = _grRepo->create("test-repo", "", "none", "",
                                      "main", sRepoDir.string(), "");

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());

  EXPECT_THROW(mgr.readFile(iRepoId, "/etc/passwd"),
               dns::common::ValidationError);
}

TEST_F(GitRepoManagerTest, ReadFile_RejectsPathTraversal) {
  // Create a local-only repo so findById() succeeds
  auto sRepoDir = _sTmpDir / "local-repo";
  std::system(("git init " + sRepoDir.string() + " 2>/dev/null").c_str());

  int64_t iRepoId = _grRepo->create("test-repo", "", "none", "",
                                      "main", sRepoDir.string(), "");

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());

  EXPECT_THROW(mgr.readFile(iRepoId, "../../etc/passwd"),
               dns::common::ValidationError);
}

// ---------------------------------------------------------------------------
// Git availability probe (v1.1.4)
//
// An unreachable remote used to block the deployment thread until the OS TCP
// timeout and then fail silently. The probe must detect it fast and report the
// skip to the caller so the deployment can record it.
// ---------------------------------------------------------------------------

TEST_F(GitRepoManagerTest, GitStatusNotConfiguredWhenZoneHasNoRepo) {
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());
  mgr.initialize();

  auto gs = mgr.gitStatusForZone(iZoneId);
  EXPECT_FALSE(gs.bConfigured);
}

TEST_F(GitRepoManagerTest, GitStatusReportsUnreachableRemoteWithReason) {
  // 127.0.0.1:1 is reserved and never listening — a deterministic refusal.
  int64_t iRepoId = _grRepo->create("dead-repo", "ssh://git@127.0.0.1:1/org/repo.git",
                                      "none", "", "main", "", "");
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt,
                                     false, false, iRepoId);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string(),
                                   /*iProbeTimeoutMs=*/1000,
                                   /*iProbeCacheTtlSeconds=*/0);
  mgr.initialize();

  auto gs = mgr.gitStatusForZone(iZoneId);
  EXPECT_TRUE(gs.bConfigured);
  EXPECT_EQ(gs.iRepoId, iRepoId);
  EXPECT_EQ(gs.sRepoName, "dead-repo");
  EXPECT_FALSE(gs.bReachable);
  EXPECT_FALSE(gs.sError.empty());
  EXPECT_FALSE(gs.sCheckedAt.empty());
}

// The mirror must actually be loaded for this to exercise the probe gate
// rather than the "mirror not loaded" path, so the remote is reachable at
// startup and only then goes away. A local path remote makes that reproducible:
// the probe resolves it with a filesystem existence check.
TEST_F(GitRepoManagerTest, CommitZoneSnapshotSkipsWhenLoadedMirrorsRemoteGoesAway) {
  auto sRemoteDir = _sTmpDir / "origin.git";
  ASSERT_EQ(std::system(("git init --bare " + sRemoteDir.string() + " 2>/dev/null").c_str()), 0);

  int64_t iRepoId = _grRepo->create("vanishing-repo", sRemoteDir.string(),
                                      "none", "", "master", "", "");
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt,
                                     false, false, iRepoId);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string(),
                                   /*iProbeTimeoutMs=*/1000,
                                   /*iProbeCacheTtlSeconds=*/0);
  mgr.initialize();

  // Confirm the mirror really did load, otherwise this test proves nothing.
  ASSERT_TRUE(mgr.gitStatusForZone(iZoneId).bReachable);

  // Now take the remote away.
  std::filesystem::remove_all(sRemoteDir);

  auto tStart = std::chrono::steady_clock::now();
  auto outcome = mgr.commitZoneSnapshot(iZoneId, "test-user");
  auto iElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tStart)
                        .count();

  EXPECT_TRUE(outcome.bConfigured);
  EXPECT_FALSE(outcome.bCommitted);
  EXPECT_TRUE(outcome.skipped());
  EXPECT_EQ(outcome.sRepoName, "vanishing-repo");
  // Must be the probe gate, not the mirror-not-loaded branch.
  EXPECT_NE(outcome.sReason.find("unreachable"), std::string::npos)
      << "actual reason: " << outcome.sReason;
  EXPECT_LT(iElapsedMs, 5000);
}

// A remote that was never reachable cannot load a mirror at startup; the commit
// must still report a skip rather than silently doing nothing.
TEST_F(GitRepoManagerTest, CommitZoneSnapshotSkipsUnloadableRemoteQuickly) {
  // 127.0.0.1:1 is reserved and never listening — a deterministic refusal.
  int64_t iRepoId = _grRepo->create("dead-repo", "ssh://git@127.0.0.1:1/org/repo.git",
                                      "none", "", "main", "", "");
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt,
                                     false, false, iRepoId);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string(),
                                   /*iProbeTimeoutMs=*/1000,
                                   /*iProbeCacheTtlSeconds=*/0);
  mgr.initialize();

  auto tStart = std::chrono::steady_clock::now();
  auto outcome = mgr.commitZoneSnapshot(iZoneId, "test-user");
  auto iElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tStart)
                        .count();

  EXPECT_TRUE(outcome.bConfigured);
  EXPECT_TRUE(outcome.skipped());
  EXPECT_FALSE(outcome.sReason.empty());
  EXPECT_LT(iElapsedMs, 5000);
}

TEST_F(GitRepoManagerTest, CommitZoneSnapshotOutcomeNotConfiguredWhenNoRepo) {
  int64_t iViewId = _vrRepo->create("test-view", "Test view");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);

  dns::gitops::GitRepoManager mgr(*_grRepo, *_zrRepo, *_vrRepo, *_rrRepo,
                                   *_veEngine, _sTmpDir.string());
  mgr.initialize();

  auto outcome = mgr.commitZoneSnapshot(iZoneId, "test-user");
  EXPECT_FALSE(outcome.bConfigured);
  EXPECT_FALSE(outcome.skipped());
}
