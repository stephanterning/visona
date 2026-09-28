#!/usr/bin/env bash
# Install Visona kiosk autostart for the current desktop user on Raspberry Pi OS.
set -euo pipefail

ENABLE_AUTOLOGIN=false
for arg in "$@"; do
    case "${arg}" in
        --enable-autologin) ENABLE_AUTOLOGIN=true ;;
        -h | --help)
            cat <<'EOF'
Usage: install-kiosk.sh [--enable-autologin]

Installs ~/.config/autostart/visona-kiosk.desktop so Visona starts at login.
Optionally enables desktop autologin via raspi-config (requires sudo).

Run from the repository root after building with the pi preset.
EOF
            exit 0
            ;;
        *)
            echo "Unknown option: ${arg}" >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
WRAPPER="${REPO_ROOT}/scripts/pi/visona-kiosk.sh"
BIN="${REPO_ROOT}/build/pi/app/Visona_artefacts/Release/Visona"

if [[ ! -x "${BIN}" ]]; then
    echo "Build Visona first: cmake --preset pi && cmake --build --preset pi" >&2
    exit 1
fi

chmod +x "${WRAPPER}"

AUTOSTART_DIR="${HOME}/.config/autostart"
mkdir -p "${AUTOSTART_DIR}"

DESKTOP_FILE="${AUTOSTART_DIR}/visona-kiosk.desktop"
cat >"${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Name=Visona
Comment=Beat-synced stereo oscilloscope
Exec=${WRAPPER}
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

chmod 644 "${DESKTOP_FILE}"

echo "Installed ${DESKTOP_FILE}"
echo "Wrapper: ${WRAPPER}"
echo "Binary:  ${BIN}"

if [[ "${ENABLE_AUTOLOGIN}" == true ]]; then
    if command -v raspi-config >/dev/null 2>&1; then
        echo "Enabling desktop autologin (raspi-config B4)..."
        sudo raspi-config nonint do_boot_behaviour B4
    else
        echo "raspi-config not found; enable desktop autologin manually." >&2
    fi
fi

cat <<'EOF'

Next steps:
1. Reboot the Pi (or log out and back in).
2. Connect the Babyface Pro FS in Class Compliant mode before boot, if you want audio at startup.
3. To disable kiosk autostart: rm ~/.config/autostart/visona-kiosk.desktop

See docs/pi-kiosk.md for details.
EOF
