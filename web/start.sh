#!/bin/sh
# OwO Input Method website - static hosting launcher for SimpFun.
#
# Port: SimpFun assigns the public port (39266 for this instance) and the
# container listens on that same port. If the platform injects a PORT
# environment variable it wins; otherwise fall back to 39266.
#
# Document root: prefer index.html next to this script. If the script sits in
# the server root while the site was uploaded into a web/ subdirectory, enter
# web/ instead. Serving the site directory (not the server root) means
# http://host:port/ shows the homepage directly and files such as .bashrc are
# never exposed.

PORT_TO_BIND="${PORT:-39266}"

echo "=== OwO website: binding 0.0.0.0:${PORT_TO_BIND} ==="
cd "$(dirname "$0")" || exit 1

if [ ! -f index.html ] && [ -f web/index.html ]; then
  cd web || exit 1
fi

if [ ! -f index.html ]; then
  echo "ERROR: index.html not found in $(pwd). Upload the site files first." >&2
  exit 1
fi

echo "serving directory: $(pwd)"

if command -v python3 >/dev/null 2>&1; then
  echo "runtime: python3"
  exec python3 -m http.server "${PORT_TO_BIND}" --bind 0.0.0.0
fi

if command -v python >/dev/null 2>&1; then
  echo "runtime: python"
  exec python -m http.server "${PORT_TO_BIND}" --bind 0.0.0.0
fi

if command -v npx >/dev/null 2>&1; then
  echo "runtime: npx serve"
  exec npx --yes serve -l "${PORT_TO_BIND}" .
fi

echo "ERROR: no python3 / python / npx found in this container." >&2
exit 1
