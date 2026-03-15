#include "core/BackupService.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/GroupRepository.hpp"
#include "dal/IdpRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/RoleRepository.hpp"
#include "dal/SettingsRepository.hpp"
#include "dal/UserRepository.hpp"
#include "dal/VariableRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace dns::core {

namespace {

constexpr std::string_view kBackupVersion = "0.1.0";

std::string nowIso8601() {
  auto tp = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&tt), "%FT%TZ");
  return oss.str();
}

}  // namespace

BackupService::BackupService(dns::dal::ConnectionPool& cpPool,
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
                             dns::dal::VariableRepository& varRepo)
    : _cpPool(cpPool),
      _stRepo(stRepo),
      _rlRepo(rlRepo),
      _grRepo(grRepo),
      _urRepo(urRepo),
      _irRepo(irRepo),
      _grRepoGit(grRepoGit),
      _prRepo(prRepo),
      _vrRepo(vrRepo),
      _zrRepo(zrRepo),
      _rrRepo(rrRepo),
      _varRepo(varRepo) {}

BackupService::~BackupService() = default;

nlohmann::json BackupService::exportSystem(const std::string& sExportedBy) const {
  // ── Build ID→name lookup maps ──────────────────────────────────────────

  // Roles: ID → name
  auto vRoles = _rlRepo.listAll();
  std::unordered_map<int64_t, std::string> mRoleNames;
  for (const auto& r : vRoles) mRoleNames[r.iId] = r.sName;

  // Groups: ID → name
  auto vGroups = _grRepo.listAll();
  std::unordered_map<int64_t, std::string> mGroupNames;
  for (const auto& g : vGroups) mGroupNames[g.iId] = g.sName;

  // Providers: ID → name
  auto vProviders = _prRepo.listAll();
  std::unordered_map<int64_t, std::string> mProviderNames;
  for (const auto& p : vProviders) mProviderNames[p.iId] = p.sName;

  // Views: ID → name
  auto vViews = _vrRepo.listAll();
  std::unordered_map<int64_t, std::string> mViewNames;
  for (const auto& v : vViews) mViewNames[v.iId] = v.sName;

  // Zones: ID → name
  auto vZones = _zrRepo.listAll();
  std::unordered_map<int64_t, std::string> mZoneNames;
  for (const auto& z : vZones) mZoneNames[z.iId] = z.sName;

  // Git repos: ID → name
  auto vGitRepos = _grRepoGit.listAll();
  std::unordered_map<int64_t, std::string> mGitRepoNames;
  for (const auto& gr : vGitRepos) mGitRepoNames[gr.iId] = gr.sName;

  // ── Export settings ────────────────────────────────────────────────────
  auto vSettings = _stRepo.listAll();
  nlohmann::json jSettings = nlohmann::json::array();
  for (const auto& s : vSettings) {
    jSettings.push_back({{"key", s.sKey}, {"value", s.sValue}});
  }

  // ── Export roles ───────────────────────────────────────────────────────
  nlohmann::json jRoles = nlohmann::json::array();
  for (const auto& r : vRoles) {
    auto vPerms = _rlRepo.getPermissions(r.iId);
    nlohmann::json jPerms = nlohmann::json::array();
    for (const auto& p : vPerms) jPerms.push_back(p);

    jRoles.push_back({
        {"name", r.sName},
        {"description", r.sDescription},
        {"is_system", r.bIsSystem},
        {"permissions", jPerms},
    });
  }

  // ── Export groups ──────────────────────────────────────────────────────
  nlohmann::json jGroups = nlohmann::json::array();
  for (const auto& g : vGroups) {
    std::string sRoleName;
    auto it = mRoleNames.find(g.iRoleId);
    if (it != mRoleNames.end()) sRoleName = it->second;

    jGroups.push_back({
        {"name", g.sName},
        {"description", g.sDescription},
        {"role_name", sRoleName},
    });
  }

  // ── Export users (exclude password_hash, oidc_sub, saml_name_id) ──────
  auto vUsers = _urRepo.listAll();
  nlohmann::json jUsers = nlohmann::json::array();
  for (const auto& u : vUsers) {
    // Get group memberships as group names
    auto vUserGroups = _urRepo.listGroupsForUser(u.iId);
    nlohmann::json jGroupNames = nlohmann::json::array();
    for (const auto& [iGroupId, sGroupName] : vUserGroups) {
      jGroupNames.push_back(sGroupName);
    }

    jUsers.push_back({
        {"username", u.sUsername},
        {"email", u.sEmail},
        {"auth_method", u.sAuthMethod},
        {"is_active", u.bIsActive},
        {"groups", jGroupNames},
    });
  }

  // ── Export identity providers (exclude encrypted_secret) ───────────────
  auto vIdps = _irRepo.listAll();
  nlohmann::json jIdps = nlohmann::json::array();
  for (const auto& idp : vIdps) {
    std::string sDefaultGroupName;
    if (idp.iDefaultGroupId > 0) {
      auto it = mGroupNames.find(idp.iDefaultGroupId);
      if (it != mGroupNames.end()) sDefaultGroupName = it->second;
    }

    jIdps.push_back({
        {"name", idp.sName},
        {"type", idp.sType},
        {"is_enabled", idp.bIsEnabled},
        {"config", idp.jConfig},
        {"group_mappings", idp.jGroupMappings},
        {"default_group_name", sDefaultGroupName},
    });
  }

  // ── Export git repos (exclude encrypted_credentials, known_hosts) ──────
  nlohmann::json jGitRepos = nlohmann::json::array();
  for (const auto& gr : vGitRepos) {
    jGitRepos.push_back({
        {"name", gr.sName},
        {"remote_url", gr.sRemoteUrl},
        {"auth_type", gr.sAuthType},
        {"default_branch", gr.sDefaultBranch},
    });
  }

  // ── Export providers (exclude encrypted token) ─────────────────────────
  nlohmann::json jProviders = nlohmann::json::array();
  for (const auto& p : vProviders) {
    jProviders.push_back({
        {"name", p.sName},
        {"type", p.sType},
        {"api_endpoint", p.sApiEndpoint},
    });
  }

  // ── Export views (with provider names via findWithProviders) ────────────
  nlohmann::json jViews = nlohmann::json::array();
  for (const auto& v : vViews) {
    // N+1 but views count is typically small
    auto oViewFull = _vrRepo.findWithProviders(v.iId);
    nlohmann::json jProviderNameList = nlohmann::json::array();
    if (oViewFull) {
      for (auto iPid : oViewFull->vProviderIds) {
        auto it = mProviderNames.find(iPid);
        if (it != mProviderNames.end()) {
          jProviderNameList.push_back(it->second);
        }
      }
    }

    jViews.push_back({
        {"name", v.sName},
        {"description", v.sDescription},
        {"provider_names", jProviderNameList},
    });
  }

  // ── Export zones ───────────────────────────────────────────────────────
  nlohmann::json jZones = nlohmann::json::array();
  for (const auto& z : vZones) {
    std::string sViewName;
    auto itV = mViewNames.find(z.iViewId);
    if (itV != mViewNames.end()) sViewName = itV->second;

    nlohmann::json jZone = {
        {"name", z.sName},
        {"view_name", sViewName},
        {"manage_soa", z.bManageSoa},
        {"manage_ns", z.bManageNs},
    };

    if (z.oDeploymentRetention) {
      jZone["deployment_retention"] = *z.oDeploymentRetention;
    }

    if (z.oGitRepoId) {
      auto itGr = mGitRepoNames.find(*z.oGitRepoId);
      if (itGr != mGitRepoNames.end()) {
        jZone["git_repo_name"] = itGr->second;
      }
    }

    if (z.oGitBranch) {
      jZone["git_branch"] = *z.oGitBranch;
    }

    jZones.push_back(jZone);
  }

  // ── Export records (exclude provider_meta) ──────────────────────────────
  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& z : vZones) {
    auto vRecords = _rrRepo.listByZoneId(z.iId);
    for (const auto& r : vRecords) {
      if (r.bPendingDelete) continue;  // Skip soft-deleted records

      jRecords.push_back({
          {"zone_name", z.sName},
          {"name", r.sName},
          {"type", r.sType},
          {"ttl", r.iTtl},
          {"value_template", r.sValueTemplate},
          {"priority", r.iPriority},
      });
    }
  }

  // ── Export variables ───────────────────────────────────────────────────
  auto vVariables = _varRepo.listAll();
  nlohmann::json jVariables = nlohmann::json::array();
  for (const auto& v : vVariables) {
    nlohmann::json jVar = {
        {"name", v.sName},
        {"value", v.sValue},
        {"type", v.sType},
        {"scope", v.sScope},
    };

    if (v.oZoneId) {
      auto itZ = mZoneNames.find(*v.oZoneId);
      if (itZ != mZoneNames.end()) {
        jVar["zone_name"] = itZ->second;
      }
    }

    jVariables.push_back(jVar);
  }

  // ── Assemble the complete backup ───────────────────────────────────────
  nlohmann::json jBackup = {
      {"version", 1},
      {"exported_at", nowIso8601()},
      {"exported_by", sExportedBy},
      {"meridian_version", std::string(kBackupVersion)},
      {"settings", jSettings},
      {"roles", jRoles},
      {"groups", jGroups},
      {"users", jUsers},
      {"identity_providers", jIdps},
      {"git_repos", jGitRepos},
      {"providers", jProviders},
      {"views", jViews},
      {"zones", jZones},
      {"records", jRecords},
      {"variables", jVariables},
  };

  return jBackup;
}

nlohmann::json BackupService::exportZone(int64_t iZoneId) const {
  // Find the zone
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  // Look up view name
  std::string sViewName;
  auto oView = _vrRepo.findById(oZone->iViewId);
  if (oView) sViewName = oView->sName;

  // Build zone JSON
  nlohmann::json jZone = {
      {"name", oZone->sName},
      {"view_name", sViewName},
      {"manage_soa", oZone->bManageSoa},
      {"manage_ns", oZone->bManageNs},
  };

  if (oZone->oDeploymentRetention) {
    jZone["deployment_retention"] = *oZone->oDeploymentRetention;
  }

  if (oZone->oGitRepoId) {
    auto oGitRepo = _grRepoGit.findById(*oZone->oGitRepoId);
    if (oGitRepo) {
      jZone["git_repo_name"] = oGitRepo->sName;
    }
  }

  if (oZone->oGitBranch) {
    jZone["git_branch"] = *oZone->oGitBranch;
  }

  // Export records (exclude provider_meta)
  auto vRecords = _rrRepo.listByZoneId(iZoneId);
  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& r : vRecords) {
    if (r.bPendingDelete) continue;

    jRecords.push_back({
        {"name", r.sName},
        {"type", r.sType},
        {"ttl", r.iTtl},
        {"value_template", r.sValueTemplate},
        {"priority", r.iPriority},
    });
  }

  // Export zone-scoped and global variables
  auto vVariables = _varRepo.listByZoneId(iZoneId);
  nlohmann::json jVariables = nlohmann::json::array();
  for (const auto& v : vVariables) {
    nlohmann::json jVar = {
        {"name", v.sName},
        {"value", v.sValue},
        {"type", v.sType},
        {"scope", v.sScope},
    };
    if (v.oZoneId) {
      jVar["zone_name"] = oZone->sName;
    }
    jVariables.push_back(jVar);
  }

  return {
      {"version", 1},
      {"type", "zone"},
      {"exported_at", nowIso8601()},
      {"zone", jZone},
      {"records", jRecords},
      {"variables", jVariables},
  };
}

RestoreResult BackupService::previewRestore(const nlohmann::json& jBackup) const {
  validateBackupFormat(jBackup);

  RestoreResult result;
  result.bApplied = false;

  // ── Settings ───────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "settings";
    auto vExisting = _stRepo.listAll();
    std::unordered_map<std::string, std::string> mExisting;
    for (const auto& s : vExisting) mExisting[s.sKey] = s.sValue;

    for (const auto& jItem : jBackup["settings"]) {
      auto sKey = jItem["key"].get<std::string>();
      auto sVal = jItem["value"].get<std::string>();
      auto it = mExisting.find(sKey);
      if (it == mExisting.end()) {
        ++summary.iCreated;
      } else if (it->second != sVal) {
        ++summary.iUpdated;
      } else {
        ++summary.iSkipped;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Roles ──────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "roles";
    auto vExisting = _rlRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& r : vExisting) mExisting[r.sName] = r.iId;

    for (const auto& jItem : jBackup["roles"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Groups ─────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "groups";
    auto vExisting = _grRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& g : vExisting) mExisting[g.sName] = g.iId;

    for (const auto& jItem : jBackup["groups"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Users ──────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "users";
    auto vExisting = _urRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& u : vExisting) mExisting[u.sUsername] = u.iId;

    for (const auto& jItem : jBackup["users"]) {
      auto sName = jItem["username"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Identity Providers ─────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "identity_providers";
    auto vExisting = _irRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& idp : vExisting) mExisting[idp.sName] = idp.iId;

    for (const auto& jItem : jBackup["identity_providers"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("identity_provider:" + sName);
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Git Repos ──────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "git_repos";
    auto vExisting = _grRepoGit.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& gr : vExisting) mExisting[gr.sName] = gr.iId;

    for (const auto& jItem : jBackup["git_repos"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("git_repo:" + sName);
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Providers ──────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "providers";
    auto vExisting = _prRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& p : vExisting) mExisting[p.sName] = p.iId;

    for (const auto& jItem : jBackup["providers"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("provider:" + sName);
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Views ──────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "views";
    auto vExisting = _vrRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& v : vExisting) mExisting[v.sName] = v.iId;

    for (const auto& jItem : jBackup["views"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Zones ──────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "zones";
    auto vExisting = _zrRepo.listAll();
    std::unordered_map<std::string, int64_t> mExisting;
    for (const auto& z : vExisting) mExisting[z.sName] = z.iId;

    for (const auto& jItem : jBackup["zones"]) {
      auto sName = jItem["name"].get<std::string>();
      if (mExisting.count(sName)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Records ────────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "records";

    // Build zone name→ID map and existing records keyed by (zone_name, name, type)
    auto vZones = _zrRepo.listAll();
    std::unordered_map<std::string, int64_t> mZoneIds;
    for (const auto& z : vZones) mZoneIds[z.sName] = z.iId;

    // Key: "zone_name|name|type"
    std::unordered_set<std::string> sExistingRecords;
    for (const auto& z : vZones) {
      auto vRecords = _rrRepo.listByZoneId(z.iId);
      for (const auto& r : vRecords) {
        if (!r.bPendingDelete) {
          sExistingRecords.insert(z.sName + "|" + r.sName + "|" + r.sType);
        }
      }
    }

    for (const auto& jItem : jBackup["records"]) {
      auto sZN = jItem["zone_name"].get<std::string>();
      auto sN = jItem["name"].get<std::string>();
      auto sT = jItem["type"].get<std::string>();
      auto sKey = sZN + "|" + sN + "|" + sT;
      if (sExistingRecords.count(sKey)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── Variables ──────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "variables";

    auto vExisting = _varRepo.listAll();
    // Build zone ID→name map
    auto vZones = _zrRepo.listAll();
    std::unordered_map<int64_t, std::string> mZoneNames;
    for (const auto& z : vZones) mZoneNames[z.iId] = z.sName;

    // Key: "name|scope|zone_name"
    std::unordered_set<std::string> sExistingVars;
    for (const auto& v : vExisting) {
      std::string sZName;
      if (v.oZoneId) {
        auto it = mZoneNames.find(*v.oZoneId);
        if (it != mZoneNames.end()) sZName = it->second;
      }
      sExistingVars.insert(v.sName + "|" + v.sScope + "|" + sZName);
    }

    for (const auto& jItem : jBackup["variables"]) {
      auto sN = jItem["name"].get<std::string>();
      auto sS = jItem["scope"].get<std::string>();
      std::string sZN;
      if (jItem.contains("zone_name") && jItem["zone_name"].is_string()) {
        sZN = jItem["zone_name"].get<std::string>();
      }
      auto sKey = sN + "|" + sS + "|" + sZN;
      if (sExistingVars.count(sKey)) {
        ++summary.iUpdated;
      } else {
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  return result;
}

RestoreResult BackupService::applyRestore(const nlohmann::json& /*jBackup*/) {
  // TODO: Implement in Task 7
  return {};
}

void BackupService::validateBackupFormat(const nlohmann::json& jBackup) const {
  if (!jBackup.is_object()) {
    throw common::ValidationError("INVALID_BACKUP", "Backup must be a JSON object");
  }

  // Version check
  if (!jBackup.contains("version") || !jBackup["version"].is_number_integer()) {
    throw common::ValidationError("INVALID_BACKUP", "Missing or invalid version field");
  }
  if (jBackup["version"] != 1) {
    throw common::ValidationError("UNSUPPORTED_VERSION",
                                  "Unsupported backup version: " +
                                      std::to_string(jBackup["version"].get<int>()));
  }

  // Metadata
  for (const auto& sField : {"exported_at", "exported_by", "meridian_version"}) {
    if (!jBackup.contains(sField) || !jBackup[sField].is_string()) {
      throw common::ValidationError("INVALID_BACKUP",
                                    "Missing or invalid metadata field: " +
                                        std::string(sField));
    }
  }

  // Settings must be an array
  if (!jBackup.contains("settings") || !jBackup["settings"].is_array()) {
    throw common::ValidationError("INVALID_BACKUP",
                                  "Missing or invalid section: settings");
  }

  // All array sections
  const std::vector<std::string> vArraySections = {
      "roles",     "groups",    "users",     "identity_providers", "git_repos",
      "providers", "views",     "zones",     "records",            "variables",
  };
  for (const auto& sKey : vArraySections) {
    if (!jBackup.contains(sKey) || !jBackup[sKey].is_array()) {
      throw common::ValidationError("INVALID_BACKUP",
                                    "Missing or invalid section: " + sKey);
    }
  }
}

}  // namespace dns::core
