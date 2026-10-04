// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "gitops/GitRepoManager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <git2.h>
#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/TimeUtils.hpp"
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
                               const std::string& sBasePath,
                               int iProbeTimeoutMs,
                               int iProbeCacheTtlSeconds,
                               int iServerTimeoutMs)
    : _grRepo(grRepo), _zrRepo(zrRepo), _vrRepo(vrRepo),
      _rrRepo(rrRepo), _veEngine(veEngine), _sBasePath(sBasePath),
      _grpProbe(iProbeTimeoutMs, iProbeCacheTtlSeconds) {
  // Bound libgit2's own network waits so a stalled remote can no longer hang a
  // deployment thread until the OS TCP timeout. These options are process-wide
  // and govern the HTTP(S) transport only — SSH runs through libssh2 and
  // ignores them, which is why GitRepoProbe does a TCP pre-probe as well.
  if (iServerTimeoutMs > 0) {
    git_libgit2_init();
    git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, iServerTimeoutMs);
    git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, iServerTimeoutMs);
    git_libgit2_shutdown();
  }
}

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
  _grpProbe.invalidate(iRepoId);

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
  _grpProbe.invalidate(iRepoId);
}

GitRepoMirror* GitRepoManager::findMirror(int64_t iRepoId) {
  auto it = _mMirrors.find(iRepoId);
  return (it != _mMirrors.end()) ? it->second.get() : nullptr;
}

std::string GitRepoManager::buildSnapshotJson(int64_t iZoneId,
                                              const std::string& sActor) const {
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

  nlohmann::json j = {
      {"zone", oZone->sName},
      {"view", sViewName},
      {"generated_at", dns::common::nowIso8601()},
      {"generated_by", sActor},
      {"records", jRecords},
  };
  return j.dump(2);
}

GitProbeResult GitRepoManager::probeRepo(int64_t iRepoId) {
  auto oRepo = _grRepo.findById(iRepoId);
  if (!oRepo) {
    GitProbeResult result;
    result.bReachable = false;
    result.sError = "Git repo " + std::to_string(iRepoId) + " not found";
    result.tpCheckedAt = std::chrono::system_clock::now();
    return result;
  }
  return _grpProbe.probe(iRepoId, oRepo->sRemoteUrl);
}

common::GitStatus GitRepoManager::gitStatusForZone(int64_t iZoneId) {
  common::GitStatus gs;

  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone || !oZone->oGitRepoId.has_value() || *oZone->oGitRepoId == 0) {
    return gs;  // bConfigured stays false — nothing to probe
  }

  gs.bConfigured = true;
  gs.iRepoId = *oZone->oGitRepoId;

  auto oRepo = _grRepo.findById(gs.iRepoId);
  gs.sRepoName = oRepo ? oRepo->sName : ("repo " + std::to_string(gs.iRepoId));

  auto result = probeRepo(gs.iRepoId);
  gs.bReachable = result.bReachable;
  gs.sError = result.sError;
  gs.sCheckedAt = dns::common::toIso8601(
      std::chrono::system_clock::to_time_t(result.tpCheckedAt));
  return gs;
}

GitCommitOutcome GitRepoManager::commitZoneSnapshot(int64_t iZoneId,
                                                   const std::string& sActor) {
  auto spLog = common::Logger::get();
  GitCommitOutcome outcome;

  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone || !oZone->oGitRepoId.has_value() || *oZone->oGitRepoId == 0) {
    // Zone has no git repo assigned — no-op
    return outcome;
  }

  int64_t iRepoId = *oZone->oGitRepoId;
  outcome.bConfigured = true;
  outcome.iRepoId = iRepoId;

  auto oRepo = _grRepo.findById(iRepoId);
  outcome.sRepoName = oRepo ? oRepo->sName : ("repo " + std::to_string(iRepoId));

  GitRepoMirror* pMirror = nullptr;
  {
    std::lock_guard lock(_mtx);
    pMirror = findMirror(iRepoId);
  }

  if (!pMirror) {
    outcome.sReason = "git repo mirror is not loaded";
    spLog->warn("GitRepoManager: repo {} not loaded — skipping commit for zone {}",
                iRepoId, iZoneId);
    return outcome;
  }

  // Check reachability before touching the mirror. Without this an unreachable
  // remote blocks here until the OS TCP timeout and then fails anyway.
  auto probeResult = _grpProbe.probe(iRepoId, oRepo ? oRepo->sRemoteUrl : std::string());
  if (!probeResult.bReachable) {
    outcome.sReason = "git remote unreachable: " + probeResult.sError;
    spLog->warn("GitRepoManager: skipping commit for zone {} — {}", iZoneId, outcome.sReason);
    _grRepo.updateSyncStatus(iRepoId, "failed", probeResult.sError);
    return outcome;
  }

  try {
    auto oView = _vrRepo.findById(oZone->iViewId);
    std::string sViewName = oView ? oView->sName : "unknown";

    std::string sRelPath = sViewName + "/" + oZone->sName + ".json";
    std::string sJson = buildSnapshotJson(iZoneId, sActor);
    std::string sBranch = oZone->oGitBranch.value_or("");
    std::string sMsg = "Update " + oZone->sName + " by " + sActor + " via API";

    pMirror->commitSnapshot(sRelPath, sJson, sMsg, sBranch);
    outcome.bCommitted = true;
    spLog->info("GitRepoManager: committed zone '{}' to repo {} branch '{}'",
                oZone->sName, iRepoId,
                sBranch.empty() ? "(default)" : sBranch);
  } catch (const std::exception& ex) {
    outcome.sReason = std::string("git commit failed: ") + ex.what();
    spLog->error("GitRepoManager: commit failed for zone {}: {}", iZoneId, ex.what());
    // The remote answered a TCP probe but the operation still failed, so the
    // cached "reachable" verdict is stale — force a fresh probe next time.
    _grpProbe.invalidate(iRepoId);
  }

  return outcome;
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
    _grpProbe.probeNow(iRepoId, oRepo->sRemoteUrl);
    return "";  // Success
  } catch (const std::exception& ex) {
    std::filesystem::remove_all(sTmpDir);
    return ex.what();
  }
}

std::string GitRepoManager::readFile(int64_t iRepoId, const std::string& sRelativePath) {
  auto oRepo = _grRepo.findById(iRepoId);
  if (!oRepo) {
    throw common::NotFoundError("REPO_NOT_FOUND",
                                "Git repo " + std::to_string(iRepoId) + " not found");
  }

  auto sLocalPath = computeLocalPath(_sBasePath, oRepo->iId, oRepo->sLocalPath);
  auto sBase      = std::filesystem::weakly_canonical(sLocalPath);
  auto sResolved  = std::filesystem::weakly_canonical(sBase / sRelativePath);

  // Enforce path confinement: resolved path must remain inside the repo root.
  // weakly_canonical collapses ".." segments; if sRelativePath is absolute it
  // replaces sBase entirely, producing a path that will fail the prefix check.
  // Trailing '/' prevents directory-prefix confusion (e.g. /repos/1 vs /repos/10).
  auto sBasePrefix = sBase.string() + "/";
  if (sResolved.string().find(sBasePrefix) != 0) {
    throw common::ValidationError("PATH_TRAVERSAL",
                                  "Backup path escapes repository boundary");
  }
  auto sFullPath = sResolved;

  std::ifstream ifs(sFullPath);
  if (!ifs.is_open()) {
    throw common::NotFoundError("FILE_NOT_FOUND",
                                "File not found in git repo: " + sRelativePath);
  }

  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

void GitRepoManager::writeAndCommit(int64_t iRepoId, const std::string& sRelativePath,
                                    const std::string& sContent,
                                    const std::string& sCommitMessage) {
  std::lock_guard lock(_mtx);
  auto* pMirror = findMirror(iRepoId);
  if (!pMirror) {
    throw common::NotFoundError("MIRROR_NOT_FOUND",
                                "No active mirror for git repo " + std::to_string(iRepoId));
  }

  pMirror->commitSnapshot(sRelativePath, sContent, sCommitMessage);
}

}  // namespace dns::gitops
