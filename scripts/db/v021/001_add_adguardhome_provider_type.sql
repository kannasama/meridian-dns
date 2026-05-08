-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (c) 2026 Meridian DNS Contributors
-- This file is part of Meridian DNS. See LICENSE for details.

-- Add AdGuard Home provider type to the provider_type enum
ALTER TYPE provider_type ADD VALUE IF NOT EXISTS 'adguardhome';
