#!/usr/bin/env bash
# Launches Visona in kiosk mode and disables screen blanking on X11.
set -euo pipefail

if [[ -z "${DISPLAY:-}" ]]; then
    export DISPLAY=:0
fi

if command -v xset >/dev/null 2>&1; then
    xset -dpms || true
    xset s off || true
    xset s noblank || true
fi

# Hide the desktop panel so Visona can cover the whole display (best effort per Pi OS flavour).
# Bookworm/Trixie labwc uses wf-panel-pi; older LXDE uses lxpanel. install-kiosk.sh disables
# wf-panel-pi autostart; this kills any panel still running before launch.
if command -v lxpanelctl >/dev/null 2>&1; then
    lxpanelctl hide || true
fi
if command -v wfpanelctl >/dev/null 2>&1; then
    wfpanelctl hide 2>/dev/null || true
fi
pkill -f 'lwrespawn /usr/bin/wf-panel-pi' 2>/dev/null || true
killall wf-panel-pi 2>/dev/null || true
pkill -x wf-panel 2>/dev/null || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# The release tarball has the binary next to scripts/; a source build has it under build/pi.
if [[ -z "${VISONA_BIN:-}" && -x "${REPO_ROOT}/Visona" ]]; then
    BIN="${REPO_ROOT}/Visona"
else
    BIN="${VISONA_BIN:-${REPO_ROOT}/build/pi/app/Visona_artefacts/Release/Visona}"
fi

if [[ ! -x "${BIN}" ]]; then
    echo "Visona binary not found or not executable: ${BIN}" >&2
    echo "Unpack the release tarball here, or build: cmake --preset pi && cmake --build --preset pi" >&2
    exit 1
fi

exec "${BIN}" --kiosk "$@"
