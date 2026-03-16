#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class ViewRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/views
/// Class abbreviation: vr
class ViewRoutes {
 public:
  ViewRoutes(dns::dal::ViewRepository& vrRepo,
             const dns::api::AuthMiddleware& amMiddleware);
  ~ViewRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ViewRepository& _vrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
