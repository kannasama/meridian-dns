-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- v013: SAML extensions for lasso integration

-- Track SAML session index for Single Logout support.
-- When a user authenticates via SAML, the IdP includes a SessionIndex in the
-- AuthnStatement. We store it so that IdP-initiated SLO can locate and destroy
-- the correct local session(s).
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS saml_session_index TEXT;

CREATE INDEX IF NOT EXISTS idx_sessions_saml_session_index
  ON sessions (saml_session_index)
  WHERE saml_session_index IS NOT NULL;
