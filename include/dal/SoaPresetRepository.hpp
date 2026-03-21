// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

struct SoaPresetRow {
  int64_t iId         = 0;
  std::string sName;
  std::string sMnameTemplate;
  std::string sRnameTemplate;
  int iRefresh        = 3600;
  int iRetry          = 900;
  int iExpire         = 604800;
  int iMinimum        = 300;
  int iDefaultTtl     = 3600;
  std::chrono::system_clock::time_point tpCreatedAt;
  std::chrono::system_clock::time_point tpUpdatedAt;
};

/// Manages the soa_presets table.
/// Class abbreviation: spr
class SoaPresetRepository {
 public:
  explicit SoaPresetRepository(ConnectionPool& cpPool);
  ~SoaPresetRepository();

  int64_t create(const std::string& sName,
                 const std::string& sMnameTemplate,
                 const std::string& sRnameTemplate,
                 int iRefresh, int iRetry, int iExpire,
                 int iMinimum, int iDefaultTtl);
  std::vector<SoaPresetRow> listAll();
  std::optional<SoaPresetRow> findById(int64_t iId);
  void update(int64_t iId,
              const std::string& sName,
              const std::string& sMnameTemplate,
              const std::string& sRnameTemplate,
              int iRefresh, int iRetry, int iExpire,
              int iMinimum, int iDefaultTtl);
  void deleteById(int64_t iId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
