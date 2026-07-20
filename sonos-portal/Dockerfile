# The one image, used both standalone and as the base for the Home Assistant add-on.
FROM python:3.12-slim

WORKDIR /app

# Deps first for layer caching.
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app

# Registry JSON lives here; mount a volume (compose) or map it (HA add-on) to persist it.
ENV DATA_DIR=/data
ENV PORT=8000
VOLUME ["/data"]
EXPOSE 8000

# NOTE: mDNS needs host networking (see docker-compose.yml / the add-on manifest) — the published
# port matters only for the dashboard when NOT on host networking; discovery won't work there.
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
