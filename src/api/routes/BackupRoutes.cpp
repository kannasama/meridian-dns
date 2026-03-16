#include "api/routes/BackupRoutes.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "core/BackupService.hpp"
#include "dal/SettingsRepository.hpp"
#include "gitops/GitRepoManager.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

namespace {

std::string makeTimestamp() {
  auto tp = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H%M%SZ");
  return oss.str();
}

nlohmann::json restoreResultToJson(const dns::core::RestoreResult& result) {
  nlohmann::json jSummaries = nlohmann::json::array();
  for (const auto& s : result.vSummaries) {
    jSummaries.push_back({
        {"entity_type", s.sEntityType},
        {"created", s.iCreated},
        {"updated", s.iUpdated},
        {"skipped", s.iSkipped},
    });
  }

  return {
      {"applied", result.bApplied},
      {"summaries", jSummaries},
      {"credential_warnings", result.vCredentialWarnings},
  };
}

}  // namespace

BackupRoutes::BackupRoutes(dns::core::BackupService& bsService,
                           dns::dal::SettingsRepository& stRepo,
                           const dns::api::AuthMiddleware& amMiddleware,
                           dns::gitops::GitRepoManager* pGitRepoManager)
    : _bsService(bsService),
      _stRepo(stRepo),
      _amMiddleware(amMiddleware),
      _pGitRepoManager(pGitRepoManager) {}

BackupRoutes::~BackupRoutes() = default;

void BackupRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/backup/export — backup.create permission
  CROW_ROUTE(app, "/api/v1/backup/export").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupCreate);

          auto jExport = _bsService.exportSystem(rcCtx.sUsername);

          auto sTimestamp = makeTimestamp();
          auto sFilename = "meridian-backup-" + sTimestamp + ".json";

          crow::response resp(200, jExport.dump(2));
          resp.set_header("Content-Type", "application/json");
          resp.set_header("Content-Disposition",
                          "attachment; filename=\"" + sFilename + "\"");
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/backup/push-to-repo — backup.create permission
  // Generates a system export and commits it to the configured backup git repo.
  CROW_ROUTE(app, "/api/v1/backup/push-to-repo").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupCreate);

          if (!_pGitRepoManager) {
            throw ValidationError("GIT_NOT_CONFIGURED",
                                  "GitRepoManager is not available");
          }

          auto sRepoId = _stRepo.getValue("backup.git_repo_id", "");
          if (sRepoId.empty() || sRepoId == "0") {
            throw ValidationError("NO_BACKUP_REPO",
                                  "No backup git repository configured "
                                  "(set backup.git_repo_id in settings)");
          }

          auto sPath = _stRepo.getValue("backup.git_path",
                                        "_system/config-backup.json");
          auto jExport = _bsService.exportSystem(rcCtx.sUsername);

          _pGitRepoManager->writeAndCommit(
              std::stoll(sRepoId), sPath, jExport.dump(2),
              "Config backup by " + rcCtx.sUsername);

          return jsonResponse(200, {{"message", "Backup pushed to repository"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/backup/restore — backup.restore permission
  CROW_ROUTE(app, "/api/v1/backup/restore").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupRestore);

          auto jBackup = nlohmann::json::parse(req.body, nullptr, false);
          if (jBackup.is_discarded()) {
            return invalidJsonResponse();
          }

          auto pApply = req.url_params.get("apply");
          bool bApply = pApply && std::string(pApply) == "true";

          dns::core::RestoreResult result;
          if (bApply) {
            result = _bsService.applyRestore(jBackup);
          } else {
            result = _bsService.previewRestore(jBackup);
          }

          return jsonResponse(200, restoreResultToJson(result));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/backup/restore-from-repo — backup.restore permission
  CROW_ROUTE(app, "/api/v1/backup/restore-from-repo").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupRestore);

          if (!_pGitRepoManager) {
            throw ValidationError("GIT_NOT_CONFIGURED",
                                  "GitRepoManager is not available");
          }

          auto sRepoId = _stRepo.getValue("backup.git_repo_id", "");
          if (sRepoId.empty() || sRepoId == "0") {
            throw ValidationError("NO_BACKUP_REPO",
                                  "No backup git repository configured "
                                  "(set backup.git_repo_id in settings)");
          }

          auto sPath = _stRepo.getValue("backup.git_path",
                                        "_system/config-backup.json");

          // Pull latest and read backup file
          auto iRepoId = std::stoll(sRepoId);
          _pGitRepoManager->pullRepo(iRepoId);
          auto sContent = _pGitRepoManager->readFile(iRepoId, sPath);

          auto jBackup = nlohmann::json::parse(sContent, nullptr, false);
          if (jBackup.is_discarded()) {
            throw ValidationError("INVALID_BACKUP_FILE",
                                  "Backup file in git repo is not valid JSON");
          }

          auto pApply = req.url_params.get("apply");
          bool bApply = pApply && std::string(pApply) == "true";

          dns::core::RestoreResult result;
          if (bApply) {
            result = _bsService.applyRestore(jBackup);
          } else {
            result = _bsService.previewRestore(jBackup);
          }

          return jsonResponse(200, restoreResultToJson(result));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/zones/<int>/export — backup.create permission
  CROW_ROUTE(app, "/api/v1/zones/<int>/export").methods("GET"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupCreate);

          auto jExport = _bsService.exportZone(static_cast<int64_t>(iZoneId));

          auto sZoneName = jExport.value("zone", nlohmann::json::object())
                               .value("name", "zone");
          auto sFilename = sZoneName + "-export.json";

          crow::response resp(200, jExport.dump(2));
          resp.set_header("Content-Type", "application/json");
          resp.set_header("Content-Disposition",
                          "attachment; filename=\"" + sFilename + "\"");
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/import — backup.restore permission
  CROW_ROUTE(app, "/api/v1/zones/<int>/import").methods("POST"_method)(
      [this](const crow::request& req, int /*iZoneId*/) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kBackupRestore);

          auto jImport = nlohmann::json::parse(req.body, nullptr, false);
          if (jImport.is_discarded()) {
            return invalidJsonResponse();
          }

          // Validate it's a zone export
          if (!jImport.contains("type") || jImport["type"] != "zone") {
            throw ValidationError("INVALID_ZONE_EXPORT",
                                  "Expected a zone export (type: zone)");
          }

          // Build a full backup JSON from the zone export for restore
          // We reuse the restore pipeline by wrapping zone data into a
          // system-level backup with only zones, records, and variables
          nlohmann::json jBackup;
          jBackup["version"] = 1;
          jBackup["exported_at"] = jImport.value("exported_at", "");
          jBackup["exported_by"] = rcCtx.sUsername;
          jBackup["meridian_version"] = "0.1.0";
          jBackup["settings"] = nlohmann::json::array();
          jBackup["roles"] = nlohmann::json::array();
          jBackup["groups"] = nlohmann::json::array();
          jBackup["users"] = nlohmann::json::array();
          jBackup["identity_providers"] = nlohmann::json::array();
          jBackup["git_repos"] = nlohmann::json::array();
          jBackup["providers"] = nlohmann::json::array();
          jBackup["views"] = nlohmann::json::array();
          jBackup["zones"] = nlohmann::json::array();

          // Add the zone's records with the zone_name field
          auto sZoneName = jImport["zone"]["name"].get<std::string>();
          nlohmann::json jRecords = nlohmann::json::array();
          for (auto jRec : jImport["records"]) {
            jRec["zone_name"] = sZoneName;
            jRecords.push_back(jRec);
          }
          jBackup["records"] = jRecords;
          jBackup["variables"] = jImport.value("variables", nlohmann::json::array());

          auto pApply = req.url_params.get("apply");
          bool bApply = pApply && std::string(pApply) == "true";

          dns::core::RestoreResult result;
          if (bApply) {
            result = _bsService.applyRestore(jBackup);
          } else {
            result = _bsService.previewRestore(jBackup);
          }

          return jsonResponse(200, restoreResultToJson(result));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
