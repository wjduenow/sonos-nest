# One Dockerfile, two uses:
#  * standalone  — `docker compose up` / `docker build .` (BUILD_FROM defaults to python:3.12-slim)
#  * HA add-on   — the Supervisor builds this on-device, passing BUILD_FROM from build.yaml
# Entry is run.sh either way: it translates HA add-on options (/data/options.json) to env when
# present, and is a plain uvicorn launcher when not. No registry / prebuilt image required.
ARG BUILD_FROM=python:3.12-slim-bookworm
FROM ${BUILD_FROM}

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app
COPY run.sh /run.sh
RUN chmod +x /run.sh

ENV DATA_DIR=/data
ENV PORT=8000
VOLUME ["/data"]
EXPOSE 8000

# mDNS needs host networking (compose network_mode: host / add-on host_network: true) — the
# published port only matters for the dashboard when NOT on host networking.
CMD ["/run.sh"]
