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

Run from the unpacked release tarball, or from the repository root after
building with the pi preset.
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
# The release tarball has the binary next to scripts/; a source build has it under build/pi.
if [[ -x "${REPO_ROOT}/Visona" ]]; then
    BIN="${REPO_ROOT}/Visona"
else
    BIN="${REPO_ROOT}/build/pi/app/Visona_artefacts/Release/Visona"
fi

if [[ ! -x "${BIN}" ]]; then
    echo "Visona not found. Unpack the release tarball here, or build first:" >&2
    echo "  cmake --preset pi && cmake --build --preset pi" >&2
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

# Raspberry Pi OS Bookworm/Trixie (labwc) draws the menubar with wf-panel-pi, which stays
# above XWayland windows. Comment out its autostart entry so the panel does not respawn.
LABWC_AUTOSTART="/etc/xdg/labwc/autostart"
LABWC_BACKUP="${LABWC_AUTOSTART}.visona-kiosk.bak"
if [[ -f "${LABWC_AUTOSTART}" ]] && grep -q 'wf-panel-pi' "${LABWC_AUTOSTART}"; then
    if [[ ! -f "${LABWC_BACKUP}" ]]; then
        echo "Backing up ${LABWC_AUTOSTART} to ${LABWC_BACKUP}"
        sudo cp "${LABWC_AUTOSTART}" "${LABWC_BACKUP}"
    fi
    if grep -q '^[^#].*wf-panel-pi' "${LABWC_AUTOSTART}"; then
        echo "Disabling wf-panel-pi autostart in ${LABWC_AUTOSTART}"
        sudo sed -i '/wf-panel-pi/s/^/#/' "${LABWC_AUTOSTART}"
    fi
    pkill -f 'lwrespawn /usr/bin/wf-panel-pi' 2>/dev/null || true
    killall wf-panel-pi 2>/dev/null || true
fi

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
