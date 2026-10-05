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

Installs ~/.config/autostart/visona-kiosk.desktop so Visona starts at login,
and turns off labwc's touch mouse emulation so multitouch (pinch) works.
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

# Raspberry Pi OS sets mouseEmulation="yes" for touchscreens in labwc, which turns every touch
# into mouse events: a tap and a drag work, but a two-finger pinch never reaches Visona. Turn the
# emulation off for this user. labwc reads only the first rc.xml it finds, so the user's copy
# starts from the system one.
LABWC_SYSTEM_RC="/etc/xdg/labwc/rc.xml"
LABWC_RC="${HOME}/.config/labwc/rc.xml"
if [[ -d /etc/xdg/labwc ]] || command -v labwc >/dev/null 2>&1; then
    mkdir -p "$(dirname "${LABWC_RC}")"
    if [[ ! -f "${LABWC_RC}" ]]; then
        if [[ -f "${LABWC_SYSTEM_RC}" ]]; then
            cp "${LABWC_SYSTEM_RC}" "${LABWC_RC}"
        else
            printf '<?xml version="1.0"?>\n<openbox_config xmlns="http://openbox.org/3.4/rc">\n</openbox_config>\n' >"${LABWC_RC}"
        fi
    elif [[ ! -f "${LABWC_RC}.visona-kiosk.bak" ]]; then
        echo "Backing up ${LABWC_RC} to ${LABWC_RC}.visona-kiosk.bak"
        cp "${LABWC_RC}" "${LABWC_RC}.visona-kiosk.bak"
    fi
    if grep -q '<touch[ />]' "${LABWC_RC}"; then
        sed -i -e 's/\(<touch [^>]*mouseEmulation="\)yes"/\1no"/g' \
            -e '/<touch[ />]/{/mouseEmulation=/!s|<touch\([ />]\)|<touch mouseEmulation="no"\1|}' \
            "${LABWC_RC}"
    elif grep -q '</openbox_config>' "${LABWC_RC}"; then
        sed -i 's|</openbox_config>|  <touch mouseEmulation="no"/>\n</openbox_config>|' "${LABWC_RC}"
    elif grep -q '</labwc_config>' "${LABWC_RC}"; then
        sed -i 's|</labwc_config>|  <touch mouseEmulation="no"/>\n</labwc_config>|' "${LABWC_RC}"
    else
        echo "Could not turn off touch mouse emulation in ${LABWC_RC}; pinch zoom needs" >&2
        echo '<touch mouseEmulation="no"/> there. See docs/pi-kiosk.md.' >&2
    fi
    echo "Touchscreen multitouch enabled in ${LABWC_RC} (takes effect after a reboot)"
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
2. Use the Babyface Pro FS in Class Compliant mode. It can be plugged in before or after boot.
3. To disable kiosk autostart: rm ~/.config/autostart/visona-kiosk.desktop

See docs/pi-kiosk.md for details.
EOF
