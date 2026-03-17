# Deployment Guide

## Prerequisites

- Docker 24+ and Docker Compose v2
- PostgreSQL 15+ (included in Docker Compose, or bring your own)
- 512 MB RAM minimum, 1 GB recommended
- Reverse proxy with TLS (nginx, Traefik, or Caddy) for production

## Quick Start with Docker Compose

### 1. Create environment file

```bash
cp .env.example .env
```

Generate the required secrets:

```bash
# Generate master key (AES-256 encryption key for provider tokens, credentials)
sed -i "s/^DNS_MASTER_KEY=.*/DNS_MASTER_KEY=$(openssl rand -hex 32)/" .env

# Generate JWT signing secret
sed -i "s/^DNS_JWT_SECRET=.*/DNS_JWT_SECRET=$(openssl rand -hex 32)/" .env
```

### 2. Review the Compose file

The default [`docker-compose.yml`](../docker-compose.yml) ships with the project:

```yaml
services:
  db:
    image: postgres:16-alpine
    environment:
      POSTGRES_DB: meridian_dns
      POSTGRES_USER: dns
      POSTGRES_PASSWORD: ${DNS_DB_PASSWORD:-dns_dev_password}
    volumes:
      - pgdata:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U dns -d meridian_dns"]
      interval: 5s
      timeout: 3s
      retries: 5

  app:
    build: .
    depends_on:
      db:
        condition: service_healthy
    environment:
      DNS_DB_URL: postgresql://dns:${DNS_DB_PASSWORD:-dns_dev_password}@db:5432/meridian_dns
      DNS_MASTER_KEY: ${DNS_MASTER_KEY}
      DNS_JWT_SECRET: ${DNS_JWT_SECRET}
      DNS_HTTP_PORT: "8080"
      DNS_AUDIT_STDOUT: "true"
      DNS_LOG_LEVEL: "${DNS_LOG_LEVEL:-info}"
    ports:
      - "${DNS_HTTP_PORT:-8080}:8080"
    volumes:
      - meridian-data:/var/meridian-dns

volumes:
  pgdata:
  meridian-data:
```

### 3. Start the stack

```bash
docker compose up -d
```

This starts PostgreSQL 16 and Meridian DNS on port 8080.

### 4. Complete setup

Open `http://localhost:8080` in your browser. The setup wizard will guide you through
creating the initial admin account and configuring your first DNS provider.

## Environment Variables

### Required (env-only)

| Variable | Description |
|----------|-------------|
| `DNS_DB_URL` | PostgreSQL connection string (set automatically in Docker Compose) |
| `DNS_MASTER_KEY` | 64-char hex string for AES-256-GCM encryption of secrets |
| `DNS_JWT_SECRET` | JWT signing secret (≥32 characters) |

Secret variables support `_FILE` variants for Docker secrets:

```yaml
environment:
  DNS_MASTER_KEY_FILE: /run/secrets/master_key
  DNS_JWT_SECRET_FILE: /run/secrets/jwt_secret
```

### Optional (env-only)

| Variable | Default | Description |
|----------|---------|-------------|
| `DNS_HTTP_PORT` | `8080` | HTTP listen port |
| `DNS_LOG_LEVEL` | `info` | Log level: `trace`, `debug`, `info`, `warn`, `error` |
| `DNS_DB_POOL_SIZE` | `10` | Database connection pool size |

All other settings are configurable at runtime via the Settings UI or API. See
[CONFIGURATION.md](CONFIGURATION.md) for the complete reference.

## First-Run Setup

On first launch, Meridian DNS enters setup mode:

1. **Create admin account** — Set username, email, and password
2. **Configure provider** — Add your first DNS provider (PowerDNS, Cloudflare, or DigitalOcean)
3. **Create view and zone** — Set up your first split-horizon view and DNS zone

The setup wizard is available for 30 minutes after startup. After setup, normal
authentication is required.

## Container Registry

### Docker Hub

```bash
docker pull kannasama/meridian-dns:1.0.0
docker pull kannasama/meridian-dns:latest
```

### GitHub Container Registry

```bash
docker pull ghcr.io/meridiandns/meridian-dns:1.0.0
```

### Tag Format

| Tag | Description |
|-----|-------------|
| `1.0.0` | Exact version |
| `1.0` | Latest patch in 1.0.x |
| `1` | Latest minor in 1.x.x |
| `latest` | Latest stable release |

## Database Backup

Meridian DNS stores all configuration in PostgreSQL. Regular DB backups are
recommended in addition to the in-app config backup feature.

```bash
# Backup
docker compose exec db pg_dump -U dns meridian_dns > backup_$(date +%Y%m%d).sql

# Restore
docker compose exec -T db psql -U dns meridian_dns < backup_20260315.sql
```

## Reverse Proxy — nginx

```nginx
server {
    listen 443 ssl http2;
    server_name dns.example.com;

    ssl_certificate     /etc/ssl/certs/dns.example.com.pem;
    ssl_certificate_key /etc/ssl/private/dns.example.com.key;

    # Security headers (recommended for production)
    add_header Strict-Transport-Security "max-age=63072000; includeSubDomains" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-Frame-Options "DENY" always;
    add_header X-XSS-Protection "0" always;
    add_header Referrer-Policy "strict-origin-when-cross-origin" always;
    add_header Permissions-Policy "camera=(), microphone=(), geolocation=()" always;
    add_header Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'" always;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

## Reverse Proxy — Traefik

```yaml
# docker-compose.override.yml
services:
  app:
    labels:
      - "traefik.enable=true"
      - "traefik.http.routers.meridian.rule=Host(`dns.example.com`)"
      - "traefik.http.routers.meridian.tls=true"
      - "traefik.http.routers.meridian.tls.certresolver=letsencrypt"
      - "traefik.http.services.meridian.loadbalancer.server.port=8080"
      # Security headers middleware
      - "traefik.http.middlewares.security-headers.headers.stsSeconds=63072000"
      - "traefik.http.middlewares.security-headers.headers.stsIncludeSubdomains=true"
      - "traefik.http.middlewares.security-headers.headers.contentTypeNosniff=true"
      - "traefik.http.middlewares.security-headers.headers.frameDeny=true"
      - "traefik.http.middlewares.security-headers.headers.referrerPolicy=strict-origin-when-cross-origin"
      - "traefik.http.middlewares.security-headers.headers.permissionsPolicy=camera=(), microphone=(), geolocation=()"
      - "traefik.http.routers.meridian.middlewares=security-headers"
```

## Upgrading

1. Pull the new image:
   ```bash
   docker compose pull app
   ```

2. Restart with migrations:
   ```bash
   docker compose up -d
   ```

   Migrations run automatically on startup via the entrypoint script.

3. Verify:
   ```bash
   docker compose exec app meridian-dns --version
   curl -s http://localhost:8080/api/v1/health/live | jq
   ```

## Health Checks

### Liveness Probe

```
GET /api/v1/health/live
```

Returns `200 OK` with `{"status": "alive", "version": "1.0.0"}` if the process is
running. No external dependency checks.

### Readiness Probe

```
GET /api/v1/health/ready
```

Returns `200 OK` when the database is reachable and the application is ready to
serve requests. Returns `503` if the database is unreachable.

### Kubernetes Example

```yaml
livenessProbe:
  httpGet:
    path: /api/v1/health/live
    port: 8080
  initialDelaySeconds: 10
  periodSeconds: 30

readinessProbe:
  httpGet:
    path: /api/v1/health/ready
    port: 8080
  initialDelaySeconds: 15
  periodSeconds: 10
```
