#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/Types.hpp"

namespace dns::dal {
class ProviderRepository;
class RecordRepository;
class ViewRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::core {

class VariableEngine;

/// Computes three-way diff between staged records and live provider state.
/// Class abbreviation: de
class DiffEngine {
 public:
  DiffEngine(dns::dal::ZoneRepository& zrRepo,
             dns::dal::ViewRepository& vrRepo,
             dns::dal::RecordRepository& rrRepo,
             dns::dal::ProviderRepository& prRepo,
             VariableEngine& veEngine);
  ~DiffEngine();

  /// Compute diff between desired state (DB + variable expansion) and live
  /// provider state for the given zone.
  common::PreviewResult preview(int64_t iZoneId);

  /// Pure diff algorithm: compare desired records against live records.
  /// Public for unit testing.
  static std::vector<common::RecordDiff> computeDiff(
      const std::vector<common::DnsRecord>& vDesired,
      const std::vector<common::DnsRecord>& vLive);

 private:
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::ProviderRepository& _prRepo;
  VariableEngine& _veEngine;
};

}  // namespace dns::core
