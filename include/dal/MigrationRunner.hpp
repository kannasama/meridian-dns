#pragma once

#include <string>

namespace dns::dal {

/// Runs SQL migrations from versioned directories against PostgreSQL.
/// Bootstraps schema_version and system_config tables on first run.
/// Class abbreviation: mr
class MigrationRunner {
 public:
  /// @param sDbUrl PostgreSQL connection string
  /// @param sMigrationsDir Path to directory containing vNNN/ subdirectories
  MigrationRunner(const std::string& sDbUrl, const std::string& sMigrationsDir);

  /// Run all pending migrations. Returns the new schema version.
  /// Throws std::runtime_error on failure (transaction is rolled back for the failing version).
  int migrate();

  /// Get the current schema version from the database.
  /// Returns 0 if schema_version table doesn't exist.
  int currentVersion();

 private:
  /// Create schema_version and system_config tables if they don't exist.
  void bootstrap();

  std::string _sDbUrl;
  std::string _sMigrationsDir;
};

}  // namespace dns::dal
