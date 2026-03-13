# Workstream 4: OIDC & SAML Authentication — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Federated login via OIDC and SAML identity providers with auto-provisioning, IdP group-to-Meridian group mapping, and a diagnostic claim/attribute test flow.

**Architecture:** New `identity_providers` table stores IdP configuration with encrypted secrets. Two new auth flows (OIDC Authorization Code + PKCE, SAML POST binding) are added alongside existing local auth. An `IdpRepository` handles DB persistence, an `OidcService` handles OIDC discovery/token exchange/JWT validation, a `SamlService` handles AuthnRequest generation and assertion validation. Both services share a `FederatedAuthService` that handles user provisioning and group mapping. The login page dynamically shows federated login buttons when IdPs are enabled.

**Tech Stack:** C++20, Crow HTTP, OpenSSL (RSA/EC signature verification, XML parsing), cpp-httplib (OIDC HTTP calls), nlohmann/json, libpqxx, Vue 3 + TypeScript + PrimeVue

**Dependencies:** Workstreams 2 (config in DB) and 3 (permissions restructure) must be complete.

---

## Table of Contents

1. [Task 1: Database Migration](#task-1-database-migration)
2. [Task 2: IdpRepository](#task-2-idprepository)
3. [Task 3: UserRepository Extensions](#task-3-userrepository-extensions)
4. [Task 4: OidcService — Discovery & PKCE](#task-4-oidcservice--discovery--pkce)
5. [Task 5: OidcService — Token Exchange & JWT Validation](#task-5-oidcservice--token-exchange--jwt-validation)
6. [Task 6: SamlService — AuthnRequest Generation](#task-6-samlservice--authnrequest-generation)
7. [Task 7: SamlService — Assertion Validation](#task-7-samlservice--assertion-validation)
8. [Task 8: FederatedAuthService — User Provisioning & Group Mapping](#task-8-federatedauthservice--user-provisioning--group-mapping)
9. [Task 9: OIDC Auth Routes](#task-9-oidc-auth-routes)
10. [Task 10: SAML Auth Routes](#task-10-saml-auth-routes)
11. [Task 11: IdP Admin CRUD Routes](#task-11-idp-admin-crud-routes)
12. [Task 12: IdP Test Diagnostic Route](#task-12-idp-test-diagnostic-route)
13. [Task 13: Startup Wiring](#task-13-startup-wiring)
14. [Task 14: Frontend — API Client & Types](#task-14-frontend--api-client--types)
15. [Task 15: Frontend — Identity Providers Admin View](#task-15-frontend--identity-providers-admin-view)
16. [Task 16: Frontend — Login Page Federated Buttons](#task-16-frontend--login-page-federated-buttons)
17. [Task 17: Frontend — OIDC/SAML Callback Handler](#task-17-frontend--oidcsaml-callback-handler)
18. [Task 18: Integration Tests](#task-18-integration-tests)

---

## Task 1: Database Migration

**Files:**
- Create: `scripts/db/v009/001_identity_providers.sql`

**Context:** The `identity_providers` table stores OIDC and SAML IdP configurations. The `config` JSONB column holds type-specific settings (issuer_url, client_id, etc. for OIDC; entity_id, sso_url, certificate for SAML). The `encrypted_secret` column stores the OIDC client_secret encrypted via `CryptoService`. The `group_mappings` JSONB column stores IdP-group-to-Meridian-group mapping rules. See the existing `providers` table in `scripts/db/v001/001_initial_schema.sql` for the encryption pattern used by `ProviderRepository`.

**Step 1: Write the migration SQL**

```sql
-- Workstream 4: OIDC & SAML identity provider configuration

CREATE TABLE identity_providers (
  id               SERIAL PRIMARY KEY,
  name             VARCHAR(100) UNIQUE NOT NULL,
  type             VARCHAR(10) NOT NULL CHECK (type IN ('oidc', 'saml')),
  is_enabled       BOOLEAN NOT NULL DEFAULT true,
  config           JSONB NOT NULL DEFAULT '{}',
  encrypted_secret TEXT,
  group_mappings   JSONB,
  default_group_id INTEGER REFERENCES groups(id) ON DELETE SET NULL,
  created_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_identity_providers_type ON identity_providers (type) WHERE is_enabled = true;
```

**Step 2: Verify migration file exists**

Run: `cat scripts/db/v009/001_identity_providers.sql`
Expected: The SQL content above.

**Step 3: Commit**

```bash
git add scripts/db/v009/001_identity_providers.sql
git commit -m "feat(ws4): add identity_providers table migration v009"
```

---

## Task 2: IdpRepository

**Files:**
- Create: `include/dal/IdpRepository.hpp`
- Create: `src/dal/IdpRepository.cpp`
- Test: `tests/integration/test_idp_repository.cpp`

**Context:** Follow the same pattern as `src/dal/ProviderRepository.cpp` — constructor takes `ConnectionPool&` and `const CryptoService&`. The `encrypted_secret` column is encrypted/decrypted using `CryptoService::encrypt()`/`decrypt()`. The `config` and `group_mappings` columns are stored as JSONB text. The repository never returns the raw secret in list operations — only in `findById()` for admin editing.

**Step 1: Write the failing test**

Create `tests/integration/test_idp_repository.cpp` with tests for:
- `CreateAndFindOidc` — create an OIDC IdP, find by ID, verify all fields including decrypted secret
- `ListEnabledDoesNotExposeSecret` — list enabled IdPs, verify `sDecryptedSecret` is empty
- `UpdateIdp` — update name, config, and secret; verify changes persisted
- `DeleteIdp` — delete an IdP, verify `findById` returns nullopt

Test fixture: `IdpRepositoryTest` with `SetUp()` that checks `DNS_DB_URL` and `DNS_MASTER_KEY` env vars, creates `ConnectionPool`, `CryptoService`, and `IdpRepository`. Cleans up test data with `DELETE FROM identity_providers WHERE name LIKE 'test-%'`.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="IdpRepository*" -v`
Expected: Compilation fails — `IdpRepository.hpp` not found.

**Step 3: Write the header**

Create `include/dal/IdpRepository.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::security { class CryptoService; }

namespace dns::dal {

class ConnectionPool;

/// Row returned from identity_providers table.
struct IdpRow {
  int64_t iId = 0;
  std::string sName;
  std::string sType;             // "oidc" or "saml"
  bool bIsEnabled = true;
  nlohmann::json jConfig;        // Type-specific configuration
  std::string sDecryptedSecret;  // Only populated by findById(), empty in list
  nlohmann::json jGroupMappings;
  int64_t iDefaultGroupId = 0;
  std::string sCreatedAt;
  std::string sUpdatedAt;
};

/// CRUD for identity_providers table.
/// Class abbreviation: ir
class IdpRepository {
 public:
  IdpRepository(ConnectionPool& cpPool, const dns::security::CryptoService& csService);
  ~IdpRepository();

  int64_t create(const std::string& sName, const std::string& sType,
                 const nlohmann::json& jConfig, const std::string& sPlaintextSecret,
                 const nlohmann::json& jGroupMappings, int64_t iDefaultGroupId);

  std::optional<IdpRow> findById(int64_t iId);
  std::vector<IdpRow> listAll();
  std::vector<IdpRow> listEnabled();

  void update(int64_t iId, const std::string& sName, bool bIsEnabled,
              const nlohmann::json& jConfig, const std::string& sPlaintextSecret,
              const nlohmann::json& jGroupMappings, int64_t iDefaultGroupId);

  void deleteIdp(int64_t iId);

 private:
  IdpRow mapRow(const auto& row, bool bDecryptSecret = false) const;

  ConnectionPool& _cpPool;
  const dns::security::CryptoService& _csService;
};

}  // namespace dns::dal
```

**Step 4: Write the implementation**

Create `src/dal/IdpRepository.cpp`. Key patterns:
- `create()`: encrypt secret with `_csService.encrypt()`, INSERT with `$3::jsonb` cast for config, `NULLIF($6, 0)` for default_group_id
- `findById()`: SELECT all columns, call `mapRow(row, true)` to decrypt secret
- `listAll()` / `listEnabled()`: SELECT with `'' AS encrypted_secret` (skip decryption), call `mapRow(row, false)`
- `update()`: if `sPlaintextSecret` is empty, don't update `encrypted_secret` column (user didn't change it)
- `deleteIdp()`: DELETE, throw `NotFoundError` if `affected_rows() == 0`
- `mapRow()`: parse `config` and `group_mappings` from JSON text, conditionally decrypt secret

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="IdpRepository*" -v`
Expected: All 4 tests PASS (or SKIP if no DNS_DB_URL).

**Step 6: Commit**

```bash
git add include/dal/IdpRepository.hpp src/dal/IdpRepository.cpp tests/integration/test_idp_repository.cpp
git commit -m "feat(ws4): add IdpRepository with CRUD and encrypted secret storage"
```

---

## Task 3: UserRepository Extensions

**Files:**
- Modify: `include/dal/UserRepository.hpp`
- Modify: `src/dal/UserRepository.cpp`
- Test: `tests/integration/test_idp_repository.cpp` (add federated user tests)

**Context:** The `users` table already has `oidc_sub` and `saml_name_id` columns (see `scripts/db/v001/001_initial_schema.sql:72-73`). The `auth_method` enum already includes `'oidc'` and `'saml'` (line 11). We need new methods to find users by their federated identifiers and to create federated users (no password hash).

**Step 1: Write the failing tests**

Add to `tests/integration/test_idp_repository.cpp`:
- `FindByOidcSub` — insert a user with `oidc_sub` via SQL, find via `findByOidcSub()`, verify username and auth_method
- `FindBySamlNameId` — insert a user with `saml_name_id` via SQL, find via `findBySamlNameId()`, verify
- `CreateFederatedUser` — call `createFederated()`, verify user created with correct auth_method and empty password_hash

**Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="IdpRepository*FindBy*" -v`
Expected: Compilation fails — `findByOidcSub` not found.

**Step 3: Add new methods to UserRepository header**

Add to `include/dal/UserRepository.hpp`:

```cpp
  std::optional<UserRow> findByOidcSub(const std::string& sOidcSub);
  std::optional<UserRow> findBySamlNameId(const std::string& sSamlNameId);
  int64_t createFederated(const std::string& sUsername, const std::string& sEmail,
                          const std::string& sAuthMethod,
                          const std::string& sOidcSub, const std::string& sSamlNameId);
  void updateFederatedEmail(int64_t iUserId, const std::string& sEmail);
```

**Step 4: Implement the new methods**

Add to `src/dal/UserRepository.cpp`:
- `findByOidcSub()`: SELECT WHERE `oidc_sub = $1`, same column mapping as `findByUsername()`
- `findBySamlNameId()`: SELECT WHERE `saml_name_id = $1`
- `createFederated()`: INSERT with `auth_method = $3::auth_method`, `NULLIF($4, '')` for oidc_sub, `NULLIF($5, '')` for saml_name_id, no password_hash
- `updateFederatedEmail()`: UPDATE email and updated_at WHERE id = $1

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="IdpRepository*" -v`
Expected: All tests PASS (or SKIP).

**Step 6: Commit**

```bash
git add include/dal/UserRepository.hpp src/dal/UserRepository.cpp tests/integration/test_idp_repository.cpp
git commit -m "feat(ws4): add federated user lookup/creation to UserRepository"
```

---

## Task 4: OidcService — Discovery & PKCE

**Files:**
- Create: `include/security/OidcService.hpp`
- Create: `src/security/OidcService.cpp`
- Test: `tests/unit/test_oidc_service.cpp`

**Context:** The OIDC service handles OpenID Connect protocol operations. It uses cpp-httplib (already linked — see `src/CMakeLists.txt:27`) for HTTP calls to the IdP. Discovery fetches `/.well-known/openid-configuration` to get the authorization endpoint, token endpoint, JWKS URI, and issuer. PKCE generates a `code_verifier` (random 32-byte base64url string) and `code_challenge` (SHA-256 hash of verifier, base64url-encoded). State is a random string to prevent CSRF. Both state and code_verifier are stored in a short-lived in-memory cache keyed by state (similar pattern to `SamlReplayCache` in `src/security/SamlReplayCache.cpp`).

**Step 1: Write the failing test**

Create `tests/unit/test_oidc_service.cpp` with tests for:
- `GeneratePkceChallenge` — verify verifier is ~43 chars, challenge is ~43 chars, they differ
- `GenerateState` — verify two calls produce different values, length >= 20
- `StoreAndRetrieveAuthState` — store state, consume it, verify fields match, second consume returns nullopt
- `BuildAuthorizationUrl` — verify URL contains `response_type=code`, `client_id`, `state`, `code_challenge`, `code_challenge_method=S256`

**Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="OidcService*" -v`
Expected: Compilation fails — `OidcService.hpp` not found.

**Step 3: Write the header**

Create `include/security/OidcService.hpp` with:

```cpp
struct OidcAuthState {
  std::string sCodeVerifier;
  int64_t iIdpId = 0;
  bool bIsTestMode = false;
  std::chrono::system_clock::time_point tpCreatedAt;
};

struct OidcDiscovery {
  std::string sAuthorizationEndpoint;
  std::string sTokenEndpoint;
  std::string sJwksUri;
  std::string sIssuer;
  std::chrono::system_clock::time_point tpFetchedAt;
};

class OidcService {
 public:
  OidcService();
  ~OidcService();

  static std::pair<std::string, std::string> generatePkce();
  static std::string generateState();
  static std::string buildAuthorizationUrl(
      const std::string& sAuthEndpoint, const std::string& sClientId,
      const std::string& sRedirectUri, const std::string& sScope,
      const std::string& sState, const std::string& sCodeChallenge);

  void storeAuthState(const std::string& sState, OidcAuthState oaState);
  std::optional<OidcAuthState> consumeAuthState(const std::string& sState);

  OidcDiscovery discover(const std::string& sIssuerUrl);
  nlohmann::json exchangeCode(/* token endpoint, code, client_id, secret, redirect_uri, verifier */);
  nlohmann::json validateIdToken(/* id_token, jwks_uri, expected_issuer, expected_audience */);

 private:
  void evictExpiredStates();
  std::mutex _mtxStates;
  std::unordered_map<std::string, OidcAuthState> _mAuthStates;
  std::mutex _mtxDiscovery;
  std::unordered_map<std::string, OidcDiscovery> _mDiscoveryCache;
};
```

**Step 4: Write the implementation**

Create `src/security/OidcService.cpp`:
- `generatePkce()`: use `RAND_bytes(32)` → `CryptoService::base64UrlEncode()` for verifier; SHA-256 hash of verifier → base64url for challenge
- `generateState()`: `RAND_bytes(24)` → `CryptoService::base64UrlEncode()`
- `buildAuthorizationUrl()`: concatenate query params with URL encoding
- `storeAuthState()` / `consumeAuthState()`: mutex-protected map with TTL eviction (10 min), same pattern as `SamlReplayCache`
- `discover()`: `httplib::Client` GET to `{issuer}/.well-known/openid-configuration`, parse JSON, cache for 1 hour
- `exchangeCode()`: `httplib::Client` POST to token endpoint with form params (`grant_type=authorization_code`, code, client_id, client_secret, redirect_uri, code_verifier)
- `validateIdToken()`: split JWT, decode header for `kid`/`alg`, fetch JWKS, find matching key, verify signature with OpenSSL `EVP_DigestVerify*`, decode payload, validate `iss`, `aud`, `exp` claims

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="OidcService*" -v`
Expected: All 4 unit tests PASS.

**Step 6: Commit**

```bash
git add include/security/OidcService.hpp src/security/OidcService.cpp tests/unit/test_oidc_service.cpp
git commit -m "feat(ws4): add OidcService with discovery, PKCE, state management"
```

---

## Task 5: OidcService — Token Exchange & JWT Validation

**Files:**
- Modify: `src/security/OidcService.cpp`
- Test: `tests/unit/test_oidc_service.cpp` (add validation tests)

**Context:** This task completes the OIDC service with token exchange and ID token JWT validation. The `validateIdToken()` method must support RS256 and ES256 algorithms (the two most common for OIDC providers). It fetches the JWKS from the IdP, finds the matching key by `kid`, constructs an `EVP_PKEY` from the JWK components (n/e for RSA, x/y for EC), and verifies the signature using OpenSSL's `EVP_DigestVerify*` API. See `src/security/HmacJwtSigner.cpp` for the existing base64url decode pattern.

**Step 1: Write the failing tests**

Add to `tests/unit/test_oidc_service.cpp`:
- `ValidateIdTokenRejectsExpiredToken` — construct a JWT with `exp` in the past, verify `AuthenticationError` thrown
- `ValidateIdTokenRejectsWrongIssuer` — construct a JWT with wrong `iss`, verify error
- `ValidateIdTokenRejectsWrongAudience` — construct a JWT with wrong `aud`, verify error

Note: Full signature validation tests require a real JWKS endpoint or a test RSA key pair. For unit tests, focus on claim validation logic. Integration tests (Task 18) will cover the full flow.

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="OidcService*Validate*" -v`
Expected: Tests fail with expected errors.

**Step 3: Implement token exchange and JWT validation**

The `exchangeCode()` method:
- Parse token endpoint URL into scheme+host and path
- `httplib::Client` POST with form params: `grant_type`, `code`, `client_id`, `client_secret`, `redirect_uri`, `code_verifier`
- Return parsed JSON response (contains `access_token`, `id_token`, `token_type`)
- Throw `AuthenticationError` on non-200 response

The `validateIdToken()` method:
- Split JWT into header.payload.signature
- Decode header → extract `kid` and `alg`
- Fetch JWKS from `sJwksUri` via httplib
- Find matching key by `kid` (or first key matching `alg` if no kid)
- For RS256: construct `EVP_PKEY` from JWK `n` and `e` via `BN_bin2bn` → `RSA_set0_key` → `EVP_PKEY_assign_RSA`
- For ES256: construct from JWK `x` and `y` via `EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)` → `EC_KEY_set_public_key_affine_coordinates`
- Verify signature: `EVP_DigestVerifyInit` → `EVP_DigestVerifyUpdate` → `EVP_DigestVerifyFinal`
- Decode payload, validate: `iss` matches expected, `aud` contains expected client_id, `exp` > now
- Return decoded payload JSON

**Step 4: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="OidcService*" -v`
Expected: All tests PASS.

**Step 5: Commit**

```bash
git add src/security/OidcService.cpp tests/unit/test_oidc_service.cpp
git commit -m "feat(ws4): add OIDC token exchange and ID token JWT validation"
```

---

## Task 6: SamlService — AuthnRequest Generation

**Files:**
- Create: `include/security/SamlService.hpp`
- Create: `src/security/SamlService.cpp`
- Test: `tests/unit/test_saml_service.cpp`

**Context:** The SAML service handles SAML 2.0 protocol operations. AuthnRequest is an XML document sent to the IdP's SSO URL via HTTP-Redirect binding (GET with deflated, base64-encoded, URL-encoded SAMLRequest parameter). The service also manages SAML auth state (similar to OIDC state) and validates SAML assertions on the callback. The existing `SamlReplayCache` (`src/security/SamlReplayCache.cpp`) is used for assertion replay prevention.

**Step 1: Write the failing test**

Create `tests/unit/test_saml_service.cpp` with tests for:
- `GenerateAuthnRequest` — verify XML contains `<samlp:AuthnRequest>`, `AssertionConsumerServiceURL`, `Issuer`, `ID` attribute
- `BuildRedirectUrl` — verify URL contains `SAMLRequest=` parameter and `RelayState=` parameter
- `StoreAndRetrieveSamlState` — store state, consume it, verify fields, second consume returns nullopt

**Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="SamlService*" -v`
Expected: Compilation fails — `SamlService.hpp` not found.

**Step 3: Write the header**

Create `include/security/SamlService.hpp`:

```cpp
struct SamlAuthState {
  int64_t iIdpId = 0;
  std::string sRequestId;  // ID from AuthnRequest for InResponseTo validation
  bool bIsTestMode = false;
  std::chrono::system_clock::time_point tpCreatedAt;
};

class SamlService {
 public:
  SamlService(SamlReplayCache& srcCache);
  ~SamlService();

  /// Generate a SAML AuthnRequest XML document.
  std::string generateAuthnRequest(const std::string& sSpEntityId,
                                   const std::string& sAcsUrl,
                                   const std::string& sIdpSsoUrl);

  /// Build the SSO redirect URL with deflated, base64-encoded AuthnRequest.
  std::string buildRedirectUrl(const std::string& sIdpSsoUrl,
                               const std::string& sAuthnRequest,
                               const std::string& sRelayState);

  void storeAuthState(const std::string& sRelayState, SamlAuthState saState);
  std::optional<SamlAuthState> consumeAuthState(const std::string& sRelayState);

  /// Parse and validate a SAML Response/Assertion.
  /// Returns decoded attributes as JSON on success.
  nlohmann::json validateAssertion(const std::string& sSamlResponse,
                                   const std::string& sIdpCertPem,
                                   const std::string& sExpectedAudience,
                                   const std::string& sExpectedRequestId);

 private:
  void evictExpiredStates();
  SamlReplayCache& _srcCache;
  std::mutex _mtxStates;
  std::unordered_map<std::string, SamlAuthState> _mAuthStates;
};
```

**Step 4: Write the implementation (AuthnRequest generation only)**

Create `src/security/SamlService.cpp`:
- `generateAuthnRequest()`: build XML string with:
  - `<samlp:AuthnRequest>` root element with `xmlns:samlp`, `xmlns:saml` namespaces
  - `ID` attribute: `_` + random hex string (32 chars)
  - `Version="2.0"`, `IssueInstant` (ISO 8601 UTC), `Destination` (IdP SSO URL)
  - `AssertionConsumerServiceURL` (our ACS endpoint), `ProtocolBinding="urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST"`
  - `<saml:Issuer>` element with SP entity ID
- `buildRedirectUrl()`: deflate XML with zlib (`deflateInit2` with `-MAX_WBITS` for raw deflate), base64-encode, URL-encode, append as `SAMLRequest` query param. Add `RelayState` param.
- `storeAuthState()` / `consumeAuthState()`: same mutex+map+TTL pattern as OidcService

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="SamlService*" -v`
Expected: All 3 tests PASS.

**Step 6: Commit**

```bash
git add include/security/SamlService.hpp src/security/SamlService.cpp tests/unit/test_saml_service.cpp
git commit -m "feat(ws4): add SamlService with AuthnRequest generation and state management"
```

---

## Task 7: SamlService — Assertion Validation

**Files:**
- Modify: `src/security/SamlService.cpp`
- Test: `tests/unit/test_saml_service.cpp` (add assertion validation tests)

**Context:** SAML assertions are XML documents signed by the IdP. Validation requires: base64-decode the SAMLResponse, parse XML, verify the XML signature against the IdP's certificate (from `identity_providers.config.certificate`), check `InResponseTo` matches our AuthnRequest ID, check `Audience` matches our SP entity ID, check `NotBefore`/`NotOnOrAfter` timestamps, check replay via `SamlReplayCache`, extract `NameID` and attributes. OpenSSL is used for signature verification. XML parsing is done with a lightweight string-based parser (no external XML library dependency — the SAML assertions we need to parse have a predictable structure).

**Step 1: Write the failing tests**

Add to `tests/unit/test_saml_service.cpp`:
- `ValidateAssertionRejectsExpired` — construct a SAML response with `NotOnOrAfter` in the past, verify error
- `ValidateAssertionRejectsWrongAudience` — construct with wrong audience, verify error
- `ExtractAttributes` — construct a valid (unsigned for unit test) assertion with attributes, verify extraction

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="SamlService*Validate*" -v`
Expected: Tests fail.

**Step 3: Implement assertion validation**

Add to `src/security/SamlService.cpp`:

`validateAssertion()` method:
1. Base64-decode the `SAMLResponse` POST parameter
2. Parse XML to extract:
   - `<samlp:StatusCode Value="urn:oasis:names:tc:SAML:2.0:status:Success"/>` — reject if not success
   - `<saml:Assertion>` element
   - `<ds:SignatureValue>` and `<ds:DigestValue>` from `<ds:Signature>`
   - `<saml:Conditions NotBefore="..." NotOnOrAfter="...">` — validate timestamps
   - `<saml:AudienceRestriction><saml:Audience>` — validate matches expected
   - `<saml:Subject><saml:SubjectConfirmation><saml:SubjectConfirmationData InResponseTo="...">` — validate matches request ID
   - `<saml:NameID>` — extract subject identifier
   - `<saml:AttributeStatement><saml:Attribute>` — extract all attributes
3. Verify XML signature:
   - Extract the `<ds:SignedInfo>` element (canonicalized)
   - Load IdP certificate PEM → `X509*` → `EVP_PKEY*`
   - `EVP_DigestVerifyInit` with SHA-256 → `EVP_DigestVerifyUpdate` with canonicalized SignedInfo → `EVP_DigestVerifyFinal` with decoded signature
4. Check replay: `_srcCache.checkAndInsert(assertionId, notOnOrAfter)` — reject if replay detected
5. Return JSON with `name_id`, `attributes` (map of attribute name → values array)

XML parsing approach: use string search (`find`, `substr`) for the predictable SAML structure. Rather than pulling in a full XML library, implement targeted extraction helpers (`extractElement`, `extractAttribute`) that handle the known SAML response structure. This is acceptable because we validate the signature over the raw XML bytes, not a re-serialized DOM.

**Step 4: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="SamlService*" -v`
Expected: All tests PASS.

**Step 5: Commit**

```bash
git add src/security/SamlService.cpp tests/unit/test_saml_service.cpp
git commit -m "feat(ws4): add SAML assertion validation with signature verification"
```

---

## Task 8: FederatedAuthService — User Provisioning & Group Mapping

**Files:**
- Create: `include/security/FederatedAuthService.hpp`
- Create: `src/security/FederatedAuthService.cpp`
- Test: `tests/unit/test_federated_auth_service.cpp`

**Context:** This service is shared between OIDC and SAML flows. After the IdP returns claims/attributes, this service handles: (1) looking up or creating the user, (2) matching IdP groups against mapping rules, (3) assigning the user to Meridian groups, (4) issuing a JWT session. It uses `UserRepository` for user CRUD, `GroupRepository` for group lookups, and `AuthService`-style JWT issuance (see `src/security/AuthService.cpp:56-76` for the JWT creation pattern).

**Step 1: Write the failing test**

Create `tests/unit/test_federated_auth_service.cpp` with tests for:
- `MatchGroupMappingExact` — rule `{"idp_group": "dns-admins", "meridian_group_id": 1}` matches group "dns-admins"
- `MatchGroupMappingWildcard` — rule `{"idp_group": "platform-*", "meridian_group_id": 3}` matches "platform-team", "platform-ops"
- `MatchGroupMappingNoMatch` — no rules match, returns default_group_id
- `MatchGroupMappingEmpty` — no groups from IdP, returns default_group_id

These test the static `matchGroups()` method which takes the mapping rules JSON, a vector of IdP group strings, and the default group ID.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="FederatedAuth*" -v`
Expected: Compilation fails.

**Step 3: Write the header**

Create `include/security/FederatedAuthService.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::dal {
class UserRepository;
class GroupRepository;
class RoleRepository;
class SessionRepository;
}

namespace dns::security {
class IJwtSigner;

/// Handles user provisioning and group mapping for federated auth (OIDC/SAML).
/// Class abbreviation: fas
class FederatedAuthService {
 public:
  FederatedAuthService(dal::UserRepository& urRepo,
                       dal::GroupRepository& grRepo,
                       dal::RoleRepository& rrRepo,
                       dal::SessionRepository& srRepo,
                       const IJwtSigner& jsSigner,
                       int iJwtTtlSeconds,
                       int iSessionAbsoluteTtlSeconds);
  ~FederatedAuthService();

  struct LoginResult {
    std::string sToken;
    int64_t iUserId = 0;
    std::string sUsername;
    bool bNewUser = false;
  };

  LoginResult processFederatedLogin(
      const std::string& sAuthMethod,
      const std::string& sFederatedId,
      const std::string& sUsername,
      const std::string& sEmail,
      const std::vector<std::string>& vIdpGroups,
      const nlohmann::json& jGroupMappings,
      int64_t iDefaultGroupId);

  static std::vector<int64_t> matchGroups(
      const nlohmann::json& jGroupMappings,
      const std::vector<std::string>& vIdpGroups,
      int64_t iDefaultGroupId);

 private:
  dal::UserRepository& _urRepo;
  dal::GroupRepository& _grRepo;
  dal::RoleRepository& _rrRepo;
  dal::SessionRepository& _srRepo;
  const IJwtSigner& _jsSigner;
  int _iJwtTtlSeconds;
  int _iSessionAbsoluteTtlSeconds;
};

}  // namespace dns::security
```

**Step 4: Write the implementation**

Create `src/security/FederatedAuthService.cpp`:

`matchGroups()` (static):
- Iterate `jGroupMappings["rules"]` array
- For each rule, check if any IdP group matches `rule["idp_group"]`
- Wildcard matching: if rule ends with `*`, check if IdP group starts with the prefix
- Collect matched `meridian_group_id` values (deduplicate)
- If no matches and `iDefaultGroupId > 0`, return `{iDefaultGroupId}`
- Return empty vector if no matches and no default

`processFederatedLogin()`:
1. Look up user by federated ID:
   - If `sAuthMethod == "oidc"`: `_urRepo.findByOidcSub(sFederatedId)`
   - If `sAuthMethod == "saml"`: `_urRepo.findBySamlNameId(sFederatedId)`
2. If not found: create user via `_urRepo.createFederated(sUsername, sEmail, sAuthMethod, oidcSub, samlNameId)`
3. If found: update email if changed via `_urRepo.updateFederatedEmail()`
4. Check `bIsActive` — throw `AuthenticationError` if disabled
5. Match groups via `matchGroups()`, assign user to matched groups via `_urRepo.addToGroup()`
   - For each matched group, use the Viewer system role as default role_id for federated users
6. Build JWT payload (same pattern as `AuthService::authenticateLocal()` lines 56-67):
   - `sub`, `username`, `role` (from `_rrRepo.getHighestRoleName()`), `auth_method`, `iat`, `exp`
7. Sign JWT, create session, return `LoginResult`

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="FederatedAuth*" -v`
Expected: All 4 tests PASS.

**Step 6: Commit**

```bash
git add include/security/FederatedAuthService.hpp src/security/FederatedAuthService.cpp tests/unit/test_federated_auth_service.cpp
git commit -m "feat(ws4): add FederatedAuthService with user provisioning and group mapping"
```

---

## Task 9: OIDC Auth Routes

**Files:**
- Create: `include/api/routes/OidcRoutes.hpp`
- Create: `src/api/routes/OidcRoutes.cpp`

**Context:** Two public (no auth) routes handle the OIDC flow. The login route initiates the flow by redirecting to the IdP. The callback route handles the IdP's redirect back with the authorization code. See `src/api/routes/AuthRoutes.cpp` for the existing route registration pattern with Crow. These routes are public (no `authenticate()` call) because the user isn't logged in yet.

**Step 1: Write the header**

Create `include/api/routes/OidcRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::dal { class IdpRepository; }
namespace dns::security {
class OidcService;
class FederatedAuthService;
}

namespace dns::api::routes {

class OidcRoutes {
 public:
  OidcRoutes(dns::dal::IdpRepository& irRepo,
             dns::security::OidcService& osService,
             dns::security::FederatedAuthService& fasService);
  ~OidcRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::IdpRepository& _irRepo;
  dns::security::OidcService& _osService;
  dns::security::FederatedAuthService& _fasService;
};

}  // namespace dns::api::routes
```

**Step 2: Write the implementation**

Create `src/api/routes/OidcRoutes.cpp`:

Route 1: `GET /api/v1/auth/oidc/<int>/login`
1. Load IdP by ID from `_irRepo.findById(id)` — 404 if not found or not enabled or type != "oidc"
2. Extract `issuer_url`, `client_id`, `redirect_uri`, `scopes` from `jConfig`
3. Call `_osService.discover(issuer_url)` to get authorization endpoint
4. Generate PKCE: `auto [sVerifier, sChallenge] = OidcService::generatePkce()`
5. Generate state: `auto sState = OidcService::generateState()`
6. Store auth state: `_osService.storeAuthState(sState, {sVerifier, id, false})`
7. Build scopes string from JSON array (join with space)
8. Build authorization URL: `OidcService::buildAuthorizationUrl(...)`
9. Return 302 redirect: `crow::response resp(302); resp.set_header("Location", sUrl); return resp;`

Route 2: `GET /api/v1/auth/oidc/<int>/callback`
1. Extract `code` and `state` query parameters from request URL
2. Consume auth state: `_osService.consumeAuthState(state)` — 400 if not found
3. Verify `oaState.iIdpId` matches the URL path ID
4. Load IdP by ID from `_irRepo.findById(id)` — includes decrypted secret
5. Discover endpoints: `_osService.discover(issuer_url)`
6. Exchange code: `_osService.exchangeCode(tokenEndpoint, code, client_id, secret, redirect_uri, verifier)`
7. Validate ID token: `_osService.validateIdToken(id_token, jwks_uri, issuer, client_id)`
8. Extract `sub`, `email`, `preferred_username`, and groups claim from payload
9. If `oaState.bIsTestMode`: return JSON with raw claims (for diagnostic)
10. Call `_fasService.processFederatedLogin("oidc", sub, username, email, groups, mappings, defaultGroupId)`
11. Redirect to `/#/auth/callback?token={token}` so the SPA can store the JWT

**Step 3: Verify it compiles**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 4: Commit**

```bash
git add include/api/routes/OidcRoutes.hpp src/api/routes/OidcRoutes.cpp
git commit -m "feat(ws4): add OIDC login and callback routes"
```

---

## Task 10: SAML Auth Routes

**Files:**
- Create: `include/api/routes/SamlRoutes.hpp`
- Create: `src/api/routes/SamlRoutes.cpp`

**Context:** Two public routes handle the SAML flow. The login route generates an AuthnRequest and redirects to the IdP's SSO URL. The ACS (Assertion Consumer Service) route handles the IdP's POST with the SAML assertion. The ACS route receives a POST with `SAMLResponse` and `RelayState` form parameters.

**Step 1: Write the header**

Create `include/api/routes/SamlRoutes.hpp` (same pattern as OidcRoutes, but with `SamlService&` instead of `OidcService&`).

**Step 2: Write the implementation**

Create `src/api/routes/SamlRoutes.cpp`:

Route 1: `GET /api/v1/auth/saml/<int>/login`
1. Load IdP by ID — 404 if not found/disabled/wrong type
2. Extract `entity_id`, `sso_url`, `assertion_consumer_service_url` from `jConfig`
3. Generate AuthnRequest: `_ssService.generateAuthnRequest(entity_id, acs_url, sso_url)`
4. Generate relay state (random string), store SAML auth state with request ID
5. Build redirect URL: `_ssService.buildRedirectUrl(sso_url, authnRequest, relayState)`
6. Return 302 redirect

Route 2: `POST /api/v1/auth/saml/<int>/acs`
1. Parse URL-encoded form body to extract `SAMLResponse` and `RelayState` parameters
2. Consume auth state via relay state — 400 if not found
3. Load IdP by ID
4. Extract `certificate`, `entity_id` from `jConfig`
5. Validate assertion: `_ssService.validateAssertion(samlResponse, certificate, entity_id, requestId)`
6. Extract `name_id`, group attribute from returned JSON
7. If test mode: return HTML page displaying raw attributes (since this is a POST from IdP, can't return JSON directly to SPA)
8. Call `_fasService.processFederatedLogin("saml", name_id, username, email, groups, mappings, defaultGroupId)`
9. Return HTML page with JavaScript that redirects to `/#/auth/callback?token={token}`

**Step 3: Verify it compiles**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 4: Commit**

```bash
git add include/api/routes/SamlRoutes.hpp src/api/routes/SamlRoutes.cpp
git commit -m "feat(ws4): add SAML login and ACS routes"
```

---

## Task 11: IdP Admin CRUD Routes

**Files:**
- Create: `include/api/routes/IdpRoutes.hpp`
- Create: `src/api/routes/IdpRoutes.cpp`

**Context:** Admin CRUD routes for managing identity providers. Requires `settings.view` for GET and `settings.edit` for mutations (as specified in the design doc API endpoints table). Follow the same pattern as existing route files — authenticate, check permission, validate input, call repository, return JSON. The secret field should never be returned in responses (only a boolean `has_secret` indicator).

**Step 1: Write the header**

Create `include/api/routes/IdpRoutes.hpp`:

```cpp
#pragma once

#include <crow.h>

namespace dns::api { class AuthMiddleware; }
namespace dns::dal { class IdpRepository; }

namespace dns::api::routes {

class IdpRoutes {
 public:
  IdpRoutes(dns::dal::IdpRepository& irRepo, const dns::api::AuthMiddleware& amMiddleware);
  ~IdpRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::IdpRepository& _irRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
```

**Step 2: Write the implementation**

Create `src/api/routes/IdpRoutes.cpp` with 5 routes:

`GET /api/v1/identity-providers` — `settings.view` permission
- Call `_irRepo.listAll()`
- Map each `IdpRow` to JSON: `id`, `name`, `type`, `is_enabled`, `config`, `has_secret` (bool), `group_mappings`, `default_group_id`, `created_at`, `updated_at`

`POST /api/v1/identity-providers` — `settings.edit` permission
- Parse JSON body: `name`, `type`, `config`, `client_secret` (optional), `group_mappings`, `default_group_id`
- Validate: `name` required, `type` must be "oidc" or "saml"
- For OIDC: validate `config` has `issuer_url`, `client_id`
- For SAML: validate `config` has `entity_id`, `sso_url`, `certificate`
- Call `_irRepo.create()`
- Return 201 with `{id, name}`

`GET /api/v1/identity-providers/<int>` — `settings.view` permission
- Call `_irRepo.findById(id)` — 404 if not found
- Return full config (still no raw secret, but include `has_secret`)

`PUT /api/v1/identity-providers/<int>` — `settings.edit` permission
- Parse JSON body, call `_irRepo.update()`
- If `client_secret` is empty string or missing, don't update the secret

`DELETE /api/v1/identity-providers/<int>` — `settings.edit` permission
- Call `_irRepo.deleteIdp(id)`

**Step 3: Verify it compiles**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 4: Commit**

```bash
git add include/api/routes/IdpRoutes.hpp src/api/routes/IdpRoutes.cpp
git commit -m "feat(ws4): add identity provider admin CRUD routes"
```

---

## Task 12: IdP Test Diagnostic Route

**Files:**
- Modify: `src/api/routes/IdpRoutes.cpp`

**Context:** The test diagnostic route (`GET /api/v1/identity-providers/{id}/test`) initiates the auth flow with `bIsTestMode = true` in the state. On callback, instead of provisioning a user, the raw decoded claims (OIDC) or attributes (SAML) are returned as JSON. This helps admins see exactly what the IdP sends so they can configure group mapping rules correctly.

**Step 1: Add the test route**

Add to `src/api/routes/IdpRoutes.cpp`:

`GET /api/v1/identity-providers/<int>/test` — `settings.edit` permission
1. Load IdP by ID
2. If type == "oidc":
   - Discover endpoints
   - Generate PKCE + state
   - Store auth state with `bIsTestMode = true`
   - Return JSON with `redirect_url` (the authorization URL) — the UI will open this in a popup
3. If type == "saml":
   - Generate AuthnRequest
   - Store auth state with `bIsTestMode = true`
   - Return JSON with `redirect_url` (the SSO redirect URL)

The callback routes (Task 9 and 10) already check `bIsTestMode` and return raw claims instead of provisioning.

**Step 2: Verify it compiles**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 3: Commit**

```bash
git add src/api/routes/IdpRoutes.cpp
git commit -m "feat(ws4): add IdP test diagnostic route"
```

---

## Task 13: Startup Wiring

**Files:**
- Modify: `src/main.cpp`

**Context:** Wire all new components into the startup sequence in `src/main.cpp`. Follow the existing pattern: construct repositories after ConnectionPool (Step 7a area), construct services after repositories, construct routes after services, register routes on the Crow app. See `src/main.cpp:269-441` for the current wiring pattern.

**Step 1: Add includes**

Add to the includes section of `src/main.cpp`:

```cpp
#include "api/routes/IdpRoutes.hpp"
#include "api/routes/OidcRoutes.hpp"
#include "api/routes/SamlRoutes.hpp"
#include "dal/IdpRepository.hpp"
#include "security/FederatedAuthService.hpp"
#include "security/OidcService.hpp"
#include "security/SamlService.hpp"
```

**Step 2: Construct new components**

Add after the existing repository construction (around line 283):

```cpp
auto idpRepo = std::make_unique<dns::dal::IdpRepository>(*cpPool, *csService);
```

Add after the SamlReplayCache initialization (around line 349):

```cpp
auto oidcService = std::make_unique<dns::security::OidcService>();
auto samlService = std::make_unique<dns::security::SamlService>(*srcCache);
```

Add after AuthService construction (around line 407):

```cpp
auto fedAuthService = std::make_unique<dns::security::FederatedAuthService>(
    *urRepo, *grRepo, *roleRepo, *srRepo, *upSigner,
    cfgApp.iJwtTtlSeconds, cfgApp.iSessionAbsoluteTtlSeconds);
```

**Step 3: Construct and register routes**

Add after the existing route construction (around line 428):

```cpp
auto idpRoutes = std::make_unique<dns::api::routes::IdpRoutes>(*idpRepo, *amMiddleware);
auto oidcRoutes = std::make_unique<dns::api::routes::OidcRoutes>(
    *idpRepo, *oidcService, *fedAuthService);
auto samlRoutes = std::make_unique<dns::api::routes::SamlRoutes>(
    *idpRepo, *samlService, *fedAuthService);
```

Add after the existing `registerRoutes()` calls (around line 441):

```cpp
idpRoutes->registerRoutes(crowApp);
oidcRoutes->registerRoutes(crowApp);
samlRoutes->registerRoutes(crowApp);
```

**Step 4: Verify it compiles and starts**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(ws4): wire OIDC/SAML services and routes into startup"
```

---

## Task 14: Frontend — API Client & Types

**Files:**
- Modify: `ui/src/types/index.ts`
- Create: `ui/src/api/identityProviders.ts`

**Context:** Add TypeScript types for identity providers and the API client module. Follow the existing patterns in `ui/src/types/index.ts` and `ui/src/api/providers.ts`. The API client uses the typed `get`/`post`/`put`/`del` helpers from `ui/src/api/client.ts`.

**Step 1: Add types**

Add to `ui/src/types/index.ts`:

```typescript
export interface IdentityProvider {
  id: number
  name: string
  type: 'oidc' | 'saml'
  is_enabled: boolean
  config: Record<string, unknown>
  has_secret: boolean
  group_mappings: GroupMappingRules | null
  default_group_id: number | null
  created_at: string
  updated_at: string
}

export interface GroupMappingRule {
  idp_group: string
  meridian_group_id: number
}

export interface GroupMappingRules {
  rules: GroupMappingRule[]
}

export interface IdentityProviderCreate {
  name: string
  type: 'oidc' | 'saml'
  config: Record<string, unknown>
  client_secret?: string
  group_mappings?: GroupMappingRules
  default_group_id?: number
}

export interface IdentityProviderUpdate {
  name: string
  is_enabled: boolean
  config: Record<string, unknown>
  client_secret?: string
  group_mappings?: GroupMappingRules
  default_group_id?: number
}

export interface IdpTestResult {
  redirect_url: string
}

export interface IdpClaimsResult {
  subject: string
  email: string
  username: string
  groups: string[]
  all_claims: Record<string, unknown>
}

export interface EnabledIdp {
  id: number
  name: string
  type: 'oidc' | 'saml'
}
```

**Step 2: Create API client module**

Create `ui/src/api/identityProviders.ts`:

```typescript
import { get, post, put, del } from './client'
import type {
  IdentityProvider, IdentityProviderCreate,
  IdentityProviderUpdate, IdpTestResult, EnabledIdp
} from '../types'

export function listIdentityProviders(): Promise<IdentityProvider[]> {
  return get('/identity-providers')
}

export function getIdentityProvider(id: number): Promise<IdentityProvider> {
  return get(`/identity-providers/${id}`)
}

export function createIdentityProvider(
  data: IdentityProviderCreate
): Promise<{ id: number; name: string }> {
  return post('/identity-providers', data)
}

export function updateIdentityProvider(
  id: number, data: IdentityProviderUpdate
): Promise<{ message: string }> {
  return put(`/identity-providers/${id}`, data)
}

export function deleteIdentityProvider(id: number): Promise<void> {
  return del(`/identity-providers/${id}`)
}

export function testIdentityProvider(id: number): Promise<IdpTestResult> {
  return get(`/identity-providers/${id}/test`)
}

export function listEnabledIdps(): Promise<EnabledIdp[]> {
  return get('/auth/identity-providers')
}
```

**Step 3: Commit**

```bash
git add ui/src/types/index.ts ui/src/api/identityProviders.ts
git commit -m "feat(ws4): add frontend types and API client for identity providers"
```

---

## Task 15: Frontend — Identity Providers Admin View

**Files:**
- Create: `ui/src/views/IdentityProvidersView.vue`
- Modify: `ui/src/router/index.ts`
- Modify: `ui/src/components/layout/AppShell.vue` (add sidebar link)

**Context:** Admin page for managing identity providers. Follow the existing DataTable CRUD pattern from `ui/src/views/ProvidersView.vue` — DataTable with columns, Dialog for create/edit, delete confirmation. The form changes based on the selected type (OIDC vs SAML) to show type-specific config fields. Group mapping rules are edited as a dynamic list (add/remove rows). A "Test Authentication" button opens a popup window for the diagnostic flow.

**Step 1: Create the view component**

Create `ui/src/views/IdentityProvidersView.vue`:

Template structure:
- `PageHeader` with title "Identity Providers" and "Add Provider" button
- `DataTable` with columns: Name, Type (badge), Enabled (toggle), Default Group, Actions (edit/delete)
- `Dialog` for create/edit form with:
  - Name input
  - Type dropdown (OIDC / SAML) — disabled on edit
  - Enabled toggle
  - **OIDC config fields** (shown when type == 'oidc'):
    - Issuer URL, Client ID, Client Secret (password), Redirect URI (auto-generated), Scopes, Groups Claim
  - **SAML config fields** (shown when type == 'saml'):
    - SP Entity ID, IdP SSO URL, IdP Certificate (textarea), ACS URL (auto-generated), Name ID Format, Group Attribute
  - **Group Mapping Rules** section:
    - Dynamic list: IdP Group input → Meridian Group dropdown
    - Add/Remove buttons, wildcard hint
  - Default Group dropdown
  - "Test Authentication" button (opens popup)

Script: use `useCrud` composable, `useConfirm` for delete.

**Step 2: Add route**

Add to `ui/src/router/index.ts` children array:

```typescript
{
  path: 'identity-providers',
  name: 'identity-providers',
  component: () => import('../views/IdentityProvidersView.vue'),
},
```

**Step 3: Add sidebar link**

Add to `ui/src/components/layout/AppShell.vue` sidebar navigation (admin section):
- Icon: `pi pi-shield`, label: "Identity Providers", route: `/identity-providers`
- Visible when user has `settings.view` permission

**Step 4: Verify UI renders**

Run: `cd ui && npm run dev`
Navigate to `/identity-providers` — verify the page renders.

**Step 5: Commit**

```bash
git add ui/src/views/IdentityProvidersView.vue ui/src/router/index.ts ui/src/components/layout/AppShell.vue
git commit -m "feat(ws4): add Identity Providers admin view with CRUD and group mapping"
```

---

## Task 16: Frontend — Login Page Federated Buttons

**Files:**
- Modify: `ui/src/views/LoginView.vue`
- Modify: `ui/src/api/auth.ts`

**Context:** When any IdP is enabled, the login page shows federated login buttons below the local login form. Each button is labeled with the IdP name (e.g., "Sign in with Okta"). Clicking a button navigates to the OIDC/SAML login endpoint. The list of enabled IdPs is fetched from a public endpoint. See `ui/src/views/LoginView.vue` for the current login page structure.

**Step 1: Add the public IdP list endpoint (backend)**

Add to `src/api/routes/OidcRoutes.cpp` (or create a shared file):

`GET /api/v1/auth/identity-providers` — no auth required
- Call `_irRepo.listEnabled()`
- Return JSON array of `{id, name, type}` only

**Step 2: Add API function**

Add to `ui/src/api/auth.ts`:

```typescript
export function listEnabledIdps(): Promise<{ id: number; name: string; type: string }[]> {
  return get('/auth/identity-providers')
}
```

**Step 3: Update LoginView**

Modify `ui/src/views/LoginView.vue`:

- Add `onMounted` to fetch enabled IdPs
- Store in `const idps = ref<{id: number; name: string; type: string}[]>([])`
- Below the "Sign in" button, add a divider ("or") and federated buttons:

```html
<template v-if="idps.length > 0">
  <div class="login-divider"><span>or</span></div>
  <div class="federated-buttons">
    <Button
      v-for="idp in idps"
      :key="idp.id"
      :label="`Sign in with ${idp.name}`"
      :icon="idp.type === 'saml' ? 'pi pi-shield' : 'pi pi-lock'"
      severity="secondary"
      outlined
      class="w-full"
      @click="federatedLogin(idp)"
    />
  </div>
</template>
```

- `federatedLogin(idp)`: navigate to `/api/v1/auth/${idp.type}/${idp.id}/login` (full page redirect)

Add CSS for `.login-divider` (horizontal line with "or" text centered).

**Step 4: Verify UI renders**

Run: `cd ui && npm run dev`
Expected: Login page shows federated buttons when IdPs exist.

**Step 5: Commit**

```bash
git add ui/src/views/LoginView.vue ui/src/api/auth.ts src/api/routes/OidcRoutes.cpp
git commit -m "feat(ws4): add federated login buttons to login page"
```

---

## Task 17: Frontend — OIDC/SAML Callback Handler

**Files:**
- Create: `ui/src/views/AuthCallbackView.vue`
- Modify: `ui/src/router/index.ts`

**Context:** After a successful OIDC/SAML login, the backend redirects to `/#/auth/callback?token={jwt}`. The callback view extracts the token from the URL, stores it in localStorage, hydrates the auth store, and redirects to the dashboard. This is a transient view — the user should never see it for more than a flash. For SAML (POST binding), the backend returns an HTML page with JavaScript that sets `window.location` to the callback URL with the token.

**Step 1: Create the callback view**

Create `ui/src/views/AuthCallbackView.vue`:

```vue
<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

onMounted(async () => {
  const token = route.query.token as string
  if (!token) {
    router.push('/login')
    return
  }

  // Store the token and hydrate the auth store
  localStorage.setItem('jwt', token)
  auth.token = token
  const valid = await auth.hydrate()

  if (valid) {
    router.push('/')
  } else {
    router.push('/login')
  }
})
</script>

<template>
  <div class="callback-page">
    <p>Signing in...</p>
  </div>
</template>

<style scoped>
.callback-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  color: var(--p-surface-400);
}
</style>
```

**Step 2: Add route**

Add to `ui/src/router/index.ts`:

```typescript
{
  path: '/auth/callback',
  name: 'auth-callback',
  component: () => import('../views/AuthCallbackView.vue'),
  meta: { public: true },
},
```

**Step 3: Verify it works**

Run: `cd ui && npm run dev`
Navigate to `/#/auth/callback?token=test` — should redirect to login (invalid token).

**Step 4: Commit**

```bash
git add ui/src/views/AuthCallbackView.vue ui/src/router/index.ts
git commit -m "feat(ws4): add auth callback view for federated login token handling"
```

---

## Task 18: Integration Tests

**Files:**
- Create: `tests/integration/test_federated_auth.cpp`

**Context:** Integration tests that verify the full federated auth flow works end-to-end with the database. These tests require `DNS_DB_URL` and `DNS_MASTER_KEY` environment variables. They test the complete chain: IdP repository CRUD, user provisioning via `FederatedAuthService`, group mapping, and JWT issuance.

**Step 1: Write the integration tests**

Create `tests/integration/test_federated_auth.cpp`:

Test fixture `FederatedAuthTest`:
- `SetUp()`: check env vars, create `ConnectionPool`, `CryptoService`, `HmacJwtSigner`, repositories (`UserRepository`, `GroupRepository`, `RoleRepository`, `SessionRepository`, `IdpRepository`), `FederatedAuthService`
- Clean up test data in `SetUp()`

Tests:
- `ProvisionNewOidcUser` — call `processFederatedLogin("oidc", "oidc|new-user", ...)`, verify user created in DB with correct auth_method, verify JWT returned
- `ProvisionNewSamlUser` — same for SAML
- `ExistingUserUpdatesEmail` — create user, call `processFederatedLogin` with new email, verify email updated
- `DisabledUserRejected` — create user, deactivate, call `processFederatedLogin`, verify `AuthenticationError` thrown
- `GroupMappingAssignsCorrectGroups` — create groups, create IdP with mapping rules, call `processFederatedLogin` with matching IdP groups, verify user assigned to correct Meridian groups
- `GroupMappingWildcard` — test wildcard matching (e.g., `platform-*` matches `platform-team`)
- `GroupMappingFallsBackToDefault` — no rules match, verify user assigned to default group
- `IdpCrudWithEncryptedSecret` — create IdP, verify secret encrypted in DB, retrieve and verify decrypted

**Step 2: Run tests**

Run: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter="FederatedAuth*" -v`
Expected: All 8 tests PASS (or SKIP if no DNS_DB_URL).

**Step 3: Commit**

```bash
git add tests/integration/test_federated_auth.cpp
git commit -m "feat(ws4): add integration tests for federated authentication"
```

---

## Summary of Files Created/Modified

### New Files (17)

| File | Purpose |
|------|---------|
| `scripts/db/v009/001_identity_providers.sql` | Migration: identity_providers table |
| `include/dal/IdpRepository.hpp` | IdP repository header |
| `src/dal/IdpRepository.cpp` | IdP repository implementation |
| `include/security/OidcService.hpp` | OIDC service header |
| `src/security/OidcService.cpp` | OIDC discovery, PKCE, token exchange, JWT validation |
| `include/security/SamlService.hpp` | SAML service header |
| `src/security/SamlService.cpp` | SAML AuthnRequest, assertion validation |
| `include/security/FederatedAuthService.hpp` | Federated auth service header |
| `src/security/FederatedAuthService.cpp` | User provisioning, group mapping, JWT issuance |
| `include/api/routes/OidcRoutes.hpp` | OIDC route handler header |
| `src/api/routes/OidcRoutes.cpp` | OIDC login + callback routes |
| `include/api/routes/SamlRoutes.hpp` | SAML route handler header |
| `src/api/routes/SamlRoutes.cpp` | SAML login + ACS routes |
| `include/api/routes/IdpRoutes.hpp` | IdP admin CRUD route header |
| `src/api/routes/IdpRoutes.cpp` | IdP admin CRUD + test diagnostic routes |
| `ui/src/api/identityProviders.ts` | Frontend API client for IdPs |
| `ui/src/views/IdentityProvidersView.vue` | Admin IdP management page |
| `ui/src/views/AuthCallbackView.vue` | Federated auth callback handler |

### Modified Files (7)

| File | Change |
|------|--------|
| `include/dal/UserRepository.hpp` | Add `findByOidcSub`, `findBySamlNameId`, `createFederated`, `updateFederatedEmail` |
| `src/dal/UserRepository.cpp` | Implement new federated user methods |
| `src/main.cpp` | Wire IdpRepository, OidcService, SamlService, FederatedAuthService, routes |
| `ui/src/types/index.ts` | Add IdentityProvider, GroupMapping, EnabledIdp types |
| `ui/src/api/auth.ts` | Add `listEnabledIdps()` |
| `ui/src/views/LoginView.vue` | Add federated login buttons |
| `ui/src/router/index.ts` | Add identity-providers and auth-callback routes |
| `ui/src/components/layout/AppShell.vue` | Add Identity Providers sidebar link |

### Test Files (4)

| File | Tests |
|------|-------|
| `tests/integration/test_idp_repository.cpp` | IdP CRUD + federated user lookup |
| `tests/unit/test_oidc_service.cpp` | PKCE, state, URL building, token validation |
| `tests/unit/test_saml_service.cpp` | AuthnRequest, redirect URL, assertion validation |
| `tests/unit/test_federated_auth_service.cpp` | Group mapping (exact, wildcard, default) |
| `tests/integration/test_federated_auth.cpp` | Full flow: provisioning, groups, JWT |
