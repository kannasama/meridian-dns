#pragma once

#include <crow.h>

namespace dns::dal {
class AuditRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/audit
/// Class abbreviation: au
class AuditRoutes {
 public:
  AuditRoutes(dns::dal::AuditRepository& arRepo,
              const dns::api::AuthMiddleware& amMiddleware,
              int iAuditRetentionDays);
  ~AuditRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::AuditRepository& _arRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  int _iAuditRetentionDays;
};

}  // namespace dns::api::routes
