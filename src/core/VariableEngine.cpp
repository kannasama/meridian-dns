// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/VariableEngine.hpp"

#include <algorithm>
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

std::string VariableEngine::expand(const std::string& sTmpl, int64_t iZoneId) const {
  if (!_pVarRepo) {
    throw std::runtime_error("VariableEngine: no VariableRepository injected");
  }

  auto vDeps = listDependencies(sTmpl);
  if (vDeps.empty()) return sTmpl;

  // Fetch all variables for this zone (zone-scoped + global)
  auto vRows = _pVarRepo->listByZoneId(iZoneId);

  // Build lookup map: zone-scoped vars shadow global vars
  std::unordered_map<std::string, std::string> mVars;
  for (const auto& row : vRows) {
    if (row.sScope == "global") {
      mVars.emplace(row.sName, row.sValue);  // emplace won't overwrite
    }
  }
  for (const auto& row : vRows) {
    if (row.sScope == "zone") {
      mVars[row.sName] = row.sValue;  // overwrites global
    }
  }

  // Replace all {{var}} placeholders
  static const std::regex reVar(R"(\{\{([A-Za-z0-9_]+)\}\})");
  std::string sResult;
  auto itBegin = std::sregex_iterator(sTmpl.begin(), sTmpl.end(), reVar);
  auto itEnd = std::sregex_iterator();

  size_t iLastPos = 0;
  for (auto it = itBegin; it != itEnd; ++it) {
    sResult.append(sTmpl, iLastPos, static_cast<size_t>(it->position()) - iLastPos);
    std::string sVarName = (*it)[1].str();
    auto found = mVars.find(sVarName);
    if (found == mVars.end()) {
      throw common::UnresolvedVariableError(
          "UNDEFINED_VARIABLE",
          "Variable '" + sVarName + "' not found for zone " + std::to_string(iZoneId));
    }
    sResult.append(found->second);
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
  for (const auto& row : vRows) {
    mExists[row.sName] = true;
  }

  for (const auto& sDep : vDeps) {
    if (mExists.find(sDep) == mExists.end()) return false;
  }
  return true;
}

std::vector<std::string> VariableEngine::listDependencies(const std::string& sTmpl) const {
  static const std::regex reVar(R"(\{\{([A-Za-z0-9_]+)\}\})");
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
