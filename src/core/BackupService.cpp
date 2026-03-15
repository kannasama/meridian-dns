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

RestoreResult BackupService::applyRestore(const nlohmann::json& jBackup) {
  validateBackupFormat(jBackup);

  RestoreResult result;
  result.bApplied = true;

  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // ── 1. Settings ────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "settings";

    for (const auto& jItem : jBackup["settings"]) {
      auto sKey = jItem["key"].get<std::string>();
      auto sVal = jItem["value"].get<std::string>();

      auto rExisting = txn.exec(
          "SELECT value FROM system_config WHERE key = $1", pqxx::params{sKey});

      if (rExisting.empty()) {
        txn.exec("INSERT INTO system_config (key, value) VALUES ($1, $2)",
                 pqxx::params{sKey, sVal});
        ++summary.iCreated;
      } else if (rExisting[0][0].as<std::string>() != sVal) {
        txn.exec("UPDATE system_config SET value = $1 WHERE key = $2",
                 pqxx::params{sVal, sKey});
        ++summary.iUpdated;
      } else {
        ++summary.iSkipped;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 2. Roles ───────────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mRoleIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "roles";

    for (const auto& jItem : jBackup["roles"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sDesc = jItem.value("description", "");
      bool bIsSystem = jItem.value("is_system", false);

      auto rExisting = txn.exec(
          "SELECT id FROM roles WHERE name = $1", pqxx::params{sName});

      int64_t iRoleId = 0;
      if (!rExisting.empty()) {
        iRoleId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE roles SET description = $1 WHERE id = $2",
                 pqxx::params{sDesc, iRoleId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO roles (name, description, is_system) "
            "VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{sName, sDesc, bIsSystem});
        iRoleId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
      }
      mRoleIds[sName] = iRoleId;

      // Set permissions: delete existing + insert from backup
      if (jItem.contains("permissions") && jItem["permissions"].is_array()) {
        txn.exec("DELETE FROM role_permissions WHERE role_id = $1",
                 pqxx::params{iRoleId});
        for (const auto& jPerm : jItem["permissions"]) {
          txn.exec(
              "INSERT INTO role_permissions (role_id, permission) VALUES ($1, $2)",
              pqxx::params{iRoleId, jPerm.get<std::string>()});
        }
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 3. Groups ──────────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mGroupIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "groups";

    for (const auto& jItem : jBackup["groups"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sDesc = jItem.value("description", "");
      auto sRoleName = jItem.value("role_name", "Viewer");

      int64_t iRoleId = 0;
      auto itRole = mRoleIds.find(sRoleName);
      if (itRole != mRoleIds.end()) {
        iRoleId = itRole->second;
      } else {
        auto rRole = txn.exec(
            "SELECT id FROM roles WHERE name = $1", pqxx::params{sRoleName});
        if (!rRole.empty()) iRoleId = rRole[0][0].as<int64_t>();
      }

      auto rExisting = txn.exec(
          "SELECT id FROM groups WHERE name = $1", pqxx::params{sName});

      int64_t iGroupId = 0;
      if (!rExisting.empty()) {
        iGroupId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE groups SET description = $1, role_id = $2 WHERE id = $3",
                 pqxx::params{sDesc, iRoleId, iGroupId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO groups (name, description, role_id) "
            "VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{sName, sDesc, iRoleId});
        iGroupId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
      }
      mGroupIds[sName] = iGroupId;
    }
    result.vSummaries.push_back(summary);
  }

  // ── 4. Users ───────────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mUserIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "users";

    for (const auto& jItem : jBackup["users"]) {
      auto sUsername = jItem["username"].get<std::string>();
      auto sEmail = jItem.value("email", "");
      auto sAuthMethod = jItem.value("auth_method", "local");
      bool bIsActive = jItem.value("is_active", true);

      auto rExisting = txn.exec(
          "SELECT id FROM users WHERE username = $1", pqxx::params{sUsername});

      int64_t iUserId = 0;
      if (!rExisting.empty()) {
        iUserId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE users SET email = $1, is_active = $2, "
                 "auth_method = $3::auth_method WHERE id = $4",
                 pqxx::params{sEmail, bIsActive, sAuthMethod, iUserId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO users (username, email, auth_method, is_active, "
            "force_password_change) "
            "VALUES ($1, $2, $3::auth_method, $4, true) RETURNING id",
            pqxx::params{sUsername, sEmail, sAuthMethod, bIsActive});
        iUserId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
      }
      mUserIds[sUsername] = iUserId;

      // Restore group memberships
      if (jItem.contains("groups") && jItem["groups"].is_array()) {
        txn.exec("DELETE FROM group_members WHERE user_id = $1",
                 pqxx::params{iUserId});
        for (const auto& jGroup : jItem["groups"]) {
          auto sGroupName = jGroup.get<std::string>();
          auto itGr = mGroupIds.find(sGroupName);
          if (itGr != mGroupIds.end()) {
            txn.exec(
                "INSERT INTO group_members (user_id, group_id) "
                "VALUES ($1, $2) ON CONFLICT DO NOTHING",
                pqxx::params{iUserId, itGr->second});
          }
        }
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 5. Providers ───────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mProviderIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "providers";

    for (const auto& jItem : jBackup["providers"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sType = jItem.value("type", "powerdns");
      auto sApiEndpoint = jItem.value("api_endpoint", "");

      auto rExisting = txn.exec(
          "SELECT id FROM providers WHERE name = $1", pqxx::params{sName});

      int64_t iProviderId = 0;
      if (!rExisting.empty()) {
        iProviderId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE providers SET api_endpoint = $1 WHERE id = $2",
                 pqxx::params{sApiEndpoint, iProviderId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO providers (name, type, api_endpoint, encrypted_token) "
            "VALUES ($1, $2::provider_type, $3, '') RETURNING id",
            pqxx::params{sName, sType, sApiEndpoint});
        iProviderId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("provider:" + sName);
      }
      mProviderIds[sName] = iProviderId;
    }
    result.vSummaries.push_back(summary);
  }

  // ── 6. Git Repos ───────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mGitRepoIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "git_repos";

    for (const auto& jItem : jBackup["git_repos"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sRemoteUrl = jItem.value("remote_url", "");
      auto sAuthType = jItem.value("auth_type", "none");
      auto sDefaultBranch = jItem.value("default_branch", "main");

      auto rExisting = txn.exec(
          "SELECT id FROM git_repos WHERE name = $1", pqxx::params{sName});

      int64_t iRepoId = 0;
      if (!rExisting.empty()) {
        iRepoId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE git_repos SET remote_url = $1, auth_type = $2, "
                 "default_branch = $3 WHERE id = $4",
                 pqxx::params{sRemoteUrl, sAuthType, sDefaultBranch, iRepoId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO git_repos (name, remote_url, auth_type, default_branch) "
            "VALUES ($1, $2, $3, $4) RETURNING id",
            pqxx::params{sName, sRemoteUrl, sAuthType, sDefaultBranch});
        iRepoId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("git_repo:" + sName);
      }
      mGitRepoIds[sName] = iRepoId;
    }
    result.vSummaries.push_back(summary);
  }

  // ── 7. Identity Providers ──────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "identity_providers";

    for (const auto& jItem : jBackup["identity_providers"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sType = jItem.value("type", "oidc");
      bool bIsEnabled = jItem.value("is_enabled", true);
      auto jConfig = jItem.value("config", nlohmann::json::object());
      auto jGroupMappings = jItem.value("group_mappings", nlohmann::json(nullptr));
      auto sDefaultGroupName = jItem.value("default_group_name", "");

      // Resolve default group
      std::optional<int64_t> oDefaultGroupId;
      if (!sDefaultGroupName.empty()) {
        auto itGr = mGroupIds.find(sDefaultGroupName);
        if (itGr != mGroupIds.end()) {
          oDefaultGroupId = itGr->second;
        } else {
          auto rGr = txn.exec(
              "SELECT id FROM groups WHERE name = $1",
              pqxx::params{sDefaultGroupName});
          if (!rGr.empty()) oDefaultGroupId = rGr[0][0].as<int64_t>();
        }
      }

      auto rExisting = txn.exec(
          "SELECT id FROM identity_providers WHERE name = $1",
          pqxx::params{sName});

      if (!rExisting.empty()) {
        auto iIdpId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE identity_providers SET type = $1, is_enabled = $2, "
                 "config = $3::jsonb, group_mappings = $4::jsonb, "
                 "default_group_id = $5 WHERE id = $6",
                 pqxx::params{sType, bIsEnabled, jConfig.dump(),
                              jGroupMappings.dump(), oDefaultGroupId, iIdpId});
        ++summary.iUpdated;
      } else {
        txn.exec("INSERT INTO identity_providers "
                 "(name, type, is_enabled, config, group_mappings, default_group_id) "
                 "VALUES ($1, $2, $3, $4::jsonb, $5::jsonb, $6)",
                 pqxx::params{sName, sType, bIsEnabled, jConfig.dump(),
                              jGroupMappings.dump(), oDefaultGroupId});
        ++summary.iCreated;
        result.vCredentialWarnings.push_back("identity_provider:" + sName);
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 8. Views ───────────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mViewIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "views";

    for (const auto& jItem : jBackup["views"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sDesc = jItem.value("description", "");

      auto rExisting = txn.exec(
          "SELECT id FROM views WHERE name = $1", pqxx::params{sName});

      int64_t iViewId = 0;
      if (!rExisting.empty()) {
        iViewId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE views SET description = $1 WHERE id = $2",
                 pqxx::params{sDesc, iViewId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO views (name, description) VALUES ($1, $2) RETURNING id",
            pqxx::params{sName, sDesc});
        iViewId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
      }
      mViewIds[sName] = iViewId;

      // Restore provider attachments
      if (jItem.contains("provider_names") && jItem["provider_names"].is_array()) {
        txn.exec("DELETE FROM view_providers WHERE view_id = $1",
                 pqxx::params{iViewId});
        for (const auto& jPn : jItem["provider_names"]) {
          auto sPn = jPn.get<std::string>();
          auto itPr = mProviderIds.find(sPn);
          if (itPr != mProviderIds.end()) {
            txn.exec(
                "INSERT INTO view_providers (view_id, provider_id) "
                "VALUES ($1, $2) ON CONFLICT DO NOTHING",
                pqxx::params{iViewId, itPr->second});
          }
        }
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 9. Zones ───────────────────────────────────────────────────────────
  std::unordered_map<std::string, int64_t> mRestoreZoneIds;
  {
    RestoreSummary summary;
    summary.sEntityType = "zones";

    for (const auto& jItem : jBackup["zones"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sViewName = jItem.value("view_name", "");
      bool bManageSoa = jItem.value("manage_soa", false);
      bool bManageNs = jItem.value("manage_ns", false);

      int64_t iViewId = 0;
      auto itView = mViewIds.find(sViewName);
      if (itView != mViewIds.end()) {
        iViewId = itView->second;
      } else {
        auto rView = txn.exec(
            "SELECT id FROM views WHERE name = $1", pqxx::params{sViewName});
        if (!rView.empty()) iViewId = rView[0][0].as<int64_t>();
      }

      // Resolve optional git_repo_name → ID
      std::optional<int64_t> oGitRepoId;
      if (jItem.contains("git_repo_name") && jItem["git_repo_name"].is_string()) {
        auto sGrName = jItem["git_repo_name"].get<std::string>();
        auto itGr = mGitRepoIds.find(sGrName);
        if (itGr != mGitRepoIds.end()) oGitRepoId = itGr->second;
      }

      std::optional<std::string> oGitBranch;
      if (jItem.contains("git_branch") && jItem["git_branch"].is_string()) {
        oGitBranch = jItem["git_branch"].get<std::string>();
      }

      std::optional<int> oRetention;
      if (jItem.contains("deployment_retention") &&
          jItem["deployment_retention"].is_number_integer()) {
        oRetention = jItem["deployment_retention"].get<int>();
      }

      auto rExisting = txn.exec(
          "SELECT id FROM zones WHERE name = $1 AND view_id = $2",
          pqxx::params{sName, iViewId});

      int64_t iZoneId = 0;
      if (!rExisting.empty()) {
        iZoneId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE zones SET manage_soa = $1, manage_ns = $2, "
                 "deployment_retention = $3, git_repo_id = $4, git_branch = $5 "
                 "WHERE id = $6",
                 pqxx::params{bManageSoa, bManageNs, oRetention,
                              oGitRepoId, oGitBranch, iZoneId});
        ++summary.iUpdated;
      } else {
        auto rNew = txn.exec(
            "INSERT INTO zones (name, view_id, manage_soa, manage_ns, "
            "deployment_retention, git_repo_id, git_branch) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
            pqxx::params{sName, iViewId, bManageSoa, bManageNs,
                         oRetention, oGitRepoId, oGitBranch});
        iZoneId = rNew[0][0].as<int64_t>();
        ++summary.iCreated;
      }
      mRestoreZoneIds[sName] = iZoneId;
    }
    result.vSummaries.push_back(summary);
  }

  // ── 10. Records ────────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "records";

    for (const auto& jItem : jBackup["records"]) {
      auto sZoneName = jItem["zone_name"].get<std::string>();
      auto sName = jItem["name"].get<std::string>();
      auto sType = jItem["type"].get<std::string>();
      int iTtl = jItem.value("ttl", 300);
      auto sValueTemplate = jItem.value("value_template", "");
      int iPriority = jItem.value("priority", 0);

      auto itZone = mRestoreZoneIds.find(sZoneName);
      if (itZone == mRestoreZoneIds.end()) continue;
      int64_t iZoneId = itZone->second;

      auto rExisting = txn.exec(
          "SELECT id FROM records WHERE zone_id = $1 AND name = $2 "
          "AND type = $3 AND pending_delete = false",
          pqxx::params{iZoneId, sName, sType});

      if (!rExisting.empty()) {
        auto iRecordId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE records SET ttl = $1, value_template = $2, "
                 "priority = $3 WHERE id = $4",
                 pqxx::params{iTtl, sValueTemplate, iPriority, iRecordId});
        ++summary.iUpdated;
      } else {
        txn.exec("INSERT INTO records (zone_id, name, type, ttl, "
                 "value_template, priority) "
                 "VALUES ($1, $2, $3, $4, $5, $6)",
                 pqxx::params{iZoneId, sName, sType, iTtl,
                              sValueTemplate, iPriority});
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  // ── 11. Variables ──────────────────────────────────────────────────────
  {
    RestoreSummary summary;
    summary.sEntityType = "variables";

    for (const auto& jItem : jBackup["variables"]) {
      auto sName = jItem["name"].get<std::string>();
      auto sValue = jItem["value"].get<std::string>();
      auto sType = jItem.value("type", "string");
      auto sScope = jItem.value("scope", "global");

      std::optional<int64_t> oZoneId;
      if (jItem.contains("zone_name") && jItem["zone_name"].is_string()) {
        auto sZoneName = jItem["zone_name"].get<std::string>();
        auto itZone = mRestoreZoneIds.find(sZoneName);
        if (itZone != mRestoreZoneIds.end()) oZoneId = itZone->second;
      }

      pqxx::result rExisting;
      if (oZoneId) {
        rExisting = txn.exec(
            "SELECT id FROM variables WHERE name = $1 AND zone_id = $2",
            pqxx::params{sName, *oZoneId});
      } else {
        rExisting = txn.exec(
            "SELECT id FROM variables WHERE name = $1 AND zone_id IS NULL",
            pqxx::params{sName});
      }

      if (!rExisting.empty()) {
        auto iVarId = rExisting[0][0].as<int64_t>();
        txn.exec("UPDATE variables SET value = $1 WHERE id = $2",
                 pqxx::params{sValue, iVarId});
        ++summary.iUpdated;
      } else {
        if (oZoneId) {
          txn.exec("INSERT INTO variables (name, value, type, scope, zone_id) "
                   "VALUES ($1, $2, $3::variable_type, $4::variable_scope, $5)",
                   pqxx::params{sName, sValue, sType, sScope, *oZoneId});
        } else {
          txn.exec("INSERT INTO variables (name, value, type, scope) "
                   "VALUES ($1, $2, $3::variable_type, $4::variable_scope)",
                   pqxx::params{sName, sValue, sType, sScope});
        }
        ++summary.iCreated;
      }
    }
    result.vSummaries.push_back(summary);
  }

  txn.commit();
  return result;
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
