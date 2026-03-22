#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class SystemLogRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// API routes for system log queries (admin only).
/// Class abbreviation: slrr
class SystemLogRoutes {
 public:
  SystemLogRoutes(dns::dal::SystemLogRepository& slrRepo,
                  const dns::api::AuthMiddleware& amMiddleware);
  ~SystemLogRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::SystemLogRepository& _slrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
