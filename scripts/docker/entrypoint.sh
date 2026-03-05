#!/bin/sh
set -e

# Run DB migrations before starting the server
meridian-dns --migrate

exec "$@"
