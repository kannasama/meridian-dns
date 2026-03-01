#pragma once

#include <crow.h>

#include <memory>

namespace dns::security {
class AuthService;
class CryptoService;
}  // namespace dns::security

namespace dns::api {
class AuthMiddleware;
}

namespace dns::dal {
class ProviderRepository;
class ViewRepository;
class ZoneRepository;
class VariableRepository;
class RecordRepository;
class DeploymentRepository;
class AuditRepository;
}  // namespace dns::dal

namespace dns::api {

/// Owns the Crow application instance; registers all routes at startup.
/// Class abbreviation: api
class ApiServer {
 public:
  ApiServer(dns::security::AuthService& asService,
            const dns::api::AuthMiddleware& amMiddleware,
            dns::dal::ProviderRepository& prRepo,
            dns::dal::ViewRepository& vrRepo,
            dns::dal::ZoneRepository& zrRepo,
            dns::dal::VariableRepository& varRepo,
            dns::dal::RecordRepository& rrRepo,
            dns::dal::DeploymentRepository& drRepo,
            dns::dal::AuditRepository& arRepo,
            int iAuditRetentionDays);
  ~ApiServer();

  void registerRoutes();
  void start(int iPort, int iThreads);
  void stop();

 private:
  crow::SimpleApp _app;
  dns::security::AuthService& _asService;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::dal::ProviderRepository& _prRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::VariableRepository& _varRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::DeploymentRepository& _drRepo;
  dns::dal::AuditRepository& _arRepo;
  int _iAuditRetentionDays;
};

}  // namespace dns::api
