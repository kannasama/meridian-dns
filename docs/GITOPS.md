# GitOps Guide

## Overview

Meridian DNS treats Git repositories as first-class entities. After every deployment,
zone snapshots are committed to configured Git repositories, providing a full audit
trail and enabling infrastructure-as-code workflows.

## Adding a Git Repository

1. Navigate to **Git Repos** in the admin UI
2. Click **Add Repository**
3. Fill in the configuration:

| Field | Description |
|-------|-------------|
| Name | Display name (e.g. "Production DNS") |
| Remote URL | Git remote URL (SSH or HTTPS) |
| Auth Type | `ssh` or `https` |
| Default Branch | Branch for commits (default: `main`) |
| Enabled | Toggle to enable/disable syncing |

## SSH Authentication

### Setup

1. Generate an SSH key pair:
   ```bash
   ssh-keygen -t ed25519 -C "meridian-dns" -f meridian_deploy_key
   ```

2. Add the **public** key as a deploy key on your Git hosting platform
   (with write access)

3. In Meridian DNS, set:
   - **Auth Type:** `ssh`
   - **SSH Private Key:** Paste the private key content
   - **Known Hosts:** Paste the host key (e.g. `ssh-keyscan github.com`)

### Troubleshooting

- Ensure the deploy key has **write** access
- Verify known hosts match the remote server's key
- Check that the remote URL uses SSH format (`git@github.com:org/repo.git`)

## HTTPS Authentication

### Setup

1. Create a personal access token (PAT) on your Git hosting platform
2. In Meridian DNS, set:
   - **Auth Type:** `https`
   - **Username:** Your username or token name
   - **Token:** The personal access token

### Provider-Specific URLs

| Platform | URL Format |
|----------|-----------|
| GitHub | `https://github.com/org/repo.git` |
| GitLab | `https://gitlab.com/org/repo.git` |
| Bitbucket | `https://bitbucket.org/org/repo.git` |

## Branch Strategies

### Repository Default Branch

All repos have a default branch (typically `main`). Zone snapshots are committed
to this branch unless overridden at the zone level.

### Per-Zone Branch Override

Individual zones can specify a branch override, enabling patterns like:

| Zone | Branch | Use Case |
|------|--------|----------|
| `example.com` | `main` | Production DNS |
| `staging.example.com` | `staging` | Staging environment |
| `dev.example.com` | `develop` | Development environment |

## Zone Snapshot Format

After each deployment, zone state is committed as JSON:

```
{view_name}/{zone_name}.json
```

Example content:
```json
{
  "zone": "example.com",
  "view": "external",
  "deployed_at": "2026-03-15T12:00:00Z",
  "deployed_by": "admin",
  "records": [
    {"name": "example.com", "type": "A", "content": "1.2.3.4", "ttl": 300},
    {"name": "www.example.com", "type": "CNAME", "content": "example.com", "ttl": 300}
  ]
}
```

## Zone Capture

### Auto-Capture

When a zone exists on a provider but has never been deployed through Meridian DNS,
the system can automatically capture a baseline snapshot. This happens during the
sync check cycle.

### Manual Capture

For on-demand snapshots:
1. Navigate to **Zones** → select zone
2. Click **Capture Snapshot**

This fetches current records from the provider and creates a deployment record
with `generated_by: "manual-capture"`.

## Config Backup via GitOps

System configuration backups can be committed to a Git repository:

1. Navigate to **Settings** → set `backup.git_repo_id` to a configured repository
2. Optionally set `backup.auto_interval_seconds` for scheduled backups
3. Manual backups: **Backup & Restore** → **Export** → committed to the configured repo

## Test Connection

Before saving a new repository, use the **Test Connection** button to verify:
- Remote URL is reachable
- Authentication credentials are valid
- Write access is available (attempts a no-op push)
