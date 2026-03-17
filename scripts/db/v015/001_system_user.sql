-- System user for automated operations (auto-capture, maintenance tasks).
-- Uses auth_method='system' to prevent login. Password hash is empty (no login possible).
INSERT INTO users (username, email, password_hash, is_active, auth_method)
VALUES ('_system', 'system@localhost', '', true, 'system')
ON CONFLICT (username) DO NOTHING;
