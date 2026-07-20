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
  opt() { python -c "import json,sys; v=json.load(open('/data/options.json')).get(sys.argv[1]); print(str(v).strip() if v is not None else '')" "$1" 2>/dev/null || true; }
  HOST=$(opt portal_host);     [ -n "$HOST" ]  && export PORTAL_HOST="$HOST"
  REPO=$(opt firmware_repo);   [ -n "$REPO" ]  && export FIRMWARE_REPO="$REPO"
  TOKEN=$(opt firmware_token); [ -n "$TOKEN" ] && export FIRMWARE_TOKEN="$TOKEN"
  POLL=$(opt firmware_poll);   [ -n "$POLL" ]  && export FIRMWARE_POLL="$POLL"
fi

exec uvicorn app.main:app --host 0.0.0.0 --port "$PORT"
