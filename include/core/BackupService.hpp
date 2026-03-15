#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::dal {
class ConnectionPool;
class SettingsRepository;
class RoleRepository;
class GroupRepository;
class UserRepository;
class IdpRepository;
class GitRepoRepository;
class ProviderRepository;
class ViewRepository;
class ZoneRepository;
class RecordRepository;
class VariableRepository;
}  // namespace dns::dal

namespace dns::core {

/// Summary of entity-level changes from a restore operation.
struct RestoreSummary {
  std::string sEntityType;
  int iCreated = 0;
  int iUpdated = 0;
  int iSkipped = 0;
};

/// Result of a restore preview or apply.
/// Class abbreviation: rr (restore result)
struct RestoreResult {
  bool bApplied = false;
  std::vector<RestoreSummary> vSummaries;
  std::vector<std::string> vCredentialWarnings;
};

/// Exports and restores full system configuration as JSON.
/// Uses repository references for export and raw SQL via ConnectionPool for
/// transactional restore.
/// Class abbreviation: bs
class BackupService {
 public:
  BackupService(dns::dal::ConnectionPool& cpPool,
                dns::dal::SettingsRepository& stRepo,
                dns::dal::RoleRepository& rlRepo,
                dns::dal::GroupRepository& grRepo,
                dns::dal::UserRepository& urRepo,
                dns::dal::IdpRepository& irRepo,
                dns::dal::GitRepoRepository& grRepoGit,
                dns::dal::ProviderRepository& prRepo,
                dns::dal::ViewRepository& vrRepo,
                dns::dal::ZoneRepository& zrRepo,
                dns::dal::RecordRepository& rrRepo,
                dns::dal::VariableRepository& varRepo);
  ~BackupService();

  /// Export full system configuration as JSON.
  nlohmann::json exportSystem(const std::string& sExportedBy) const;

  /// Export a single zone with its records and variables.
  nlohmann::json exportZone(int64_t iZoneId) const;

  /// Preview what a restore would do (dry-run). Returns counts without applying.
  RestoreResult previewRestore(const nlohmann::json& jBackup) const;

  /// Apply a restore from backup JSON inside a single transaction.
  RestoreResult applyRestore(const nlohmann::json& jBackup);

 private:
  /// Validate backup JSON structure. Throws ValidationError on failure.
  void validateBackupFormat(const nlohmann::json& jBackup) const;

  dns::dal::ConnectionPool& _cpPool;
  dns::dal::SettingsRepository& _stRepo;
  dns::dal::RoleRepository& _rlRepo;
  dns::dal::GroupRepository& _grRepo;
  dns::dal::UserRepository& _urRepo;
  dns::dal::IdpRepository& _irRepo;
  dns::dal::GitRepoRepository& _grRepoGit;
  dns::dal::ProviderRepository& _prRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::VariableRepository& _varRepo;
};

}  // namespace dns::core
