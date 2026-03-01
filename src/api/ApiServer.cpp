#include "api/ApiServer.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/routes/AuthRoutes.hpp"
#include "api/routes/ProviderRoutes.hpp"
#include "api/routes/ViewRoutes.hpp"
#include "api/routes/ZoneRoutes.hpp"
#include "api/routes/VariableRoutes.hpp"
#include "api/routes/RecordRoutes.hpp"
#include "api/routes/AuditRoutes.hpp"
#include "common/Logger.hpp"
#include "security/AuthService.hpp"

namespace dns::api {

ApiServer::ApiServer(dns::security::AuthService& asService,
                     const dns::api::AuthMiddleware& amMiddleware,
                     dns::dal::ProviderRepository& prRepo,
                     dns::dal::ViewRepository& vrRepo,
                     dns::dal::ZoneRepository& zrRepo,
                     dns::dal::VariableRepository& varRepo,
                     dns::dal::RecordRepository& rrRepo,
                     dns::dal::DeploymentRepository& drRepo,
                     dns::dal::AuditRepository& arRepo,
                     int iAuditRetentionDays)
    : _asService(asService),
      _amMiddleware(amMiddleware),
      _prRepo(prRepo),
      _vrRepo(vrRepo),
      _zrRepo(zrRepo),
      _varRepo(varRepo),
      _rrRepo(rrRepo),
      _drRepo(drRepo),
      _arRepo(arRepo),
      _iAuditRetentionDays(iAuditRetentionDays) {}

ApiServer::~ApiServer() = default;

void ApiServer::registerRoutes() {
  // Auth routes (login, logout, me)
  auto arAuth = routes::AuthRoutes(_asService, _amMiddleware);
  arAuth.registerRoutes(_app);

  // Provider routes
  auto prRoutes = routes::ProviderRoutes(_prRepo, _amMiddleware);
  prRoutes.registerRoutes(_app);

  // View routes
  auto vrRoutes = routes::ViewRoutes(_vrRepo, _amMiddleware);
  vrRoutes.registerRoutes(_app);

  // Zone routes
  auto zrRoutes = routes::ZoneRoutes(_zrRepo, _amMiddleware);
  zrRoutes.registerRoutes(_app);

  // Variable routes
  auto varRoutes = routes::VariableRoutes(_varRepo, _amMiddleware);
  varRoutes.registerRoutes(_app);

  // Record routes (basic CRUD only; preview/push deferred to Phase 6-7)
  auto rrRoutes = routes::RecordRoutes(_rrRepo, _amMiddleware);
  rrRoutes.registerRoutes(_app);

  // Audit routes
  auto auRoutes = routes::AuditRoutes(_arRepo, _amMiddleware,
                                      _iAuditRetentionDays);
  auRoutes.registerRoutes(_app);

  auto spLog = common::Logger::get();
  spLog->info("All API routes registered");
}

void ApiServer::start(int iPort, int iThreads) {
  auto spLog = common::Logger::get();
  spLog->info("Starting HTTP server on port {} with {} threads",
              iPort, iThreads);
  _app.port(iPort).concurrency(iThreads).run();
}

void ApiServer::stop() {
  _app.stop();
}

}  // namespace dns::api
