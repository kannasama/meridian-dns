# Phase 8: REST API Hardening + Docker Compose Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Harden the full API surface with input validation, security headers, and rate limiting; containerize the application with Docker Compose for one-command startup; write a comprehensive OpenAPI spec; and add a full API integration test suite.

**Architecture:** A new `RequestValidator` utility centralizes input length/format validation (ARCHITECTURE.md §4.6.5). Security headers (SEC-12) are applied via shared `RouteHelpers`. A token-bucket rate limiter protects auth endpoints in-process. The multi-stage Dockerfile and docker-compose.yml follow ARCHITECTURE.md §11. The OpenAPI spec documents every endpoint from §6. Integration tests exercise the full route→repository chain using GMock provider/repository fakes.

**Tech Stack:** C++20, Crow HTTP, libpqxx, nlohmann/json, GoogleTest/GMock, spdlog, Docker, docker-compose

---

## Existing Context

**Current state (Phase 7 complete):**
- 180 tests (89 pass, 91 skip — DB integration tests need `DNS_DB_URL`)
- All startup steps 1–12 wired in `src/main.cpp`
- 9 route classes registered: Auth, Audit, Deployment, Health, Provider, View, Zone, Record, Variable
- Each route file duplicates `requireRole()`, `authenticate()`, `jsonResponse()`, `errorResponse()`
- No input length validation beyond "field not empty" checks
- No security response headers, no rate limiting
- No Dockerfile or docker-compose.yml, no OpenAPI spec

**Input validation limits (ARCHITECTURE.md §4.6.5):**

| Field | Max Length |
|-------|-----------|
| Zone name | 253 |
| Record name | 253 |
| Record value/template | 4,096 |
| Variable name | 64 |
| Variable value | 4,096 |
| Provider name | 128 |
| Username | 128 |
| Password | 1,024 |
| API key description | 512 |
| Group name | 128 |
| Audit identity filter | 128 |

**Naming conventions:** `s` = string, `i` = int, `b` = bool, `v` = vector, `o` = optional, `p` = raw ptr, `sp` = shared_ptr, `up` = unique_ptr. Member vars `_` prefixed, functions camelCase, classes PascalCase.

**Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
**Test:** `build/tests/dns-tests`

---

## Task 1: Extract Common Route Helpers into RouteHelpers

**Files:**
- Create: `include/api/RouteHelpers.hpp`
- Create: `src/api/RouteHelpers.cpp`
- Modify: All 8 route files in `src/api/routes/` (remove duplicated helpers)

### Step 1: Create the shared header

Create `include/api/RouteHelpers.hpp`:

```cpp
#pragma once

#include <string>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "common/Types.hpp"

namespace dns::api {

/// Authenticate a Crow request via AuthMiddleware.
common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req);

/// Enforce minimum role. Throws AuthorizationError if insufficient.
void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole);

/// Build a JSON response with Content-Type and security headers.
crow::response jsonResponse(int iStatus, const nlohmann::json& j);

/// Build an error response from an AppError with security headers.
crow::response errorResponse(const common::AppError& e);

/// Build an error response for invalid JSON parse failures.
crow::response invalidJsonResponse();

}  // namespace dns::api
```

### Step 2: Create the implementation

Create `src/api/RouteHelpers.cpp`:

```cpp
#include "api/RouteHelpers.hpp"

namespace dns::api {

common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  return amMiddleware.authenticate(req.get_header_value("Authorization"),
                                   req.get_header_value("X-API-Key"));
}

void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole) {
  if (sMinRole == "admin" && rcCtx.sRole != "admin") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE", "Admin role required");
  }
  if (sMinRole == "operator" && rcCtx.sRole == "viewer") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE",
                                     "Operator or admin role required");
  }
}

crow::response jsonResponse(int iStatus, const nlohmann::json& j) {
  crow::response resp(iStatus, j.dump(2));
  resp.set_header("Content-Type", "application/json");
  return resp;
}

crow::response errorResponse(const common::AppError& e) {
  nlohmann::json jErr = {{"error", e._sErrorCode}, {"message", e.what()}};
  return crow::response(e._iHttpStatus, jErr.dump(2));
}

crow::response invalidJsonResponse() {
  nlohmann::json jErr = {{"error", "invalid_json"},
                         {"message", "Invalid JSON body"}};
  return crow::response(400, jErr.dump(2));
}

}  // namespace dns::api
```

### Step 3: Update all route files

In each of the 8 route files (`RecordRoutes.cpp`, `ProviderRoutes.cpp`, `ViewRoutes.cpp`, `ZoneRoutes.cpp`, `VariableRoutes.cpp`, `AuthRoutes.cpp`, `AuditRoutes.cpp`, `DeploymentRoutes.cpp`):

1. Add `#include "api/RouteHelpers.hpp"`
2. Remove the anonymous namespace containing duplicated `requireRole()`, `authenticate()`, `jsonResponse()`, `errorResponse()`
3. Qualify all calls: `dns::api::authenticate(...)`, `dns::api::requireRole(...)`, `dns::api::jsonResponse(...)`, `dns::api::errorResponse(...)`, `dns::api::invalidJsonResponse()`

### Step 4: Build and verify

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
Expected: Clean build, zero warnings

### Step 5: Run tests

Run: `build/tests/dns-tests`
Expected: Same 89 pass, 91 skip — no regressions

### Step 6: Commit

```bash
git add include/api/RouteHelpers.hpp src/api/RouteHelpers.cpp src/api/routes/*.cpp
git commit -m "refactor: extract common route helpers into RouteHelpers.hpp"
```

---

## Task 2: Add Security Response Headers (SEC-12)

**Files:**
- Modify: `src/api/RouteHelpers.cpp`
- Create: `tests/unit/test_route_helpers.cpp`

### Step 1: Write the failing tests

Create `tests/unit/test_route_helpers.cpp`:

```cpp
#include "api/RouteHelpers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace dns::api;

TEST(RouteHelpersTest, JsonResponseHasSecurityHeaders) {
  nlohmann::json j = {{"status", "ok"}};
  auto resp = jsonResponse(200, j);
  EXPECT_EQ(resp.code, 200);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
  EXPECT_EQ(resp.get_header_value("Referrer-Policy"), "strict-origin-when-cross-origin");
  EXPECT_EQ(resp.get_header_value("Content-Security-Policy"), "default-src 'self'");
}

TEST(RouteHelpersTest, ErrorResponseHasSecurityHeaders) {
  dns::common::ValidationError err("TEST_ERROR", "test message");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
}

TEST(RouteHelpersTest, InvalidJsonResponseHasSecurityHeaders) {
  auto resp = invalidJsonResponse();
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
}
```

### Step 2: Run tests to verify they fail

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="RouteHelpersTest.*"`
Expected: FAIL — security headers not set yet

### Step 3: Add security headers to all response helpers

Modify `src/api/RouteHelpers.cpp` — add `applySecurityHeaders()` and call it in `jsonResponse()`, `errorResponse()`, and `invalidJsonResponse()`:

```cpp
namespace {
void applySecurityHeaders(crow::response& resp) {
  resp.set_header("X-Content-Type-Options", "nosniff");
  resp.set_header("X-Frame-Options", "DENY");
  resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
  resp.set_header("Content-Security-Policy", "default-src 'self'");
}
}  // namespace
```

Add `applySecurityHeaders(resp);` before `return resp;` in all three response functions. Also add `resp.set_header("Content-Type", "application/json");` to `errorResponse()` and `invalidJsonResponse()`.

### Step 4: Run tests to verify they pass

Run: `build/tests/dns-tests --gtest_filter="RouteHelpersTest.*"`
Expected: 3 tests PASS

### Step 5: Commit

```bash
git add src/api/RouteHelpers.cpp tests/unit/test_route_helpers.cpp
git commit -m "feat: add security response headers (SEC-12)"
```

---

## Task 3: Input Validation Utility (SEC-11)

**Files:**
- Create: `include/api/RequestValidator.hpp`
- Create: `src/api/RequestValidator.cpp`
- Create: `tests/unit/test_request_validator.cpp`

### Step 1: Write the failing tests

Create `tests/unit/test_request_validator.cpp` with tests for:
- `validateStringLength()` — accepts within limit, accepts exact limit, rejects over limit, rejects empty
- `validateRequired()` — accepts non-empty, rejects empty
- `validateZoneName()` — accepts valid, rejects >253, rejects empty
- `validateRecordName()` — accepts valid, rejects >253
- `validateRecordType()` — accepts A/MX/TXT, rejects "INVALID"
- `validateTtl()` — accepts 1/300/604800, rejects 0/-1/604801
- `validateVariableName()` — accepts "LB_VIP"/"server01", rejects >64, rejects "invalid name!", rejects "invalid.name"
- `validateProviderType()` — accepts powerdns/cloudflare/digitalocean, rejects "invalid"
- `validateApiKeyDescription()` — accepts empty, accepts valid, rejects >512

Total: ~32 tests. See full test code in the `RequestValidator` test listing below.

```cpp
#include "api/RequestValidator.hpp"
#include <gtest/gtest.h>
using dns::api::RequestValidator;

TEST(RequestValidatorTest, StringLengthAcceptsWithinLimit) {
  EXPECT_NO_THROW(RequestValidator::validateStringLength("hello", "name", 253));
}
TEST(RequestValidatorTest, StringLengthRejectsOverLimit) {
  EXPECT_THROW(RequestValidator::validateStringLength(std::string(254, 'a'), "name", 253),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, StringLengthRejectsEmpty) {
  EXPECT_THROW(RequestValidator::validateStringLength("", "name", 253),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, RequiredAcceptsNonEmpty) {
  EXPECT_NO_THROW(RequestValidator::validateRequired("hello", "name"));
}
TEST(RequestValidatorTest, RequiredRejectsEmpty) {
  EXPECT_THROW(RequestValidator::validateRequired("", "name"),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, ZoneNameAcceptsValid) {
  EXPECT_NO_THROW(RequestValidator::validateZoneName("example.com"));
}
TEST(RequestValidatorTest, ZoneNameRejectsTooLong) {
  EXPECT_THROW(RequestValidator::validateZoneName(std::string(254, 'a')),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, RecordTypeAcceptsA) {
  EXPECT_NO_THROW(RequestValidator::validateRecordType("A"));
}
TEST(RequestValidatorTest, RecordTypeRejectsInvalid) {
  EXPECT_THROW(RequestValidator::validateRecordType("INVALID"),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, TtlAccepts300) {
  EXPECT_NO_THROW(RequestValidator::validateTtl(300));
}
TEST(RequestValidatorTest, TtlRejectsZero) {
  EXPECT_THROW(RequestValidator::validateTtl(0), dns::common::ValidationError);
}
TEST(RequestValidatorTest, TtlRejectsTooLarge) {
  EXPECT_THROW(RequestValidator::validateTtl(604801), dns::common::ValidationError);
}
TEST(RequestValidatorTest, VariableNameAcceptsValid) {
  EXPECT_NO_THROW(RequestValidator::validateVariableName("LB_VIP"));
}
TEST(RequestValidatorTest, VariableNameRejectsInvalidChars) {
  EXPECT_THROW(RequestValidator::validateVariableName("invalid name!"),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, ProviderTypeAcceptsPowerdns) {
  EXPECT_NO_THROW(RequestValidator::validateProviderType("powerdns"));
}
TEST(RequestValidatorTest, ProviderTypeRejectsInvalid) {
  EXPECT_THROW(RequestValidator::validateProviderType("invalid"),
               dns::common::ValidationError);
}
TEST(RequestValidatorTest, ApiKeyDescriptionAcceptsEmpty) {
  EXPECT_NO_THROW(RequestValidator::validateApiKeyDescription(""));
}
TEST(RequestValidatorTest, ApiKeyDescriptionRejectsTooLong) {
  EXPECT_THROW(RequestValidator::validateApiKeyDescription(std::string(513, 'a')),
               dns::common::ValidationError);
}
```

### Step 2: Run tests to verify they fail

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="RequestValidatorTest.*"`
Expected: FAIL — class doesn't exist

### Step 3: Create the header

Create `include/api/RequestValidator.hpp`:

```cpp
#pragma once
#include <cstddef>
#include <string>
#include "common/Errors.hpp"

namespace dns::api {

/// Centralized input validation (ARCHITECTURE.md §4.6.5).
/// All methods throw ValidationError on failure.
/// Class abbreviation: rv
class RequestValidator {
 public:
  static void validateStringLength(const std::string& sValue,
                                   const std::string& sFieldName, size_t nMaxLength);
  static void validateRequired(const std::string& sValue, const std::string& sFieldName);
  static void validateZoneName(const std::string& sName);
  static void validateRecordName(const std::string& sName);
  static void validateRecordType(const std::string& sType);
  static void validateValueTemplate(const std::string& sValue);
  static void validateTtl(int iTtl);
  static void validateVariableName(const std::string& sName);
  static void validateVariableValue(const std::string& sValue);
  static void validateProviderName(const std::string& sName);
  static void validateProviderType(const std::string& sType);
  static void validateUsername(const std::string& sUsername);
  static void validatePassword(const std::string& sPassword);
  static void validateApiKeyDescription(const std::string& sDescription);
  static void validateGroupName(const std::string& sName);
};

}  // namespace dns::api
```

### Step 4: Create the implementation

Create `src/api/RequestValidator.cpp`:

```cpp
#include "api/RequestValidator.hpp"
#include <regex>
#include <unordered_set>

namespace dns::api {

void RequestValidator::validateStringLength(const std::string& sValue,
                                            const std::string& sFieldName,
                                            size_t nMaxLength) {
  if (sValue.empty())
    throw common::ValidationError("FIELD_REQUIRED", sFieldName + " is required");
  if (sValue.size() > nMaxLength)
    throw common::ValidationError("FIELD_TOO_LONG",
        sFieldName + " exceeds maximum length of " + std::to_string(nMaxLength));
}

void RequestValidator::validateRequired(const std::string& sValue,
                                        const std::string& sFieldName) {
  if (sValue.empty())
    throw common::ValidationError("FIELD_REQUIRED", sFieldName + " is required");
}

void RequestValidator::validateZoneName(const std::string& s) { validateStringLength(s, "zone_name", 253); }
void RequestValidator::validateRecordName(const std::string& s) { validateStringLength(s, "record_name", 253); }
void RequestValidator::validateValueTemplate(const std::string& s) { validateStringLength(s, "value_template", 4096); }
void RequestValidator::validateVariableValue(const std::string& s) { validateStringLength(s, "variable_value", 4096); }
void RequestValidator::validateProviderName(const std::string& s) { validateStringLength(s, "provider_name", 128); }
void RequestValidator::validateUsername(const std::string& s) { validateStringLength(s, "username", 128); }
void RequestValidator::validatePassword(const std::string& s) { validateStringLength(s, "password", 1024); }
void RequestValidator::validateGroupName(const std::string& s) { validateStringLength(s, "group_name", 128); }

void RequestValidator::validateRecordType(const std::string& sType) {
  static const std::unordered_set<std::string> st = {"A","AAAA","CNAME","MX","TXT","SRV","NS","PTR"};
  if (st.find(sType) == st.end())
    throw common::ValidationError("INVALID_RECORD_TYPE",
        "Record type must be one of: A, AAAA, CNAME, MX, TXT, SRV, NS, PTR");
}

void RequestValidator::validateTtl(int iTtl) {
  if (iTtl < 1 || iTtl > 604800)
    throw common::ValidationError("INVALID_TTL", "TTL must be between 1 and 604800 seconds");
}

void RequestValidator::validateVariableName(const std::string& sName) {
  validateStringLength(sName, "variable_name", 64);
  static const std::regex rx("^[A-Za-z0-9_]+$");
  if (!std::regex_match(sName, rx))
    throw common::ValidationError("INVALID_VARIABLE_NAME",
        "Variable name must contain only alphanumeric characters and underscores");
}

void RequestValidator::validateProviderType(const std::string& sType) {
  static const std::unordered_set<std::string> st = {"powerdns","cloudflare","digitalocean"};
  if (st.find(sType) == st.end())
    throw common::ValidationError("INVALID_PROVIDER_TYPE",
        "Provider type must be one of: powerdns, cloudflare, digitalocean");
}

void RequestValidator::validateApiKeyDescription(const std::string& s) {
  if (!s.empty() && s.size() > 512)
    throw common::ValidationError("FIELD_TOO_LONG",
        "api_key_description exceeds maximum length of 512");
}

}  // namespace dns::api
```

### Step 5: Run tests, verify pass, commit

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="RequestValidatorTest.*"`
Expected: All tests PASS

```bash
git add include/api/RequestValidator.hpp src/api/RequestValidator.cpp tests/unit/test_request_validator.cpp
git commit -m "feat: add RequestValidator for input validation (SEC-11)"
```

---

## Task 4: Wire Input Validation into Route Handlers

**Files:**
- Modify: `src/api/routes/RecordRoutes.cpp`
- Modify: `src/api/routes/ProviderRoutes.cpp`
- Modify: `src/api/routes/ZoneRoutes.cpp`
- Modify: `src/api/routes/VariableRoutes.cpp`
- Modify: `src/api/routes/AuthRoutes.cpp`

### Step 1: Add validation to each route file

Add `#include "api/RequestValidator.hpp"` to each file. Replace existing empty-field checks with validator calls.

**RecordRoutes** (POST + PUT handlers):
```cpp
RequestValidator::validateRecordName(sName);
RequestValidator::validateRecordType(sType);
RequestValidator::validateTtl(iTtl);
RequestValidator::validateValueTemplate(sValueTemplate);
```

**ProviderRoutes** (POST + PUT):
```cpp
RequestValidator::validateProviderName(sName);
RequestValidator::validateProviderType(sType);
RequestValidator::validateRequired(sApiEndpoint, "api_endpoint");
RequestValidator::validateRequired(sToken, "token");
```

**ZoneRoutes** (POST + PUT):
```cpp
RequestValidator::validateZoneName(sName);
```

**VariableRoutes** (POST + PUT):
```cpp
RequestValidator::validateVariableName(sName);
RequestValidator::validateVariableValue(sValue);
```

**AuthRoutes** (login handler):
```cpp
RequestValidator::validateUsername(sUsername);
RequestValidator::validatePassword(sPassword);
```

### Step 2: Build and run tests

Run: `cmake --build build --parallel && build/tests/dns-tests`
Expected: All existing tests pass

### Step 3: Commit

```bash
git add src/api/routes/*.cpp
git commit -m "feat: wire input validation into all route handlers (SEC-11)"
```

---

## Task 5: In-Process Rate Limiter for Auth Endpoints

**Files:**
- Create: `include/api/RateLimiter.hpp`
- Create: `src/api/RateLimiter.cpp`
- Create: `tests/unit/test_rate_limiter.cpp`
- Modify: `include/common/Errors.hpp` — add `RateLimitedError`
- Modify: `src/api/routes/AuthRoutes.cpp` — apply rate limiter

### Step 1: Write the failing tests

Create `tests/unit/test_rate_limiter.cpp`:

```cpp
#include "api/RateLimiter.hpp"
#include <gtest/gtest.h>
#include <thread>
using dns::api::RateLimiter;

TEST(RateLimiterTest, AllowsWithinLimit) {
  RateLimiter rl(5, std::chrono::seconds(60));
  for (int i = 0; i < 5; ++i) EXPECT_TRUE(rl.allow("192.168.1.1"));
}

TEST(RateLimiterTest, BlocksOverLimit) {
  RateLimiter rl(3, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("ip")); EXPECT_TRUE(rl.allow("ip")); EXPECT_TRUE(rl.allow("ip"));
  EXPECT_FALSE(rl.allow("ip"));
}

TEST(RateLimiterTest, DifferentKeysIndependent) {
  RateLimiter rl(2, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("a")); EXPECT_TRUE(rl.allow("a")); EXPECT_FALSE(rl.allow("a"));
  EXPECT_TRUE(rl.allow("b")); EXPECT_TRUE(rl.allow("b")); EXPECT_FALSE(rl.allow("b"));
}

TEST(RateLimiterTest, TokensRefillAfterWindow) {
  RateLimiter rl(2, std::chrono::milliseconds(200));
  EXPECT_TRUE(rl.allow("k")); EXPECT_TRUE(rl.allow("k")); EXPECT_FALSE(rl.allow("k"));
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  EXPECT_TRUE(rl.allow("k"));
}
```

### Step 2: Run tests to verify they fail

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="RateLimiterTest.*"`
Expected: FAIL

### Step 3: Create header and implementation

Create `include/api/RateLimiter.hpp`:

```cpp
#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dns::api {

/// Token-bucket rate limiter keyed by client identifier (IP address).
/// Thread-safe. Class abbreviation: rl
class RateLimiter {
 public:
  RateLimiter(int iMaxRequests, std::chrono::steady_clock::duration durWindow);
  bool allow(const std::string& sKey);
 private:
  struct Bucket { int iTokens; std::chrono::steady_clock::time_point tpLastRefill; };
  void evictStale();
  int _iMaxRequests;
  std::chrono::steady_clock::duration _durWindow;
  std::unordered_map<std::string, Bucket> _mBuckets;
  std::mutex _mtx;
};

}  // namespace dns::api
```

Create `src/api/RateLimiter.cpp`:

```cpp
#include "api/RateLimiter.hpp"

namespace dns::api {

RateLimiter::RateLimiter(int iMaxRequests, std::chrono::steady_clock::duration durWindow)
    : _iMaxRequests(iMaxRequests), _durWindow(durWindow) {}

bool RateLimiter::allow(const std::string& sKey) {
  std::lock_guard<std::mutex> lock(_mtx);
  auto tpNow = std::chrono::steady_clock::now();
  auto it = _mBuckets.find(sKey);
  if (it == _mBuckets.end()) {
    _mBuckets[sKey] = Bucket{_iMaxRequests - 1, tpNow};
    if (_mBuckets.size() % 100 == 0) evictStale();
    return true;
  }
  auto& bucket = it->second;
  if (tpNow - bucket.tpLastRefill >= _durWindow) {
    bucket.iTokens = _iMaxRequests;
    bucket.tpLastRefill = tpNow;
  }
  if (bucket.iTokens > 0) { --bucket.iTokens; return true; }
  return false;
}

void RateLimiter::evictStale() {
  auto tpNow = std::chrono::steady_clock::now();
  for (auto it = _mBuckets.begin(); it != _mBuckets.end();)
    it = (tpNow - it->second.tpLastRefill > _durWindow * 2) ? _mBuckets.erase(it) : ++it;
}

}  // namespace dns::api
```

### Step 4: Add RateLimitedError

Add to `include/common/Errors.hpp`:

```cpp
/// 429 Too Many Requests — rate limit exceeded.
struct RateLimitedError : AppError {
  explicit RateLimitedError(std::string sCode, std::string sMsg)
      : AppError(429, std::move(sCode), std::move(sMsg)) {}
};
```

### Step 5: Wire into AuthRoutes

In `src/api/routes/AuthRoutes.cpp`, add a static rate limiter and check before login:

```cpp
#include "api/RateLimiter.hpp"

// At file scope:
static dns::api::RateLimiter g_rlLogin(5, std::chrono::seconds(60));

// In POST /auth/local/login handler, before parsing body:
std::string sClientIp = req.get_header_value("X-Forwarded-For");
if (sClientIp.empty()) sClientIp = req.remote_ip_address;
if (!g_rlLogin.allow(sClientIp))
  throw common::RateLimitedError("RATE_LIMITED", "Too many login attempts. Try again later.");
```

### Step 6: Run tests, verify pass, commit

Run: `cmake --build build --parallel && build/tests/dns-tests`
Expected: All tests pass (4 new rate limiter tests)

```bash
git add include/api/RateLimiter.hpp src/api/RateLimiter.cpp tests/unit/test_rate_limiter.cpp \
        include/common/Errors.hpp src/api/routes/AuthRoutes.cpp
git commit -m "feat: add in-process rate limiter for auth endpoints"
```

---

## Task 6: Dockerfile (Multi-Stage Build)

**Files:**
- Create: `Dockerfile`
- Create: `.dockerignore`

### Step 1: Create the Dockerfile

Create `Dockerfile` following ARCHITECTURE.md §11.1:

```dockerfile
# ── Stage 1: Build ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
  cmake ninja-build g++ \
  libpqxx-dev libssl-dev libgit2-dev \
  nlohmann-json3-dev libspdlog-dev \
  git ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --parallel

# ── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
  libpq5 libssl3 libgit2-1.5 libspdlog1.10 \
  && rm -rf /var/lib/apt/lists/*

RUN useradd --system --no-create-home dns-orchestrator

COPY --from=builder /build/build/dns-orchestrator /usr/local/bin/dns-orchestrator
COPY scripts/db/ /opt/dns-orchestrator/db/
COPY scripts/docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER dns-orchestrator
EXPOSE 8080

ENTRYPOINT ["/entrypoint.sh"]
CMD ["dns-orchestrator"]
```

### Step 2: Create .dockerignore

Create `.dockerignore`:

```
build/
.git/
.roo/
tasks/
*.md
!README.md
```

### Step 3: Test Docker build (if Docker available)

Run: `docker build -t dns-orchestrator:dev .`
Expected: Successful multi-stage build

### Step 4: Commit

```bash
git add Dockerfile .dockerignore
git commit -m "feat: add multi-stage Dockerfile"
```

---

## Task 7: Docker Compose (PostgreSQL 16 + PowerDNS + App)

**Files:**
- Create: `docker-compose.yml`
- Create: `.env.example`
- Modify: `.gitignore` — ensure `.env` is ignored

### Step 1: Create docker-compose.yml

Create `docker-compose.yml`:

```yaml
services:
  db:
    image: postgres:16-alpine
    environment:
      POSTGRES_DB: dns_orchestrator
      POSTGRES_USER: dns
      POSTGRES_PASSWORD: ${DNS_DB_PASSWORD:-dns_dev_password}
    volumes:
      - pgdata:/var/lib/postgresql/data
      - ./scripts/db:/docker-entrypoint-initdb.d:ro
    ports:
      - "${DNS_DB_PORT:-5432}:5432"
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U dns -d dns_orchestrator"]
      interval: 5s
      timeout: 3s
      retries: 5

  powerdns:
    image: powerdns/pdns-auth-49:latest
    environment:
      PDNS_AUTH_API_KEY: ${PDNS_API_KEY:-pdns_dev_key}
    ports:
      - "${PDNS_HTTP_PORT:-8081}:8081"
      - "${PDNS_DNS_PORT:-5353}:53/udp"
      - "${PDNS_DNS_PORT:-5353}:53/tcp"
    depends_on:
      db:
        condition: service_healthy

  app:
    build: .
    depends_on:
      db:
        condition: service_healthy
    environment:
      DNS_DB_URL: postgresql://dns:${DNS_DB_PASSWORD:-dns_dev_password}@db:5432/dns_orchestrator
      DNS_MASTER_KEY: ${DNS_MASTER_KEY}
      DNS_JWT_SECRET: ${DNS_JWT_SECRET}
      DNS_HTTP_PORT: "8080"
      DNS_AUDIT_STDOUT: "true"
      DNS_LOG_LEVEL: "${DNS_LOG_LEVEL:-info}"
    ports:
      - "${DNS_HTTP_PORT:-8080}:8080"
    volumes:
      - gitrepo:/var/dns-orchestrator/repo

volumes:
  pgdata:
  gitrepo:
```

### Step 2: Create .env.example

Create `.env.example`:

```bash
# DNS Orchestrator — Environment Variables
# Copy to .env and fill in values: cp .env.example .env

# Required secrets (generate with: openssl rand -hex 32)
DNS_MASTER_KEY=
DNS_JWT_SECRET=

# Database
DNS_DB_PASSWORD=dns_dev_password

# PowerDNS
PDNS_API_KEY=pdns_dev_key

# Optional overrides
# DNS_HTTP_PORT=8080
# DNS_DB_PORT=5432
# PDNS_HTTP_PORT=8081
# DNS_LOG_LEVEL=info
```

### Step 3: Ensure .env is in .gitignore

Check `.gitignore` for `.env` entry. Add if missing.

### Step 4: Commit

```bash
git add docker-compose.yml .env.example .gitignore
git commit -m "feat: add docker-compose.yml with PostgreSQL 16 + PowerDNS"
```

---

## Task 8: OpenAPI Specification

**Files:**
- Create: `docs/openapi.yaml`

### Step 1: Write the OpenAPI spec

Create `docs/openapi.yaml` documenting every endpoint from ARCHITECTURE.md §6. The spec must include:

- **Info block:** title "DNS Orchestrator API", version 0.1.0
- **Security schemes:** `bearerAuth` (JWT) and `apiKeyAuth` (X-API-Key header)
- **Component schemas:** Error, Provider, ProviderCreate, View, ViewCreate, Zone, ZoneCreate, Record, RecordCreate, Variable, VariableCreate, PreviewResult, RecordDiff, LoginRequest, LoginResponse, HealthResponse, DeploymentSnapshot, RollbackRequest, AuditEntry
- **All paths from §6.1–§6.10:**
  - `/health` — GET (no auth)
  - `/auth/local/login` — POST (no auth, rate limited)
  - `/auth/local/logout` — POST
  - `/auth/me` — GET
  - `/providers` — GET, POST
  - `/providers/{id}` — GET, PUT, DELETE
  - `/views` — GET, POST
  - `/views/{id}` — GET, PUT, DELETE
  - `/views/{id}/providers/{pid}` — POST, DELETE
  - `/zones` — GET (with `?view_id=` filter), POST
  - `/zones/{id}` — GET, PUT, DELETE
  - `/zones/{id}/records` — GET, POST
  - `/zones/{id}/records/{rid}` — GET, PUT, DELETE
  - `/zones/{id}/preview` — POST
  - `/zones/{id}/push` — POST
  - `/variables` — GET (with `?scope=` and `?zone_id=` filters), POST
  - `/variables/{id}` — GET, PUT, DELETE
  - `/zones/{id}/deployments` — GET
  - `/zones/{id}/deployments/{did}` — GET
  - `/zones/{id}/deployments/{did}/diff` — GET
  - `/zones/{id}/deployments/{did}/rollback` — POST
  - `/audit` — GET (with `?entity_type=`, `?identity=`, `?from=`, `?to=` filters)
  - `/audit/export` — GET (NDJSON stream)
  - `/audit/purge` — DELETE

Each path must document: HTTP method, summary, tags, required role, request body schema (if applicable), response schemas, and error responses (400, 401, 403, 404, 409, 422, 429, 502).

Use `$ref` to reference component schemas. Follow the field constraints from the `RequestValidator` (maxLength, enum values, min/max for TTL).

### Step 2: Validate the spec

Run: `npx @redocly/cli lint docs/openapi.yaml` (if Node.js available)
Or manually verify YAML syntax: `python3 -c "import yaml; yaml.safe_load(open('docs/openapi.yaml'))"`

### Step 3: Commit

```bash
git add docs/openapi.yaml
git commit -m "docs: add OpenAPI 3.1 specification for full API surface"
```

---

## Task 9: Full API Integration Test Suite

**Files:**
- Create: `tests/integration/test_api_validation.cpp`

This task adds integration tests that verify the validation and security header behavior end-to-end through the route handlers. These tests use the existing test patterns (no DB required — they test validation rejection before any DB call).

### Step 1: Write the tests

Create `tests/integration/test_api_validation.cpp`:

```cpp
#include "api/RequestValidator.hpp"
#include "api/RateLimiter.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace dns::api;
using namespace dns::common;

// ── Validation rejection tests ──────────────────────────────────────────────

TEST(ApiValidationTest, RecordNameTooLongReturns400) {
  std::string sLong(254, 'a');
  EXPECT_THROW(RequestValidator::validateRecordName(sLong), ValidationError);
}

TEST(ApiValidationTest, InvalidRecordTypeReturns400) {
  EXPECT_THROW(RequestValidator::validateRecordType("BOGUS"), ValidationError);
}

TEST(ApiValidationTest, NegativeTtlReturns400) {
  EXPECT_THROW(RequestValidator::validateTtl(-5), ValidationError);
}

TEST(ApiValidationTest, VariableNameWithSpacesReturns400) {
  EXPECT_THROW(RequestValidator::validateVariableName("has spaces"), ValidationError);
}

TEST(ApiValidationTest, ProviderNameTooLongReturns400) {
  std::string sLong(129, 'x');
  EXPECT_THROW(RequestValidator::validateProviderName(sLong), ValidationError);
}

TEST(ApiValidationTest, InvalidProviderTypeReturns400) {
  EXPECT_THROW(RequestValidator::validateProviderType("route53"), ValidationError);
}

TEST(ApiValidationTest, EmptyUsernameReturns400) {
  EXPECT_THROW(RequestValidator::validateUsername(""), ValidationError);
}

TEST(ApiValidationTest, PasswordTooLongReturns400) {
  std::string sLong(1025, 'p');
  EXPECT_THROW(RequestValidator::validatePassword(sLong), ValidationError);
}

// ── Security header tests ───────────────────────────────────────────────────

TEST(ApiValidationTest, AllResponsesHaveSecurityHeaders) {
  auto resp = jsonResponse(200, {{"ok", true}});
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
  EXPECT_EQ(resp.get_header_value("Referrer-Policy"), "strict-origin-when-cross-origin");
  EXPECT_EQ(resp.get_header_value("Content-Security-Policy"), "default-src 'self'");
}

TEST(ApiValidationTest, ErrorResponsesHaveSecurityHeaders) {
  ValidationError err("TEST", "test");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
}

// ── Rate limiter integration ────────────────────────────────────────────────

TEST(ApiValidationTest, RateLimiterBlocksAfterThreshold) {
  RateLimiter rl(3, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_FALSE(rl.allow("test_ip"));
}

// ── Error response format ───────────────────────────────────────────────────

TEST(ApiValidationTest, ErrorResponseHasCorrectJsonShape) {
  ValidationError err("FIELD_REQUIRED", "name is required");
  auto resp = errorResponse(err);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["error"], "FIELD_REQUIRED");
  EXPECT_EQ(j["message"], "name is required");
  EXPECT_EQ(resp.code, 400);
}

TEST(ApiValidationTest, InvalidJsonResponseHasCorrectShape) {
  auto resp = invalidJsonResponse();
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["error"], "invalid_json");
  EXPECT_EQ(resp.code, 400);
}
```

### Step 2: Run tests

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="ApiValidationTest.*"`
Expected: All 14 tests PASS

### Step 3: Run full test suite

Run: `build/tests/dns-tests`
Expected: All tests pass (new tests added on top of existing)

### Step 4: Commit

```bash
git add tests/integration/test_api_validation.cpp
git commit -m "test: add API validation integration test suite"
```

---

## Task 10: Update CLAUDE.md and Documentation

**Files:**
- Modify: `CLAUDE.md` — update Phase 8 status to complete, update test counts
- Modify: `docs/ARCHITECTURE.md` — add note about in-process rate limiter

### Step 1: Update CLAUDE.md

Update the project status section:
- Change `Phase 8` from "Next task" to "complete"
- Update test count
- Add Phase 8 deliverables summary
- Set "Next task" to Phase 9

### Step 2: Commit

```bash
git add CLAUDE.md docs/ARCHITECTURE.md
git commit -m "docs: update CLAUDE.md for Phase 8 completion"
```