#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace dns::common {

/// Thin wrapper over spdlog for structured logging.
/// Uses spdlog's default logger to avoid static destruction order issues.
/// Class abbreviation: N/A (static interface)
///
/// Usage:
///   Logger::init("debug");
///   Logger::get()->info("Server starting on port {}", iPort);
class Logger {
 public:
  /// Initialize the global logger with the given level string.
  /// Valid levels: "trace", "debug", "info", "warn", "error", "critical", "off"
  static void init(const std::string& sLevel);

  /// Get the shared spdlog logger instance.
  /// Returns spdlog's default logger (always valid).
  static std::shared_ptr<spdlog::logger> get();

 private:
  static bool _bInitialized;
};

}  // namespace dns::common
