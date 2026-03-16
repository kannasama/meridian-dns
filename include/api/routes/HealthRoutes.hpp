#pragma once

#include <crow.h>
#include <string>

namespace dns::dal {
class ConnectionPool;
class GitRepoRepository;
class ProviderRepository;
}  // namespace dns::dal

namespace dns::api::routes {

/// Handlers for GET /api/v1/health, /health/live, /health/ready
class HealthRoutes {
 public:
  /// Liveness probe requires no dependencies.
  /// Readiness probe requires DB pool, repo access.
  HealthRoutes(dns::dal::ConnectionPool& cpPool,
               dns::dal::GitRepoRepository& grRepo,
               dns::dal::ProviderRepository& prRepo);
  ~HealthRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ConnectionPool& _cpPool;
  dns::dal::GitRepoRepository& _grRepo;
  dns::dal::ProviderRepository& _prRepo;
};

}  // namespace dns::api::routes
