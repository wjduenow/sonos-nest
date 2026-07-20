#!/usr/bin/env sh
# Add-on entrypoint: translate HA options (/data/options.json) into the env vars the app reads,
# then start it. Deliberately POSIX sh + python (both present in the base image) rather than
# bashio, so the base image carries no HA-specific dependency.
set -e

export DATA_DIR=/data      # HA gives every add-on a persistent /data; the registry lives here
export PORT=8000           # must match ingress_port in config.yaml

if [ -f /data/options.json ]; then
  HOST=$(python -c "import json; print((json.load(open('/data/options.json')).get('portal_host') or '').strip())" 2>/dev/null || true)
  if [ -n "$HOST" ]; then
    export PORTAL_HOST="$HOST"
  fi
fi

exec uvicorn app.main:app --host 0.0.0.0 --port "$PORT"
