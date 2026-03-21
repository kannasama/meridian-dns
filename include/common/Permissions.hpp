#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <array>
#include <string_view>

namespace dns::common {

/// Code-defined permission strings.
/// These are the canonical permission identifiers referenced by role_permissions rows.
namespace Permissions {

// Zones
inline constexpr std::string_view kZonesView       = "zones.view";
inline constexpr std::string_view kZonesCreate     = "zones.create";
inline constexpr std::string_view kZonesEdit       = "zones.edit";
inline constexpr std::string_view kZonesDelete     = "zones.delete";
inline constexpr std::string_view kZonesDeploy     = "zones.deploy";
inline constexpr std::string_view kZonesRollback   = "zones.rollback";

// Records
inline constexpr std::string_view kRecordsView     = "records.view";
inline constexpr std::string_view kRecordsCreate   = "records.create";
inline constexpr std::string_view kRecordsEdit     = "records.edit";
inline constexpr std::string_view kRecordsDelete   = "records.delete";
inline constexpr std::string_view kRecordsImport   = "records.import";

// Providers
inline constexpr std::string_view kProvidersView   = "providers.view";
inline constexpr std::string_view kProvidersCreate = "providers.create";
inline constexpr std::string_view kProvidersEdit   = "providers.edit";
inline constexpr std::string_view kProvidersDelete = "providers.delete";

// Provider Definitions
inline constexpr std::string_view kProviderDefinitionsView   = "provider_definitions.view";
inline constexpr std::string_view kProviderDefinitionsCreate = "provider_definitions.create";
inline constexpr std::string_view kProviderDefinitionsEdit   = "provider_definitions.edit";
inline constexpr std::string_view kProviderDefinitionsDelete = "provider_definitions.delete";

// Views
inline constexpr std::string_view kViewsView       = "views.view";
inline constexpr std::string_view kViewsCreate     = "views.create";
inline constexpr std::string_view kViewsEdit       = "views.edit";
inline constexpr std::string_view kViewsDelete     = "views.delete";

// Variables
inline constexpr std::string_view kVariablesView   = "variables.view";
inline constexpr std::string_view kVariablesCreate = "variables.create";
inline constexpr std::string_view kVariablesEdit   = "variables.edit";
inline constexpr std::string_view kVariablesDelete = "variables.delete";

// Snippets
inline constexpr std::string_view kSnippetsView   = "snippets.view";
inline constexpr std::string_view kSnippetsCreate = "snippets.create";
inline constexpr std::string_view kSnippetsEdit   = "snippets.edit";
inline constexpr std::string_view kSnippetsDelete = "snippets.delete";

// SOA Presets
inline constexpr std::string_view kSoaPresetsView   = "soa_presets.view";
inline constexpr std::string_view kSoaPresetsCreate = "soa_presets.create";
inline constexpr std::string_view kSoaPresetsEdit   = "soa_presets.edit";
inline constexpr std::string_view kSoaPresetsDelete = "soa_presets.delete";

// Zone Templates
inline constexpr std::string_view kTemplatesView   = "templates.view";
inline constexpr std::string_view kTemplatesCreate = "templates.create";
inline constexpr std::string_view kTemplatesEdit   = "templates.edit";
inline constexpr std::string_view kTemplatesDelete = "templates.delete";

// Git Repos
inline constexpr std::string_view kReposView       = "repos.view";
inline constexpr std::string_view kReposCreate     = "repos.create";
inline constexpr std::string_view kReposEdit       = "repos.edit";
inline constexpr std::string_view kReposDelete     = "repos.delete";

// Audit
inline constexpr std::string_view kAuditView       = "audit.view";
inline constexpr std::string_view kAuditExport     = "audit.export";
inline constexpr std::string_view kAuditPurge      = "audit.purge";

// Users
inline constexpr std::string_view kUsersView       = "users.view";
inline constexpr std::string_view kUsersCreate     = "users.create";
inline constexpr std::string_view kUsersEdit       = "users.edit";
inline constexpr std::string_view kUsersDelete     = "users.delete";

// Groups
inline constexpr std::string_view kGroupsView      = "groups.view";
inline constexpr std::string_view kGroupsCreate    = "groups.create";
inline constexpr std::string_view kGroupsEdit      = "groups.edit";
inline constexpr std::string_view kGroupsDelete    = "groups.delete";

// Roles
inline constexpr std::string_view kRolesView       = "roles.view";
inline constexpr std::string_view kRolesCreate     = "roles.create";
inline constexpr std::string_view kRolesEdit       = "roles.edit";
inline constexpr std::string_view kRolesDelete     = "roles.delete";

// Settings
inline constexpr std::string_view kSettingsView    = "settings.view";
inline constexpr std::string_view kSettingsEdit    = "settings.edit";

// Backup
inline constexpr std::string_view kBackupCreate    = "backup.create";
inline constexpr std::string_view kBackupRestore   = "backup.restore";

/// All known permissions, for validation and UI rendering.
inline constexpr std::array kAllPermissions = {
    kZonesView, kZonesCreate, kZonesEdit, kZonesDelete, kZonesDeploy, kZonesRollback,
    kRecordsView, kRecordsCreate, kRecordsEdit, kRecordsDelete, kRecordsImport,
    kProvidersView, kProvidersCreate, kProvidersEdit, kProvidersDelete,
    kProviderDefinitionsView, kProviderDefinitionsCreate,
    kProviderDefinitionsEdit, kProviderDefinitionsDelete,
    kViewsView, kViewsCreate, kViewsEdit, kViewsDelete,
    kVariablesView, kVariablesCreate, kVariablesEdit, kVariablesDelete,
    kSnippetsView, kSnippetsCreate, kSnippetsEdit, kSnippetsDelete,
    kSoaPresetsView, kSoaPresetsCreate, kSoaPresetsEdit, kSoaPresetsDelete,
    kTemplatesView, kTemplatesCreate, kTemplatesEdit, kTemplatesDelete,
    kReposView, kReposCreate, kReposEdit, kReposDelete,
    kAuditView, kAuditExport, kAuditPurge,
    kUsersView, kUsersCreate, kUsersEdit, kUsersDelete,
    kGroupsView, kGroupsCreate, kGroupsEdit, kGroupsDelete,
    kRolesView, kRolesCreate, kRolesEdit, kRolesDelete,
    kSettingsView, kSettingsEdit,
    kBackupCreate, kBackupRestore,
};

/// Permission categories for UI grouping.
struct PermissionCategory {
  std::string_view sName;
  std::array<std::string_view, 6> vPermissions;  // max 6 per category
  int iCount;
};

}  // namespace Permissions
}  // namespace dns::common
