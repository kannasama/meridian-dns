#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::api {
class AuthMiddleware;
}
namespace dns::dal {
class GitRepoRepository;
}
namespace dns::gitops {
class GitRepoManager;
}

namespace dns::api::routes {

/// Handlers for /api/v1/git-repos
class GitRepoRoutes {
 public:
  GitRepoRoutes(dns::dal::GitRepoRepository& grRepo,
                const dns::api::AuthMiddleware& amMiddleware,
                dns::gitops::GitRepoManager& grmManager);
  ~GitRepoRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::GitRepoRepository& _grRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::gitops::GitRepoManager& _grmManager;
};

}  // namespace dns::api::routes
