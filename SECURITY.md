# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Meridian DNS, please report it responsibly.

**Email:** security@meridiandns.io

**Do not** open a public GitHub issue for security vulnerabilities.

## What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact assessment
- Suggested fix (if any)

## Response Timeline

- **Acknowledgment:** within 72 hours of report
- **Assessment:** within 7 days
- **Fix target:** within 90 days of confirmed vulnerability

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

| Version | Supported |
|---------|-----------|
| 1.0.x   | ✅        |
| < 1.0   | ❌        |

## Credits

We credit reporters in the changelog (with permission) when a fix is released.
