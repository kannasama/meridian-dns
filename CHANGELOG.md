# Changelog

All notable changes to Meridian DNS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-XX-XX

### Added

- **Multi-provider DNS management** — Orchestrate DNS records across PowerDNS,
  Cloudflare, and DigitalOcean through an extensible provider interface
- **Split-horizon views** — Define internal and external views for the same domain
  with strict isolation
- **Variable template engine** — Store records with `{{var_name}}` placeholders;
  update a variable once and propagate to all affected records
- **Preview-before-deploy pipeline** — Diff staged changes against live provider
  state, detect and resolve drift before pushing
- **Deployment snapshots and rollback** — Every push captures a full zone snapshot;
  roll back to any previous state (full zone or cherry-picked records)
- **Multi-repo GitOps** — Zone snapshots committed to configured Git repositories
  with SSH and HTTPS authentication, branch-per-zone support
- **Zone capture** — Auto-capture baseline snapshots for zones never deployed through
  Meridian; manual capture button for on-demand snapshots
- **Config backup and restore** — Full system export (JSON) and transactional restore
  via file upload or GitOps pull; zone-level export/import
- **Named theme presets** — 14 dark themes (Catppuccin Mocha, Dracula, Nord, Tokyo
  Night, etc.) and 9 light themes with independent accent color customization
- **Granular RBAC** — 40+ discrete permissions collected into customizable roles with
  view-level and zone-level scoping
- **OIDC and SAML authentication** — Federated login with auto-provisioning, IdP
  group-to-Meridian-group mapping, claim/attribute test diagnostic
- **User and group management** — Full CRUD, forced password change, self-service
  profile editing
- **API key authentication** — Create, list, and revoke API keys with one-time display
- **Database-backed configuration** — Runtime settings stored in DB with env-var
  seeding on first run
- **Batch record import** — CSV, JSON, DNSControl, and provider import with preview
- **Audit trail** — Every mutation logged with before/after state, actor identity,
  NDJSON export, configurable retention
- **Health probes** — Liveness (`/health/live`) and readiness (`/health/ready`)
  endpoints for container orchestrators
- **OpenAPI 3.1 specification** — Complete API documentation covering all endpoints
- **Web UI** — Vue 3 + PrimeVue with dark mode, responsive layout, deployment diff
  viewer, variable autocomplete

### Security

- AGPL-3.0-or-later licensing
- AES-256-GCM encryption for provider tokens, Git credentials, and IdP secrets
- Argon2id password hashing
- HMAC-SHA256 JWT sessions with sliding + absolute TTL
- SAML replay cache with TTL eviction
- PKCE for OIDC authorization code flow
- Rate limiting on authentication endpoints

### Infrastructure

- Multi-stage Docker image (Fedora 43, ~180MB runtime)
- Docker Compose with PostgreSQL 16
- Container health checks via liveness probe
- CMake build system with C++20 and strict compiler warnings
