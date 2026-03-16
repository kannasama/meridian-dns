# Authentication Guide

## Overview

Meridian DNS supports three authentication methods:

1. **Local accounts** — Username/password with Argon2id hashing
2. **OIDC (OpenID Connect)** — Federated login via Keycloak, Okta, Azure AD, etc.
3. **SAML 2.0** — Enterprise SSO via ADFS, Okta, Azure AD, etc.

Additionally, **API keys** provide programmatic access for automation.

## Local Authentication

### Account Creation

Local accounts are created via:
- **Setup wizard** — First admin account during initial setup
- **Admin UI** — Users page (requires `users.create` permission)
- **API** — `POST /api/v1/users`

### Password Requirements

- Minimum 8 characters
- Maximum 1024 characters
- Hashed with Argon2id (memory-hard, timing-attack resistant)

### Forced Password Change

Admins can flag an account for forced password change. On next login, the user is
redirected to the change password page before accessing any other functionality.

### JWT Sessions

- Login returns a JWT token with configurable TTL (default: 8 hours)
- Sessions tracked in database for revocation support
- Absolute TTL prevents indefinite session extension (default: 24 hours)
- Session cleanup runs periodically (default: every hour)

## OIDC Provider Setup

### Prerequisites

- An OIDC-compliant identity provider (Keycloak, Okta, Azure AD, Auth0, etc.)
- The provider's discovery URL (e.g. `https://idp.example.com/.well-known/openid-configuration`)

### Configuration

1. Navigate to **Identity Providers** in the admin UI
2. Click **Add Provider** and select **OIDC**
3. Fill in the configuration:

| Field | Description |
|-------|-------------|
| Name | Display name (e.g. "Corporate SSO") |
| Issuer URL | OIDC discovery URL base |
| Client ID | Application client ID from your IdP |
| Client Secret | Application client secret |
| Scopes | Space-separated scopes (default: `openid profile email`) |
| Groups Claim | JWT claim containing group memberships (e.g. `groups`) |

4. Set the redirect URI in your IdP to: `{app.base_url}/api/v1/auth/oidc/{provider_id}/callback`

### PKCE Support

Meridian DNS uses PKCE (Proof Key for Code Exchange) for the authorization code flow,
providing additional security even if the client secret is compromised.

## SAML 2.0 Provider Setup

### Configuration

1. Navigate to **Identity Providers** and add a **SAML** provider
2. Fill in the configuration:

| Field | Description |
|-------|-------------|
| Name | Display name |
| Entity ID | Your IdP's entity ID |
| SSO URL | IdP's Single Sign-On URL |
| Certificate | IdP's X.509 signing certificate (PEM format) |
| Name ID Format | `urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress` (typical) |
| Group Attribute | SAML attribute containing group memberships |

3. Configure your IdP with:
   - **ACS URL:** `{app.base_url}/api/v1/auth/saml/{provider_id}/acs`
   - **Entity ID:** `{app.base_url}`

### SAML Security

- XML signature validation on all assertions
- Replay cache with TTL eviction prevents assertion reuse
- Audience restriction validation

## IdP Group Mapping

Map IdP groups to Meridian DNS groups for automatic role assignment:

1. Navigate to **Identity Providers** → select provider → **Group Mappings**
2. Add mapping rules:

| IdP Group Pattern | Meridian Group | Description |
|-------------------|----------------|-------------|
| `dns-admins` | `Administrators` | Exact match |
| `dns-*` | `Operators` | Wildcard match |
| *(default)* | `Viewers` | Fallback for unmapped users |

### How Mapping Works

On federated login:
1. IdP returns user's group memberships in the configured claim/attribute
2. Each group is matched against mapping rules (exact match first, then wildcards)
3. User is added to the corresponding Meridian group (which carries a role)
4. If no rules match, the default group is used

## Claim/Attribute Test Diagnostic

Before configuring group mappings, use the **Test** button on the IdP configuration
page to perform a test authentication. This shows the raw claims (OIDC) or
attributes (SAML) returned by the IdP, helping you identify the correct
groups claim name and format.

## Display Name Extraction

Display names are extracted from IdP tokens:
- **OIDC:** `name` claim, falling back to `preferred_username`
- **SAML:** Configurable attribute, typically `displayName` or `cn`

## API Keys

### Creating API Keys

1. Navigate to **Settings** → **API Keys** (or user profile)
2. Click **Create API Key**
3. Set a description for the key
4. The key is displayed **once** — copy and store it securely

### Authentication

Include the key in the `X-API-Key` header:

```bash
curl -H "X-API-Key: mdns_abc123..." https://dns.example.com/api/v1/zones
```

### Key Management

- Keys inherit permissions from the user who created them
- Revoke keys from the API Keys management page
- Revoked keys have a grace period before permanent deletion (default: 5 minutes)
- `last_used_at` timestamp tracks key usage

## Session Management

### JWT Structure

Tokens contain:
- `sub` — User ID
- `sid` — Session ID (for server-side tracking)
- `iat` — Issued at timestamp
- `exp` — Expiration timestamp

### Session Cleanup

The maintenance scheduler periodically removes expired sessions from the database.
Configure the interval via the `session.cleanup_interval_seconds` setting.
