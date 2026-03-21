// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/VariableEngine.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/Errors.hpp"
#include "dal/VariableRepository.hpp"

namespace dns::core {

VariableEngine::VariableEngine() = default;

VariableEngine::VariableEngine(dns::dal::VariableRepository& varRepo)
    : _pVarRepo(&varRepo) {}

VariableEngine::~VariableEngine() = default;

std::string VariableEngine::expand(const std::string& sTmpl, int64_t iZoneId,
                                    const SysContext& sysCtx) const {
  auto vDeps = listDependencies(sTmpl);
  if (vDeps.empty()) return sTmpl;

  if (!_pVarRepo) {
    throw std::runtime_error("VariableEngine: no VariableRepository injected");
  }

  // Fetch all variables for this zone (static zone-scoped + global + dynamic)
  auto vRows = _pVarRepo->listByZoneId(iZoneId);

  // Build lookup maps: static (zone-scoped overwrites global) and dynamic formats
  std::unordered_map<std::string, std::string> mStatic;
  std::unordered_map<std::string, std::string> mDynamicFormat;

  for (const auto& row : vRows) {
    if (row.sVariableKind == "dynamic") {
      if (row.osDynamicFormat.has_value()) {
        mDynamicFormat[row.sName] = *row.osDynamicFormat;
      }
    } else {
      // static global: emplace won't overwrite an already-inserted key
      if (row.sScope == "global") mStatic.emplace(row.sName, row.sValue);
    }
  }
  // zone-scoped statics overwrite globals
  for (const auto& row : vRows) {
    if (row.sVariableKind != "dynamic" && row.sScope == "zone") {
      mStatic[row.sName] = row.sValue;
    }
  }

  // Capture current UTC time once for all time-based resolutions
  auto tpNow = std::chrono::system_clock::now();
  auto ttNow = std::chrono::system_clock::to_time_t(tpNow);
  std::tm tmUtc{};
  gmtime_r(&ttNow, &tmUtc);

  static const std::regex reVar(R"(\{\{([A-Za-z0-9_.]+)\}\})");
  std::string sResult;
  auto itBegin = std::sregex_iterator(sTmpl.begin(), sTmpl.end(), reVar);
  auto itEnd   = std::sregex_iterator();

  size_t iLastPos = 0;
  for (auto it = itBegin; it != itEnd; ++it) {
    sResult.append(sTmpl, iLastPos, static_cast<size_t>(it->position()) - iLastPos);
    const std::string sVarName = (*it)[1].str();
    std::string sResolved;

    if (sVarName == "sys.zone") {
      if (sysCtx.sZoneName.empty()) {
        throw common::UnresolvedVariableError(
            "UNDEFINED_VARIABLE", "{{sys.zone}} requires a deployment context");
      }
      sResolved = sysCtx.sZoneName;
    } else if (sVarName == "sys.serial") {
      if (sysCtx.sSerial.empty()) {
        throw common::UnresolvedVariableError(
            "UNDEFINED_VARIABLE", "{{sys.serial}} is only available during deployment push");
      }
      sResolved = sysCtx.sSerial;
    } else if (sVarName == "sys.date") {
      char buf[9] = {};
      std::strftime(buf, sizeof(buf), "%Y%m%d", &tmUtc);
      sResolved = buf;
    } else if (sVarName == "sys.datetime") {
      char buf[15] = {};
      std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tmUtc);
      sResolved = buf;
    } else if (sVarName == "sys.timestamp") {
      sResolved = std::to_string(static_cast<int64_t>(ttNow));
    } else {
      // Static variable lookup
      auto itStatic = mStatic.find(sVarName);
      if (itStatic != mStatic.end()) {
        sResolved = itStatic->second;
      } else {
        // User-defined dynamic variable
        auto itDyn = mDynamicFormat.find(sVarName);
        if (itDyn != mDynamicFormat.end()) {
          char buf[128] = {};
          std::strftime(buf, sizeof(buf), itDyn->second.c_str(), &tmUtc);
          sResolved = buf;
        } else {
          throw common::UnresolvedVariableError(
              "UNDEFINED_VARIABLE",
              "Variable '" + sVarName + "' not found for zone " + std::to_string(iZoneId));
        }
      }
    }

    sResult.append(sResolved);
    iLastPos = static_cast<size_t>(it->position() + it->length());
  }
  sResult.append(sTmpl, iLastPos);
  return sResult;
}

bool VariableEngine::validate(const std::string& sTmpl, int64_t iZoneId) const {
  if (!_pVarRepo) {
    throw std::runtime_error("VariableEngine: no VariableRepository injected");
  }

  auto vDeps = listDependencies(sTmpl);
  if (vDeps.empty()) return true;

  auto vRows = _pVarRepo->listByZoneId(iZoneId);
  std::unordered_map<std::string, bool> mExists;
  for (const auto& row : vRows) mExists[row.sName] = true;

  static const std::string sSysPrefix = "sys.";
  for (const auto& sDep : vDeps) {
    if (sDep.rfind(sSysPrefix, 0) == 0) continue;  // sys.* always valid
    if (mExists.find(sDep) == mExists.end()) return false;
  }
  return true;
}

std::vector<std::string> VariableEngine::listDependencies(const std::string& sTmpl) const {
  static const std::regex reVar(R"(\{\{([A-Za-z0-9_.]+)\}\})");
  std::vector<std::string> vDeps;

  auto itBegin = std::sregex_iterator(sTmpl.begin(), sTmpl.end(), reVar);
  auto itEnd = std::sregex_iterator();

  for (auto it = itBegin; it != itEnd; ++it) {
    std::string sName = (*it)[1].str();
    if (std::find(vDeps.begin(), vDeps.end(), sName) == vDeps.end()) {
      vDeps.push_back(std::move(sName));
    }
  }

  return vDeps;
}

}  // namespace dns::core
