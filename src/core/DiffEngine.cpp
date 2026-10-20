// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/DiffEngine.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/DnsNameUtils.hpp"
#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "providers/ProviderFactory.hpp"

namespace dns::core {

namespace {

// Record matching is trailing-dot insensitive: a trailing dot on a name, or on
// a hostname-valued record's value, is DNS presentation syntax rather than
// content, and providers disagree about emitting it. Canonicalisation governs
// equality ONLY — emitted diffs always carry the original strings so the UI
// reports exactly what each side holds.

std::string makeNameTypeKey(const std::string& sName, const std::string& sType) {
  return common::canonicalName(sName) + "\t" + sType;
}

std::string makeKey(const std::string& sName, const std::string& sType,
                    const std::string& sValue) {
  return makeNameTypeKey(sName, sType) + "\t" + common::canonicalValue(sType, sValue);
}

std::string makeKey(const common::DnsRecord& dr) {
  return makeKey(dr.sName, dr.sType, dr.sValue);
}

}  // namespace

DiffEngine::DiffEngine(dns::dal::ZoneRepository& zrRepo,
                       dns::dal::ViewRepository& vrRepo,
                       dns::dal::RecordRepository& rrRepo,
                       dns::dal::ProviderRepository& prRepo,
                       VariableEngine& veEngine)
    : _zrRepo(zrRepo),
      _vrRepo(vrRepo),
      _rrRepo(rrRepo),
      _prRepo(prRepo),
      _veEngine(veEngine) {}

DiffEngine::~DiffEngine() = default;

std::vector<common::RecordDiff> DiffEngine::computeDiff(
    const std::vector<common::DnsRecord>& vDesired,
    const std::vector<common::DnsRecord>& vLive) {
  std::vector<common::RecordDiff> vDiffs;

  // Index live records by canonical (name, type, value) for exact-match lookup,
  // and keep a pointer back to the original record so an Update can recover its
  // untouched value and provider record ID.
  std::set<std::string> sLiveKeys;
  std::map<std::string, const common::DnsRecord*> mLiveByKey;
  for (const auto& dr : vLive) {
    auto sKey = makeKey(dr);
    sLiveKeys.insert(sKey);
    mLiveByKey.emplace(sKey, &dr);
  }

  std::set<std::string> sDesiredKeys;
  for (const auto& dr : vDesired) {
    sDesiredKeys.insert(makeKey(dr));
  }

  // Build (name, type) -> canonical values map for update detection
  std::map<std::string, std::vector<std::string>> mLiveByNameType;
  for (const auto& dr : vLive) {
    mLiveByNameType[makeNameTypeKey(dr.sName, dr.sType)].push_back(
        common::canonicalValue(dr.sType, dr.sValue));
  }

  // Track which live values have been consumed by update pairings
  std::set<std::string> sConsumedLive;

  // Mark exact matches as consumed so they aren't paired with other desired records
  for (const auto& dr : vDesired) {
    auto sExactKey = makeKey(dr);
    if (sLiveKeys.count(sExactKey)) {
      sConsumedLive.insert(sExactKey);
    }
  }

  // 1. Check desired records: Add or Update
  for (const auto& dr : vDesired) {
    auto sExactKey = makeKey(dr);
    if (sLiveKeys.count(sExactKey)) continue;  // exact match — no diff

    auto sNameTypeKey = makeNameTypeKey(dr.sName, dr.sType);
    auto itLive = mLiveByNameType.find(sNameTypeKey);

    const common::DnsRecord* pLiveMatch = nullptr;
    if (itLive != mLiveByNameType.end()) {
      // Same name+type exists on provider but value differs -> Update.
      // Pair with the first live value not claimed by another desired record.
      for (const auto& sLiveVal : itLive->second) {
        auto sLiveExact = sNameTypeKey + "\t" + sLiveVal;
        if (!sDesiredKeys.count(sLiveExact) && !sConsumedLive.count(sLiveExact)) {
          auto itRec = mLiveByKey.find(sLiveExact);
          if (itRec != mLiveByKey.end()) {
            sConsumedLive.insert(sLiveExact);
            pLiveMatch = itRec->second;
          }
          break;
        }
      }
    }

    common::RecordDiff rd;
    rd.sName = dr.sName;
    rd.sType = dr.sType;
    rd.sSourceValue = dr.sValue;
    rd.uTtl = dr.uTtl;
    rd.iPriority = dr.iPriority;
    rd.jProviderMeta = dr.jProviderMeta;

    if (pLiveMatch) {
      rd.action = common::DiffAction::Update;
      rd.sProviderValue = pLiveMatch->sValue;
      rd.sProviderRecordId = pLiveMatch->sProviderRecordId;
    } else {
      rd.action = common::DiffAction::Add;
    }
    vDiffs.push_back(std::move(rd));
  }

  // 2. Check live records for drift
  for (const auto& dr : vLive) {
    auto sExactKey = makeKey(dr);
    if (sDesiredKeys.count(sExactKey)) continue;

    // Skip live records already claimed by an exact match or an Update pairing
    if (sConsumedLive.count(sExactKey)) continue;

    common::RecordDiff rd;
    rd.action = common::DiffAction::Drift;
    rd.sName = dr.sName;
    rd.sType = dr.sType;
    rd.sProviderValue = dr.sValue;
    rd.uTtl = dr.uTtl;
    rd.iPriority = dr.iPriority;
    rd.jProviderMeta = dr.jProviderMeta;
    rd.sProviderRecordId = dr.sProviderRecordId;
    vDiffs.push_back(std::move(rd));
  }

  return vDiffs;
}

std::vector<common::DnsRecord> DiffEngine::filterRecordTypes(
    const std::vector<common::DnsRecord>& vRecords,
    bool bIncludeSoa, bool bIncludeNs) {
  std::vector<common::DnsRecord> vFiltered;
  vFiltered.reserve(vRecords.size());
  for (const auto& dr : vRecords) {
    if (!bIncludeSoa && dr.sType == "SOA") continue;
    if (!bIncludeNs && dr.sType == "NS") continue;
    vFiltered.push_back(dr);
  }
  return vFiltered;
}

std::vector<common::DnsRecord> DiffEngine::fetchLiveRecords(int64_t iZoneId) {
  auto spLog = common::Logger::get();

  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  auto oView = _vrRepo.findWithProviders(oZone->iViewId);
  if (!oView) {
    throw common::NotFoundError("VIEW_NOT_FOUND",
                                "View " + std::to_string(oZone->iViewId) + " not found");
  }
  if (oView->vProviderIds.empty()) {
    throw common::ValidationError("NO_PROVIDERS",
                                  "View '" + oView->sName + "' has no providers attached");
  }

  std::vector<common::DnsRecord> vLive;
  for (int64_t iProviderId : oView->vProviderIds) {
    auto oProvider = _prRepo.findById(iProviderId);
    if (!oProvider) {
      spLog->warn("Provider {} not found — skipping", iProviderId);
      continue;
    }

    auto upProvider = dns::providers::ProviderFactory::create(
        oProvider->sType, oProvider->sApiEndpoint, oProvider->sDecryptedToken,
        oProvider->jConfig);

    try {
      auto vProviderRecords = upProvider->listRecords(oZone->sName);
      vLive.insert(vLive.end(), vProviderRecords.begin(), vProviderRecords.end());
    } catch (const common::ProviderError& ex) {
      spLog->error("Failed to list records from provider '{}': {}", oProvider->sName, ex.what());
      throw;
    }
  }

  return vLive;
}

std::map<int64_t, std::vector<common::DnsRecord>> DiffEngine::fetchLiveRecordsPerProvider(
    int64_t iZoneId) {
  auto spLog = common::Logger::get();

  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  auto oView = _vrRepo.findWithProviders(oZone->iViewId);
  if (!oView) {
    throw common::NotFoundError("VIEW_NOT_FOUND",
                                "View " + std::to_string(oZone->iViewId) + " not found");
  }
  if (oView->vProviderIds.empty()) {
    throw common::ValidationError("NO_PROVIDERS",
                                  "View '" + oView->sName + "' has no providers attached");
  }

  std::map<int64_t, std::vector<common::DnsRecord>> mResult;
  for (int64_t iProviderId : oView->vProviderIds) {
    auto oProvider = _prRepo.findById(iProviderId);
    if (!oProvider) {
      spLog->warn("Provider {} not found — skipping", iProviderId);
      continue;
    }

    auto upProvider = dns::providers::ProviderFactory::create(
        oProvider->sType, oProvider->sApiEndpoint, oProvider->sDecryptedToken,
        oProvider->jConfig);

    try {
      mResult[iProviderId] = upProvider->listRecords(oZone->sName);
    } catch (const common::ProviderError& ex) {
      spLog->error("Failed to list from provider '{}': {}", oProvider->sName, ex.what());
      throw;
    }
  }

  return mResult;
}

common::PreviewResult DiffEngine::preview(int64_t iZoneId) {
  auto spLog = common::Logger::get();

  // 1. Look up zone
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  // 2. Fetch desired records from DB and expand templates
  auto vRecordRows = _rrRepo.listByZoneId(iZoneId);
  std::vector<common::DnsRecord> vDesired;
  std::vector<common::DnsRecord> vPendingDelete;
  vDesired.reserve(vRecordRows.size());
  for (const auto& row : vRecordRows) {
    common::DnsRecord dr;
    dr.sName = toFqdn(row.sName, oZone->sName);
    dr.sType = row.sType;
    dr.uTtl = static_cast<uint32_t>(row.iTtl);
    dr.sValue = _veEngine.expand(row.sValueTemplate, iZoneId);
    dr.iPriority = row.iPriority;
    dr.jProviderMeta = row.jProviderMeta;
    if (row.bPendingDelete) {
      vPendingDelete.push_back(std::move(dr));
    } else {
      vDesired.push_back(std::move(dr));
    }
  }

  vDesired = filterRecordTypes(vDesired, oZone->bManageSoa, oZone->bManageNs);
  vPendingDelete = filterRecordTypes(vPendingDelete, oZone->bManageSoa, oZone->bManageNs);

  // 3. Fetch live records per provider
  auto mLive = fetchLiveRecordsPerProvider(iZoneId);

  // 4. Compute per-provider diffs
  common::PreviewResult pr;
  pr.iZoneId = iZoneId;
  pr.sZoneName = oZone->sName;

  for (auto& [iProviderId, vLive] : mLive) {
    vLive = filterRecordTypes(vLive, oZone->bManageSoa, oZone->bManageNs);

    auto oProvider = _prRepo.findById(iProviderId);

    // For Cloudflare providers, normalize desired TTL to 1 for auto_ttl records
    // so the diff doesn't flag a false TTL mismatch against Cloudflare's TTL=1
    auto vProviderDesired = vDesired;
    if (oProvider && oProvider->sType == "cloudflare") {
      for (auto& dr : vProviderDesired) {
        if (!dr.jProviderMeta.is_null() && dr.jProviderMeta.value("auto_ttl", false)) {
          dr.uTtl = 1;
        }
      }
    }

    // Allow each provider to filter/transform desired records before diffing
    if (oProvider) {
      auto upProviderInst = dns::providers::ProviderFactory::create(
          oProvider->sType, oProvider->sApiEndpoint, oProvider->sDecryptedToken,
          oProvider->jConfig);
      vProviderDesired = upProviderInst->prepareDesiredRecords(vProviderDesired);
    }

    // Pre-match pending-delete records against live records and remove matched
    // live records so they don't appear as drift in computeDiff().
    // Match by name+type first; use value as tiebreaker when multiple records
    // share the same name+type.
    std::vector<common::RecordDiff> vDeleteDiffs;
    std::set<std::string> sDeleteConsumedIds;  // provider record IDs consumed by deletes

    for (const auto& drPending : vPendingDelete) {
      const common::DnsRecord* pBestMatch = nullptr;
      for (const auto& drLive : vLive) {
        if (drLive.sName != drPending.sName || drLive.sType != drPending.sType) continue;
        if (sDeleteConsumedIds.count(drLive.sProviderRecordId)) continue;
        if (drLive.sValue == drPending.sValue) {
          pBestMatch = &drLive;  // exact match — prefer this
          break;
        }
        if (!pBestMatch) {
          pBestMatch = &drLive;  // name+type match as fallback
        }
      }
      if (pBestMatch) {
        sDeleteConsumedIds.insert(pBestMatch->sProviderRecordId);
        common::RecordDiff rd;
        rd.action = common::DiffAction::Delete;
        rd.sName = pBestMatch->sName;
        rd.sType = pBestMatch->sType;
        rd.sProviderValue = pBestMatch->sValue;
        rd.sProviderRecordId = pBestMatch->sProviderRecordId;
        rd.uTtl = pBestMatch->uTtl;
        rd.iPriority = pBestMatch->iPriority;
        rd.jProviderMeta = pBestMatch->jProviderMeta;
        vDeleteDiffs.push_back(std::move(rd));
      }
    }

    // Filter out live records consumed by pending deletes before computing diff
    std::vector<common::DnsRecord> vFilteredLive;
    vFilteredLive.reserve(vLive.size());
    for (const auto& drLive : vLive) {
      if (!sDeleteConsumedIds.count(drLive.sProviderRecordId)) {
        vFilteredLive.push_back(drLive);
      }
    }

    auto vDiffs = computeDiff(vProviderDesired, vFilteredLive);

    // Append the Delete diffs
    vDiffs.insert(vDiffs.end(), vDeleteDiffs.begin(), vDeleteDiffs.end());

    common::ProviderPreviewResult ppr;
    ppr.iProviderId = iProviderId;
    ppr.sProviderName = oProvider ? oProvider->sName : "unknown";
    ppr.sProviderType = oProvider ? oProvider->sType : "unknown";
    ppr.vDiffs = vDiffs;
    ppr.bHasDrift = std::any_of(vDiffs.begin(), vDiffs.end(),
                                 [](const auto& d) {
                                   return d.action == common::DiffAction::Drift;
                                 });
    pr.vProviderPreviews.push_back(std::move(ppr));

    // Merge into combined diffs for backward compat
    pr.vDiffs.insert(pr.vDiffs.end(), vDiffs.begin(), vDiffs.end());
  }

  pr.bHasDrift = std::any_of(pr.vDiffs.begin(), pr.vDiffs.end(),
                              [](const auto& d) {
                                return d.action == common::DiffAction::Drift;
                              });
  pr.tpGeneratedAt = std::chrono::system_clock::now();

  spLog->info("DiffEngine: zone '{}' — {} diffs across {} providers, drift={}",
              oZone->sName, pr.vDiffs.size(), pr.vProviderPreviews.size(), pr.bHasDrift);
  return pr;
}

std::string DiffEngine::toFqdn(const std::string& sRecordName, const std::string& sZoneName) {
  // Ensure zone name has trailing dot
  std::string sZoneFqdn = sZoneName;
  if (!sZoneFqdn.empty() && sZoneFqdn.back() != '.') {
    sZoneFqdn += '.';
  }

  // Already an FQDN (trailing dot)
  if (!sRecordName.empty() && sRecordName.back() == '.') {
    return sRecordName;
  }

  // "@" or empty → zone apex
  if (sRecordName == "@" || sRecordName.empty()) {
    return sZoneFqdn;
  }

  // Relative name → prepend to zone
  return sRecordName + "." + sZoneFqdn;
}

std::string DiffEngine::fromFqdn(const std::string& sFqdn, const std::string& sZoneName) {
  // Ensure zone name has trailing dot
  std::string sZoneFqdn = sZoneName;
  if (!sZoneFqdn.empty() && sZoneFqdn.back() != '.') {
    sZoneFqdn += '.';
  }

  // Strip trailing dot from input for comparison
  std::string sName = sFqdn;
  if (!sName.empty() && sName.back() == '.') {
    sName.pop_back();
  }

  std::string sZoneNoDot = sZoneFqdn;
  if (!sZoneNoDot.empty() && sZoneNoDot.back() == '.') {
    sZoneNoDot.pop_back();
  }

  // Exact match with zone → apex record "@"
  if (sName == sZoneNoDot) {
    return "@";
  }

  // Ends with ".zone" → strip the suffix to get relative name
  std::string sSuffix = "." + sZoneNoDot;
  if (sName.size() > sSuffix.size() &&
      sName.compare(sName.size() - sSuffix.size(), sSuffix.size(), sSuffix) == 0) {
    return sName.substr(0, sName.size() - sSuffix.size());
  }

  // Not a subdomain of the zone — return as-is (already relative or external)
  return sFqdn;
}

}  // namespace dns::core
