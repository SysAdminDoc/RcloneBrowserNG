#!/bin/sh
# Starts the X display, the window manager and the app, then serves the
# display to browsers on port 6080. The app comes back if it's closed, so the
# page never goes blank.
#
# VNC_PASSWORD  asks the browser for this password before it connects
# WIDTH/HEIGHT  starting desktop size; the page resizes it to the browser
set -eu

width="${WIDTH:-1280}"
height="${HEIGHT:-800}"
mkdir -p "$HOME/.vnc"

if [ -n "${VNC_PASSWORD:-}" ]; then
  printf '%s\n' "$VNC_PASSWORD" | vncpasswd -f >"$HOME/.vnc/passwd"
  chmod 600 "$HOME/.vnc/passwd"
  set -- -SecurityTypes VncAuth -rfbauth "$HOME/.vnc/passwd"
else
  set -- -SecurityTypes None
fi

# -localhost keeps the VNC port inside the container. Only websockify talks
# to it.
Xvnc :1 -geometry "${width}x${height}" -depth 24 -rfbport 5901 -localhost \
  -AlwaysShared -desktop "Rclone Browser" "$@" &

tries=0
until [ -S /tmp/.X11-unix/X1 ]; do
  tries=$((tries + 1))
  if [ "$tries" -gt 100 ]; then
    echo "Xvnc didn't start" >&2
    exit 1
  fi
  sleep 0.1
done

openbox &

(
  while :; do
    rclone-browser
    sleep 1
  done
) &

exec websockify --web /usr/share/novnc 6080 localhost:5901
