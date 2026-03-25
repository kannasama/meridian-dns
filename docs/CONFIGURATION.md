# Configuration Reference

## Overview

Meridian DNS uses a hybrid configuration model:

- **Environment-only variables** — Secrets and bootstrap config that must be set
  before the application starts. These cannot be changed at runtime.
- **Database-configurable settings** — All other settings stored in the
  `system_config` table. Manageable via the Settings UI or REST API.

## Environment-Only Variables

### Required

| Variable | Description |
|----------|-------------|
| `DNS_DB_URL` | PostgreSQL connection string (e.g. `postgresql://user:pass@host:5432/dbname`) |
| `DNS_MASTER_KEY` | 64-character hex string for AES-256-GCM encryption |
| `DNS_JWT_SECRET` | JWT signing secret (minimum 32 characters) |

### Secret File Variants

For container orchestrators that mount secrets as files:

| Variable | File Variant |
|----------|-------------|
| `DNS_MASTER_KEY` | `DNS_MASTER_KEY_FILE` |
| `DNS_JWT_SECRET` | `DNS_JWT_SECRET_FILE` |

When the `_FILE` variant is set, the application reads the secret from the specified
file path and trims whitespace.

### Optional

| Variable | Default | Description |
|----------|---------|-------------|
| `DNS_HTTP_PORT` | `8080` | HTTP listen port |
| `DNS_LOG_LEVEL` | `info` | Log level (`trace`, `debug`, `info`, `warn`, `error`) |
| `DNS_DB_POOL_SIZE` | `10` | Database connection pool size |
| `DNS_JWT_TTL_SECONDS` | `28800` | JWT token TTL (8 hours) |
| `DNS_JWT_ALGORITHM` | `HS256` | JWT signing algorithm |
| `DNS_THREAD_POOL_SIZE` | `0` | Background thread pool (0 = auto-detect CPU count) |

## Database-Configurable Settings

These settings are stored in the `system_config` database table. On first run,
values are seeded from environment variables (if set). After seeding, the database
value takes precedence — changes are made via Settings UI or API.

### Session & Security

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `session.absolute_ttl_seconds` | `DNS_SESSION_ABSOLUTE_TTL_SECONDS` | `86400` | No | Session absolute TTL (24h) |
| `session.cleanup_interval_seconds` | `DNS_SESSION_CLEANUP_INTERVAL_SECONDS` | `3600` | No | Session cleanup interval |
| `apikey.cleanup_grace_seconds` | `DNS_API_KEY_CLEANUP_GRACE_SECONDS` | `300` | No | API key cleanup grace period |
| `apikey.cleanup_interval_seconds` | `DNS_API_KEY_CLEANUP_INTERVAL_SECONDS` | `3600` | No | API key cleanup interval |

### Application

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `app.base_url` | `DNS_BASE_URL` | *(empty)* | No | Base URL for OIDC/SAML callbacks |
| `http.threads` | `DNS_HTTP_THREADS` | `4` | **Yes** | HTTP server thread count |
| `ui.dir` | `DNS_UI_DIR` | *(empty)* | **Yes** | Path to built UI assets |
| `migrations.dir` | `DNS_MIGRATIONS_DIR` | `/opt/meridian-dns/db` | **Yes** | Migration scripts directory |

### Deployment & Sync

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `deployment.retention_count` | `DNS_DEPLOYMENT_RETENTION_COUNT` | `10` | No | Snapshots retained per zone |
| `sync.check_interval_seconds` | `DNS_SYNC_CHECK_INTERVAL` | `3600` | No | Zone sync check interval (0 = disabled) |

### Audit

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `audit.db_url` | `DNS_AUDIT_DB_URL` | *(empty)* | **Yes** | Separate audit DB URL |
| `audit.stdout` | `DNS_AUDIT_STDOUT` | `false` | No | Mirror audit to stdout |
| `audit.retention_days` | `DNS_AUDIT_RETENTION_DAYS` | `365` | No | Audit log retention |
| `audit.purge_interval_seconds` | `DNS_AUDIT_PURGE_INTERVAL_SECONDS` | `86400` | No | Audit purge interval |

### System Logs

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `system_log.retention_days` | — | `30` | No | System log retention in days |
| `system_log.purge_interval_seconds` | — | `86400` | No | System log purge interval |

### GitOps

| Setting Key | Env Seed | Default | Restart | Description |
|-------------|----------|---------|---------|-------------|
| `gitops.base_path` | `DNS_GITOPS_BASE_PATH` | `/var/meridian-dns/repos` | **Yes** | Local clone base directory |
| `backup.git_repo_id` | `DNS_BACKUP_GIT_REPO_ID` | `0` | No | Git repo for backup commits (0 = disabled) |
| `backup.auto_interval_seconds` | `DNS_BACKUP_AUTO_INTERVAL_SECONDS` | `0` | No | Auto-backup interval (0 = disabled) |

## Seeding Behavior

On first run (empty `system_config` table):

1. Each setting is checked for a corresponding environment variable
2. If the env var is set, its value is inserted into `system_config`
3. If the env var is not set, the default value from the setting definition is used

After seeding, the database value is authoritative. Environment variables are not
re-read on subsequent starts (except for the environment-only variables listed above).

## Restart-Required Settings

Settings marked **Yes** in the Restart column require an application restart to take
effect. The Settings UI indicates which settings need a restart.

- `http.threads` — Thread pool is created at startup
- `ui.dir` — Static file handler initializes at startup
- `migrations.dir` — Migration runner uses this at startup
- `audit.db_url` — Audit database connection is established at startup
- `gitops.base_path` — Git repository paths are resolved at startup

## API Reference

### List all settings

```
GET /api/v1/settings
Authorization: Bearer <token>
```

Requires `settings.view` permission.

### Update settings

```
PUT /api/v1/settings
Authorization: Bearer <token>
Content-Type: application/json

{
  "settings": [
    {"key": "audit.retention_days", "value": "180"},
    {"key": "deployment.retention_count", "value": "20"}
  ]
}
```

Requires `settings.edit` permission.
