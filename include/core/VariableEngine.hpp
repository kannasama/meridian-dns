#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <string>
#include <vector>

namespace dns::dal {
class VariableRepository;
}

namespace dns::core {

/// Context for built-in system variables. Pass to expand() during deployment.
/// For preview calls, leave default-constructed — sys.serial will throw if referenced.
struct SysContext {
  std::string sZoneName;  ///< Value for {{sys.zone}}; throws if empty when referenced
  std::string sSerial;    ///< Pre-fetched serial for {{sys.serial}}; throws if empty
};

/// Tokenizes and expands {{var}} placeholders in record templates.
/// Class abbreviation: ve
class VariableEngine {
 public:
  /// Construct without a repository — only listDependencies() is usable.
  VariableEngine();

  /// Construct with a repository — all methods are usable.
  explicit VariableEngine(dns::dal::VariableRepository& varRepo);

  ~VariableEngine();

  /// Expand all {{var}} and {{sys.*}} placeholders in sTmpl using variables for iZoneId.
  /// Zone-scoped static variables > global static > built-in sys > user-defined dynamic.
  /// Throws UnresolvedVariableError if any placeholder cannot be resolved.
  std::string expand(const std::string& sTmpl, int64_t iZoneId,
                     const SysContext& sysCtx = {}) const;

  /// Returns true if all {{var}} placeholders in sTmpl can be resolved for iZoneId.
  bool validate(const std::string& sTmpl, int64_t iZoneId) const;

  /// Extract variable names from {{var}} placeholders. No DB access needed.
  std::vector<std::string> listDependencies(const std::string& sTmpl) const;

 private:
  dns::dal::VariableRepository* _pVarRepo = nullptr;
};

}  // namespace dns::core
