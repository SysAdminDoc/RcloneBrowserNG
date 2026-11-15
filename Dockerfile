# Runs Rclone Browser NG on a machine with no desktop and serves it to a web
# browser. Xvnc gives the app an X display, openbox manages its windows, xterm
# runs `rclone config` when the app asks for a terminal, and noVNC turns the
# VNC display into a page on port 6080.
#
#   docker build -t rclone-browser .
#   docker run -d -p 127.0.0.1:6080:6080 -v rb-config:/config -v /srv:/data rclone-browser
#
# then open http://localhost:6080. Settings and rclone.conf live in /config.

FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake qt6-base-dev libgl1-mesa-dev \
      ca-certificates curl unzip \
 && rm -rf /var/lib/apt/lists/*

# rclone from rclone.org, since Ubuntu's package is older than the release
# the app treats as the security floor.
ARG RCLONE_VERSION=1.76.0
ARG TARGETARCH=amd64
RUN set -eu; \
    zip="rclone-v${RCLONE_VERSION}-linux-${TARGETARCH}.zip"; \
    cd /tmp; \
    curl -fsSLO "https://downloads.rclone.org/v${RCLONE_VERSION}/${zip}"; \
    curl -fsSLO "https://downloads.rclone.org/v${RCLONE_VERSION}/SHA256SUMS"; \
    grep " ${zip}\$" SHA256SUMS | sha256sum -c -; \
    unzip -q "${zip}"; \
    install -m 0755 "rclone-v${RCLONE_VERSION}-linux-${TARGETARCH}/rclone" /usr/local/bin/rclone

COPY . /src
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
 && cmake --build /build --parallel "$(nproc)" \
 && cmake --install /build --strip

FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libqt6widgets6t64 libqt6network6t64 qt6-qpa-plugins \
      tigervnc-standalone-server tigervnc-tools openbox xterm \
      novnc websockify tini \
      fonts-dejavu-core ca-certificates fuse3 \
 && rm -rf /var/lib/apt/lists/* \
 && printf '%s\n' '<!doctype html>' \
      '<meta http-equiv="refresh" content="0; url=vnc.html?autoconnect=1&amp;resize=remote&amp;reconnect=1">' \
      > /usr/share/novnc/index.html

COPY --from=build /usr/local/bin/rclone /usr/local/bin/rclone
COPY --from=build /usr/local/bin/rclone-browser /usr/local/bin/rclone-browser
COPY --from=build /usr/local/share/icons /usr/local/share/icons
COPY scripts/docker-entrypoint.sh /usr/local/bin/rclone-browser-web

RUN chmod 0755 /usr/local/bin/rclone-browser-web \
 && userdel --remove ubuntu 2>/dev/null || true \
 && useradd --uid 1000 --create-home --home-dir /config --shell /bin/sh app \
 && mkdir -p /data \
 && chown app:app /data

USER app
ENV HOME=/config \
    DISPLAY=:1
VOLUME ["/config"]
EXPOSE 6080
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/rclone-browser-web"]
