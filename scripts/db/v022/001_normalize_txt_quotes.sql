-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Normalise TXT record values to the canonical unquoted form.
--
-- The PowerDNS provider's read path stripped surrounding quotes while its write
-- path sent values verbatim, so unquoted TXT was rejected by the API. Operators
-- worked around that by storing the value already quoted, which then never
-- matched the parsed live value and showed as permanent drift.
--
-- The provider now owns the wire format: it quotes, escapes and chunks on write
-- and parses on read. Stored values should therefore be the raw text.
--
-- Only single fully-quoted values are rewritten here. A multi-chunk value such
-- as '"a" "b"' is left alone: splitting it correctly requires the escape-aware
-- parser, and the provider normalises those at the boundary anyway, so they no
-- longer produce drift.
UPDATE records
SET value_template = substring(value_template FROM 2 FOR length(value_template) - 2)
WHERE type = 'TXT'
  AND length(value_template) >= 2
  AND value_template LIKE '"%'
  AND value_template LIKE '%"'
  -- Skip multi-chunk presentation values; the provider handles those.
  AND substring(value_template FROM 2 FOR length(value_template) - 2) NOT LIKE '%"%';
