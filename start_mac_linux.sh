#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
echo "Building Echo native C backend..."
cc -std=c11 -O2 -o echo_server backend/server.c backend/music.c backend/json.c
echo "Open http://localhost:8000 in your browser"
./echo_server
