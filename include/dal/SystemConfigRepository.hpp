// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once

#include <string>

namespace dns::dal {

class ConnectionPool;

/// Manages the system_config table for durable key-value settings.
/// Class abbreviation: scr
class SystemConfigRepository {
 public:
  explicit SystemConfigRepository(ConnectionPool& cpPool);
  ~SystemConfigRepository();

  /// Atomically get and increment the SOA serial counter.
  /// Returns the new serial as a 10-character string: YYYYMMDDNN
  /// where NN is the zero-padded two-digit suffix (00-99).
  /// Resets NN to 00 when the UTC date changes from the stored date.
  /// Throws std::runtime_error if NN would exceed 99.
  std::string getAndIncrementSerial();

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
