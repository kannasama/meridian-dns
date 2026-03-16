#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class RoleRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Role CRUD + permission assignment routes.
/// Class abbreviation: rr (role routes — note: the RoleRepository abbreviation is also rr,
/// but they live in different namespaces)
class RoleRoutes {
 public:
  RoleRoutes(dns::dal::RoleRepository& rrRepo,
             const dns::api::AuthMiddleware& amMiddleware);
  ~RoleRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::RoleRepository& _rrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
