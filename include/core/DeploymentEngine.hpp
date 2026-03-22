#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/Types.hpp"
#include "core/VariableEngine.hpp"

namespace dns::dal {
class AuditRepository;
class DeploymentRepository;
class ProviderRepository;
class RecordRepository;
class SystemConfigRepository;
class SystemLogRepository;
class ViewRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::gitops {
class GitRepoManager;
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
                   dns::dal::SystemConfigRepository& scrRepo,
                   dns::dal::SystemLogRepository& slrRepo,
                   dns::gitops::GitRepoManager* pGitRepoManager,
                   int iRetentionCount);
  ~DeploymentEngine();

  /// Execute the full push pipeline for a zone.
  /// Throws DeploymentLockedError if the zone is already being deployed.
  /// Throws ProviderError on provider failure.
  void push(int64_t iZoneId,
            const std::vector<common::DriftAction>& vDriftActions,
            int64_t iActorUserId, const common::AuditContext& acCtx);

  /// Capture current provider state as a baseline snapshot.
  /// No diff/push — records captured as-is from the provider.
  /// Returns the new deployment ID.
  int64_t capture(int64_t iZoneId, int64_t iActorUserId,
                  const common::AuditContext& acCtx, const std::string& sGeneratedBy);

  /// Reorder diffs for safe execution: deletes first, then updates, adds, drifts.
  /// Drift records with action="delete" are moved into the delete phase.
  static std::vector<common::RecordDiff> partitionDiffsForExecution(
      const std::vector<common::RecordDiff>& vDiffs,
      const std::map<std::string, std::string>& mDriftActionMap);

 private:
  /// Build the deployment snapshot JSON from current records.
  nlohmann::json buildSnapshot(int64_t iZoneId, const std::string& sIdentity,
                               const SysContext& sysCtx) const;

  /// Build a capture snapshot from live provider records (no diff/push).
  nlohmann::json buildCaptureSnapshot(int64_t iZoneId,
                                      const std::vector<common::DnsRecord>& vLiveRecords,
                                      const std::string& sIdentity,
                                      const std::string& sGeneratedBy) const;

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
  dns::dal::SystemConfigRepository& _scrRepo;
  dns::dal::SystemLogRepository& _slrRepo;
  dns::gitops::GitRepoManager* _pGitRepoManager;
  int _iRetentionCount;

  std::unordered_map<int64_t, std::unique_ptr<std::mutex>> _mZoneMutexes;
  std::mutex _mtxMap;
};

}  // namespace dns::core
