#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::common {

/// DNS record as seen by providers.
/// Class abbreviation: dr
struct DnsRecord {
  std::string sProviderRecordId;
  std::string sName;
  std::string sType;
  uint32_t uTtl = 300;
  std::string sValue;
  int iPriority = 0;
  nlohmann::json jProviderMeta;  // Provider-specific metadata (e.g., {"proxied": true})
};

/// Provider health status.
enum class HealthStatus { Ok, Degraded, Unreachable };

/// Result of a provider push operation.
/// Class abbreviation: prs
struct PushResult {
  bool bSuccess = false;
  std::string sProviderRecordId;
  std::string sErrorMessage;
};

/// Diff action type.
enum class DiffAction { Add, Update, Delete, Drift };

/// A single record diff entry.
/// Class abbreviation: rd
struct RecordDiff {
  DiffAction action;
  std::string sName;
  std::string sType;
  std::string sProviderValue;
  std::string sSourceValue;
  uint32_t uTtl = 300;
  int iPriority = 0;
  nlohmann::json jProviderMeta;  // Provider metadata from source record
  std::string sProviderRecordId;  // Provider's native record ID for deletes
};

/// Per-provider preview result for multi-provider deployments.
/// Class abbreviation: ppr
struct ProviderPreviewResult {
  int64_t iProviderId = 0;
  std::string sProviderName;
  std::string sProviderType;
  std::vector<RecordDiff> vDiffs;
  bool bHasDrift = false;
};

/// Result of a preview/diff operation.
/// Class abbreviation: pr
struct PreviewResult {
  int64_t iZoneId = 0;
  std::string sZoneName;
  std::vector<RecordDiff> vDiffs;             // Merged diffs (backward compat)
  bool bHasDrift = false;
  std::chrono::system_clock::time_point tpGeneratedAt;
  std::vector<ProviderPreviewResult> vProviderPreviews;  // Per-provider breakdown
};

/// User decision for a drift record during deployment.
/// Class abbreviation: da
struct DriftAction {
  std::string sName;
  std::string sType;
  std::string sAction;  // "adopt", "delete", or "ignore"
};

/// Identity context injected by AuthMiddleware.
/// Class abbreviation: rc
struct RequestContext {
  int64_t iUserId = 0;
  std::string sUsername;
  std::string sDisplayName;
  std::string sRole;          // Display-only: highest-privilege role name
  std::string sAuthMethod;
  std::string sIpAddress;
  std::unordered_set<std::string> vPermissions;  // Effective permissions for this request
};

}  // namespace dns::common
