# Meridian DNS

A self-hosted, multi-provider DNS management platform with split-horizon views,
GitOps integration, and a deployment pipeline — built for operators who manage DNS
across PowerDNS, Cloudflare, and DigitalOcean from a single interface.

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](LICENSE)

## Screenshots

> Screenshots will be captured from a running themed instance before release.
> See `docs/screenshots/` once available.

## Features

### Core

- **Multi-provider orchestration** — Manage PowerDNS, Cloudflare, and DigitalOcean
  through a unified API and UI with an extensible provider interface
- **Split-horizon views** — Define internal and external views for the same domain
  with strict record isolation
- **Variable templates** — Store records with `{{variable}}` placeholders; update a
  variable once and propagate to all referencing records
- **Preview-before-deploy** — Diff staged changes against live provider state,
  detect drift, and review before pushing
- **Deployment snapshots and rollback** — Every push captures a full zone snapshot;
  roll back to any previous state (full zone or cherry-picked records)
- **Batch record import** — CSV, JSON, DNSControl, and provider import with preview

### Operations

- **Multi-repo GitOps** — Zone snapshots committed to configured Git repositories
  with SSH/HTTPS auth and per-zone branch support
- **Zone capture** — Import existing DNS records from providers without deploying
  through the pipeline
- **Config backup and restore** — Full system export/import with preview mode
- **Audit trail** — Every mutation logged with before/after state, NDJSON export,
  configurable retention
- **Health probes** — Liveness and readiness endpoints for container orchestrators

### Security

- **Granular RBAC** — 44 discrete permissions in customizable roles with
  view-level and zone-level scoping
- **OIDC and SAML 2.0** — Federated login with auto-provisioning and IdP group mapping
- **API key authentication** — Programmatic access with one-time key display
- **AES-256-GCM encryption** — Provider tokens, Git credentials, and IdP secrets
  encrypted at rest
- **Argon2id password hashing** — Memory-hard password storage
- **HMAC-SHA256 JWT sessions** — Sliding and absolute TTL with server-side tracking

### UI

- **Vue 3 + PrimeVue** — Responsive web interface with deployment diff viewer
  and variable autocomplete
- **23 theme presets** — 14 dark themes (Catppuccin Mocha, Dracula, Nord, Tokyo Night,
  etc.) and 9 light themes with independent accent color customization
- **Database-backed settings** — Runtime configuration via Settings UI

## Quick Start

### Docker Compose (recommended)

```bash
# 1. Create environment file
cp .env.example .env

# Generate required secrets
echo "DNS_MASTER_KEY=$(openssl rand -hex 32)" >> .env
echo "DNS_JWT_SECRET=$(openssl rand -hex 32)" >> .env

# 2. Start the stack
docker compose up -d

# 3. Open browser
open http://localhost:8080
```

The setup wizard guides you through creating an admin account and configuring your
first DNS provider.

### From Source

See [BUILD_ENVIRONMENT.md](docs/BUILD_ENVIRONMENT.md) for full build instructions.
Docker is the recommended build method — native builds require Fedora 43 or
Arch Linux with matching dependencies.

## Documentation

| Document | Description |
|----------|-------------|
| [Deployment Guide](docs/DEPLOYMENT.md) | Docker Compose, reverse proxy, upgrading, health checks |
| [Configuration Reference](docs/CONFIGURATION.md) | Environment variables and DB-configurable settings |
| [Authentication Guide](docs/AUTHENTICATION.md) | Local, OIDC, SAML, and API key authentication |
| [GitOps Guide](docs/GITOPS.md) | Multi-repo setup, branch strategies, zone snapshots |
| [Permissions Guide](docs/PERMISSIONS.md) | RBAC model, roles, scoping, resolution logic |
| [Architecture](docs/ARCHITECTURE.md) | System design, components, data flows |
| [Build Environment](docs/BUILD_ENVIRONMENT.md) | Docker and native build setup |
| [Code Standards](docs/CODE_STANDARDS.md) | C++20 conventions, naming, formatting |
| [OpenAPI Spec](docs/openapi.yaml) | Complete REST API specification |

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 (`-Wall -Wextra -Wpedantic -Werror`) |
| Build | CMake 3.20+ / Ninja |
| HTTP | Crow v1.3.1 (FetchContent) |
| Database | PostgreSQL 16 via libpqxx |
| Crypto | OpenSSL (AES-256-GCM, HMAC-SHA256 JWT, Argon2id) |
| Git | libgit2 |
| SAML | lasso + xmlsec1 |
| OIDC | liboauth2 + cjose (built from source) |
| Logging | spdlog |
| JSON | nlohmann/json |
| Testing | Google Test / Google Mock (FetchContent) |
| Frontend | Vue 3 + TypeScript + Vite |
| UI Components | PrimeVue (Aura preset) |
| State | Pinia |
| Routing | Vue Router 4 |
| Container | Multi-stage Docker (Fedora 43, ~180 MB runtime) |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the contribution process, code standards,
and pull request guidelines.

## License

Meridian DNS is licensed under the
[GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0-or-later).
