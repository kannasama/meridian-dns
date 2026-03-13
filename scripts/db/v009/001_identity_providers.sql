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
