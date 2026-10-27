#!/bin/sh
# The C server reads/writes ./data/state.json and ./media/*.
# Point both at the persistent disk so uploaded songs survive restarts and redeploys.
set -e
STORAGE="${STORAGE_DIR:-/var/data}"
mkdir -p "$STORAGE/data" "$STORAGE/media"
rm -rf /app/data /app/media
ln -s "$STORAGE/data" /app/data
ln -s "$STORAGE/media" /app/media
echo "Echo storage: $STORAGE (data/state.json + media/)"
exec ./echo_server
