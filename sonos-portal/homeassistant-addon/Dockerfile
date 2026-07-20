# The add-on is the SAME base image + a run.sh that translates HA options → env. BUILD_FROM is
# the base sonos-portal image (built from ../Dockerfile). Publish it to a registry HA can pull
# (e.g. ghcr.io/wjduenow/sonos-portal), or build+tag it locally on the HA host — see README.
ARG BUILD_FROM=ghcr.io/wjduenow/sonos-portal:latest
FROM ${BUILD_FROM}

# WORKDIR /app and the installed deps + app/ all come from the base image.
COPY run.sh /run.sh
RUN chmod +x /run.sh
CMD ["/run.sh"]
