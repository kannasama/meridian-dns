#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/Types.hpp"
#include "gitops/GitRepoProbe.hpp"

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

/// Outcome of an attempted zone snapshot commit.
/// Class abbreviation: gco
struct GitCommitOutcome {
  bool bConfigured = false;  // zone had a git repo assigned
  bool bCommitted = false;   // snapshot reached the mirror
  int64_t iRepoId = 0;
  std::string sRepoName;
  std::string sReason;       // why it was skipped; empty when committed

  /// True when a repo was assigned but the snapshot did not get written.
  bool skipped() const { return bConfigured && !bCommitted; }
};

/// Manages multiple GitRepoMirror instances, one per enabled git_repos row.
/// Class abbreviation: grmgr
class GitRepoManager {
 public:
  GitRepoManager(dns::dal::GitRepoRepository& grRepo,
                 dns::dal::ZoneRepository& zrRepo,
                 dns::dal::ViewRepository& vrRepo,
                 dns::dal::RecordRepository& rrRepo,
                 dns::core::VariableEngine& veEngine,
                 const std::string& sBasePath,
                 int iProbeTimeoutMs = 3000,
                 int iProbeCacheTtlSeconds = 30,
                 int iServerTimeoutMs = 20000);
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
  /// Probes the remote first and skips rather than blocking when unreachable;
  /// the returned outcome says what happened so the caller can record it.
  GitCommitOutcome commitZoneSnapshot(int64_t iZoneId, const std::string& sActor);

  /// Reachability of the git repo assigned to a zone, for preview reporting.
  /// bConfigured is false when the zone has no repo assigned.
  common::GitStatus gitStatusForZone(int64_t iZoneId);

  /// Cache-aware reachability probe for a single repo.
  GitProbeResult probeRepo(int64_t iRepoId);

  /// Pull (fetch) latest for all enabled mirrors.
  void pullAll();

  /// Pull for a single repo by ID.
  void pullRepo(int64_t iRepoId);

  /// Test connection for a repo: clone to temp dir, verify auth works.
  /// Returns empty string on success, error message on failure.
  std::string testConnection(int64_t iRepoId);

  /// Build the zone snapshot JSON (same format as GitOpsMirror::buildSnapshotJson).
  std::string buildSnapshotJson(int64_t iZoneId, const std::string& sActor) const;

  /// Read a file from a mirror's working directory.
  std::string readFile(int64_t iRepoId, const std::string& sRelativePath);

  /// Write a file and commit+push via the mirror.
  void writeAndCommit(int64_t iRepoId, const std::string& sRelativePath,
                      const std::string& sContent, const std::string& sCommitMessage);

 private:
  GitRepoMirror* findMirror(int64_t iRepoId);

  dns::dal::GitRepoRepository& _grRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::core::VariableEngine& _veEngine;
  std::string _sBasePath;

  GitRepoProbe _grpProbe;

  std::unordered_map<int64_t, std::unique_ptr<GitRepoMirror>> _mMirrors;
  std::mutex _mtx;
};

}  // namespace dns::gitops
