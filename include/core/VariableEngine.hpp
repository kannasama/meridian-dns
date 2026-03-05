#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dns::dal {
class VariableRepository;
}

namespace dns::core {

/// Tokenizes and expands {{var}} placeholders in record templates.
/// Class abbreviation: ve
class VariableEngine {
 public:
  /// Construct without a repository — only listDependencies() is usable.
  VariableEngine();

  /// Construct with a repository — all methods are usable.
  explicit VariableEngine(dns::dal::VariableRepository& varRepo);

  ~VariableEngine();

  /// Expand all {{var}} placeholders in sTmpl using variables for iZoneId.
  /// Zone-scoped variables take precedence over global.
  /// Throws UnresolvedVariableError if any variable cannot be resolved.
  std::string expand(const std::string& sTmpl, int64_t iZoneId) const;

  /// Returns true if all {{var}} placeholders in sTmpl can be resolved for iZoneId.
  bool validate(const std::string& sTmpl, int64_t iZoneId) const;

  /// Extract variable names from {{var}} placeholders. No DB access needed.
  std::vector<std::string> listDependencies(const std::string& sTmpl) const;

 private:
  dns::dal::VariableRepository* _pVarRepo = nullptr;
};

}  // namespace dns::core
