# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Meridian DNS, please report it responsibly
via [GitHub Security Advisories](../../security/advisories/new).

**Do not** open a public GitHub issue for security vulnerabilities.

## What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact assessment
- Suggested fix (if any)

## Response Timeline

Meridian DNS is maintained by a single developer as a hobby project. Security reports
are taken seriously, but response timelines cannot be guaranteed. Best-effort
acknowledgment is typically within a few weeks.

## Scope

The following are in scope:
- Meridian DNS server application (`meridian-dns` binary)
- Web UI (Vue 3 frontend)
- API endpoints and authentication system
- Database schema and migration scripts
- Docker image and deployment configuration

The following are **out of scope:**
- Third-party DNS providers (Cloudflare, DigitalOcean, PowerDNS) — report to those vendors
- Issues in upstream dependencies (Crow, libpqxx, libgit2, etc.) — report to those projects
- Social engineering attacks

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 1.0.x   | ✅ (best effort)   |
| < 1.0   | ❌                 |

## Credits

Reporters are credited in the changelog (with permission) when a fix is released.
