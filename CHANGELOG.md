# Changelog

All notable changes to Meridian DNS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.2] - 2026-03-24

### Fixed

- **Deployment execution ordering** — Deletes and drift-deletes now execute before adds and
  updates, fixing CNAME→A type changes that previously failed with a PowerDNS 422 error
  (RFC 1034 prohibits a CNAME and another record with the same name)
- **Provider record ID on updates** — Missing `sProviderRecordId` copy in the DeploymentEngine
  Update case caused all PowerDNS record updates to fail with "Invalid provider_record_id format"

### Changed

- `IProvider::deleteRecord` return type changed from `bool` to `PushResult`; all providers
  (PowerDNS, Cloudflare, DigitalOcean, Generic REST, Subprocess) now return structured error
  detail on delete failures
- PowerDNS error responses now include the API error reason from the response body, not just
  the HTTP status code

### Added

- **System log table** — Persistent technical logging for deployment operations, provider API
  calls, and application events; stored in the `system_logs` table
- **System log maintenance** — Configurable retention via `system_log.retention_days` setting
  (default 30 days) with periodic background purge
- **System Log admin page** — Filterable DataTable with severity, category, and time-range
  filters and a detail dialog; restricted to Admin role (`system_logs.view` permission)

## [1.1.1] - 2026-03-22

### Fixed

- **Provider import** — FQDN to relative name conversion now strips the zone apex correctly
- **Sync status** — Zone sync status now uses a tri-state model (synced / drifted / unknown)
  rather than boolean; UI badge updated accordingly
- **Tags** — Free-form tag entry corrected; tags moved from zone list inline to zone edit dialog
- Various UI fixes across provider, zone, and record views

### Added

- **User preferences** — Per-user persistent preferences stored in the `user_preferences` table
  and managed via `GET/PUT /api/v1/users/me/preferences`
- **Zone category filter** — Filter zone list by Forward, Reverse (in-addr.arpa / ip6.arpa), or All
- **Zone filter persistence** — Active zone filter and column preferences saved to user preferences
  and restored on next visit
- **Theme persistence** — Dark/light mode and accent color migrated from `localStorage` to user
  preferences for cross-device consistency
- **Tag admin** — Tag creation and management endpoint; admin UI for the global tag vocabulary

### Changed

- All 12 CRUD views migrated from PrimeVue `Drawer` to `Dialog` for consistency and better
  keyboard/accessibility behavior

## [1.1.0] - 2026-03-21

### Added

- **Zone Templates** — Combine SOA presets and snippets as reusable zone blueprints;
  apply templates during zone creation or on-demand; advisory compliance check diffs
  live zone against the source template and surfaces Add/Update actions only
- **Snippets** — Named, reusable record sets; applying a snippet bulk-inserts its
  records into a zone as a one-shot operation with no ongoing link to the snippet
- **SOA Presets** — Named SOA timing profiles (`mname`, `rname`, `refresh`, `retry`,
  `expire`, `minimum`, `default_ttl`) with `{{var}}` and `{{sys.*}}` placeholder support
- **Built-in Dynamic Variables** — `{{sys.serial}}` (auto-incrementing SOA serial in
  `YYYYMMDDNN` format, durable across restarts), `{{sys.date}}`, `{{sys.year}}`
- **User-defined Dynamic Variable Types** — Custom variable types with configurable
  dynamic value resolution
- **Zone Clone** — Duplicate a zone and all its records within or across views
- **Global Search** — Full-text search across zones, records, variables, and templates
- **Tags** — Freeform label vocabulary attached to zones and records for grouping and
  filtering
- **BIND Zone Export** — Export any zone as a standards-compliant BIND zone file
- **Bulk TTL Update** — Change TTL across multiple selected records in a single
  operation
- **Generic REST Provider** — Connect to HTTP APIs following the Meridian provider
  protocol; configured entirely through the admin UI with no code required
- **Subprocess Provider** — Execute a local binary as a DNS provider; the binary
  communicates via JSON over stdin/stdout
- **Provider Definition Management** — Create, edit, and delete provider type
  definitions through the admin UI
- **Expanded RBAC permissions** — 18 new permissions for templates, snippets, SOA presets,
  and provider definitions (62 total, up from 44 in 1.0)

### Security

- Server-side session invalidation on logout
- Password minimum length enforcement (8 characters minimum)
- Rate limiting applied to the change-password endpoint in addition to login
- Security response headers (`X-Content-Type-Options`, `X-Frame-Options`,
  `Referrer-Policy`) applied to all API and static responses
- Email format validation on user create and update endpoints
- Database credentials redacted from connection pool log output
- Thread-safe ISO 8601 timestamp formatting via shared `TimeUtils` utility;
  all call sites replaced from `std::gmtime` to `gmtime_r`
- PostgreSQL network isolation via explicit internal Docker bridge network

## [1.0.0] - 2026-03-18

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
- **Granular RBAC** — 44 discrete permissions collected into customizable roles;
  view-level and zone-level scoping is planned for a future release
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
