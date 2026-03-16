#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::core {
class BackupService;
}

namespace dns::dal {
class SettingsRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::gitops {
class GitRepoManager;
}

namespace dns::api::routes {

/// Handlers for /api/v1/backup and zone export/import
/// Class abbreviation: bkr
class BackupRoutes {
 public:
  BackupRoutes(dns::core::BackupService& bsService,
               dns::dal::SettingsRepository& stRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::gitops::GitRepoManager* pGitRepoManager = nullptr);
  ~BackupRoutes();

  /// Register backup/restore routes on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::core::BackupService& _bsService;
  dns::dal::SettingsRepository& _stRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::gitops::GitRepoManager* _pGitRepoManager;
};

}  // namespace dns::api::routes
