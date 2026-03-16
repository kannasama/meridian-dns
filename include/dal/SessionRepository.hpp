#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <string>

namespace dns::dal {

class ConnectionPool;

/// Manages the sessions table; create, touch, exists, deleteByHash, pruneExpired.
/// Class abbreviation: sr
class SessionRepository {
 public:
  explicit SessionRepository(ConnectionPool& cpPool);
  ~SessionRepository();

  /// Create a new session row. sliding TTL sets expires_at; absolute TTL sets
  /// absolute_expires_at. Both are relative to NOW().
  void create(int64_t iUserId, const std::string& sTokenHash,
              int iSlidingTtlSeconds, int iAbsoluteTtlSeconds);

  /// Update last_seen_at and extend expires_at by iSlidingTtl seconds,
  /// clamped to absolute_expires_at.
  void touch(const std::string& sTokenHash, int iSlidingTtl, int iAbsoluteTtl);

  /// Check if a session row exists for this token hash.
  bool exists(const std::string& sTokenHash);

  /// Returns true if the session exists and has not exceeded its sliding
  /// or absolute TTL. Also checks that the user is still active.
  /// Returns false if expired or revoked (row absent).
  bool isValid(const std::string& sTokenHash);

  /// Hard-delete a session row by token hash.
  void deleteByHash(const std::string& sTokenHash);

  /// Set SAML session index on an existing session (for SLO support).
  void setSamlSessionIndex(const std::string& sTokenHash,
                           const std::string& sSamlSessionIndex);

  /// Delete all sessions matching a SAML session index (IdP-initiated SLO).
  /// Returns the number of rows deleted.
  int deleteBySamlSessionIndex(const std::string& sSamlSessionIndex);

  /// Delete all sessions where expires_at < NOW(). Returns rows deleted.
  int pruneExpired();

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
