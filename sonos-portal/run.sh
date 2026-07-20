#!/usr/bin/env sh
# Container entrypoint for BOTH standalone and the HA add-on. Translates HA options
# (/data/options.json) into the env vars the app reads, then starts it. When there's no options
# file (standalone), it's just a uvicorn launcher. POSIX sh + python (both in the base image) —
# deliberately no bashio, so the base carries no HA-specific dependency.
set -e
cd /app

export DATA_DIR="${DATA_DIR:-/data}"   # HA gives every add-on a persistent /data; registry lives here
export PORT="${PORT:-8000}"            # must match ingress_port in config.yaml

if [ -f /data/options.json ]; then
  HOST=$(python -c "import json; print((json.load(open('/data/options.json')).get('portal_host') or '').strip())" 2>/dev/null || true)
  if [ -n "$HOST" ]; then
    export PORTAL_HOST="$HOST"
  fi
fi

exec uvicorn app.main:app --host 0.0.0.0 --port "$PORT"
