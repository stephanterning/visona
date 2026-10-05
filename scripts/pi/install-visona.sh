#!/usr/bin/env bash
# Download a Visona Pi tarball, unpack it, and run install-kiosk.sh for the current user.
set -euo pipefail

REPO=stephanterning/visona
DEFAULT_RELEASE_URL="https://github.com/${REPO}/releases/latest/download/Visona-linux-arm64-pi.tar.gz"
INSTALL_DIR="${HOME}/visona"

ENABLE_AUTOLOGIN=false
NO_REBOOT=false
TARBALL_URL=""

usage() {
    cat <<EOF
Usage: install-visona.sh [options] [tarball-url]

Downloads the Raspberry Pi arm64 tarball, unpacks it to ${INSTALL_DIR}, and runs
install-kiosk.sh so Visona starts in kiosk mode at login (with touch multitouch on labwc).

With no tarball-url, installs the latest published GitHub release:
  ${DEFAULT_RELEASE_URL}

With a tarball-url, installs that build (for example a Development builds file):
  install-visona.sh https://github.com/${REPO}/releases/download/dev-builds/Visona-linux-arm64-pi-cursor-kiosk-touch-807e-df72c7b.tar.gz

Options:
  --enable-autologin   Pass through to install-kiosk.sh (desktop autologin via raspi-config)
  --install-dir DIR    Unpack here instead of ${INSTALL_DIR}
  --no-reboot          Do not suggest a reboot at the end (labwc touch changes need one)
  -h, --help           Show this help

Run on the Pi over SSH or in a desktop terminal. Requires curl and tar.
Settings in ~/.config/Visona/ are kept across upgrades.

Bootstrap before you have a tarball (runs the script from the default branch):
  curl -fsSL https://raw.githubusercontent.com/${REPO}/main/scripts/pi/install-visona.sh | bash -s -- [options] [tarball-url]
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable-autologin)
            ENABLE_AUTOLOGIN=true
            ;;
        --install-dir)
            shift
            [[ $# -gt 0 ]] || {
                echo "Missing argument for --install-dir" >&2
                exit 1
            }
            INSTALL_DIR="$1"
            ;;
        --no-reboot)
            NO_REBOOT=true
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        --*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [[ -n "${TARBALL_URL}" ]]; then
                echo "Unexpected extra argument: $1" >&2
                usage >&2
                exit 1
            fi
            TARBALL_URL="$1"
            ;;
    esac
    shift
done

if [[ -z "${TARBALL_URL}" ]]; then
    TARBALL_URL="${DEFAULT_RELEASE_URL}"
fi

case "${TARBALL_URL}" in
    https://*) ;;
    http://*)
        echo "Refusing non-HTTPS URL: ${TARBALL_URL}" >&2
        exit 1
        ;;
    *)
        echo "Tarball URL must be http(s): ${TARBALL_URL}" >&2
        exit 1
        ;;
esac

if ! command -v curl >/dev/null 2>&1; then
    echo "curl is required. Install it with: sudo apt install curl" >&2
    exit 1
fi

arch="$(uname -m)"
if [[ "${arch}" != aarch64 && "${arch}" != arm64 ]]; then
    echo "This script installs the Linux arm64 Pi build; this machine is ${arch}." >&2
    exit 1
fi

echo "Stopping Visona if it is running..."
pkill -x Visona 2>/dev/null || true
sleep 1

tmpdir="$(mktemp -d)"
cleanup() {
    rm -rf "${tmpdir}"
}
trap cleanup EXIT

archive="${tmpdir}/visona-pi.tar.gz"
echo "Downloading ${TARBALL_URL}"
curl -fL --retry 3 --retry-delay 2 -o "${archive}" "${TARBALL_URL}"

mkdir -p "${INSTALL_DIR}"
echo "Unpacking to ${INSTALL_DIR}"
tar xzf "${archive}" -C "${INSTALL_DIR}"

bin="${INSTALL_DIR}/Visona"
kiosk_install="${INSTALL_DIR}/scripts/pi/install-kiosk.sh"
if [[ ! -x "${bin}" ]]; then
    echo "Expected executable ${bin} after unpack; check the tarball." >&2
    exit 1
fi
if [[ ! -f "${kiosk_install}" ]]; then
    echo "Expected ${kiosk_install} after unpack; check the tarball." >&2
    exit 1
fi

chmod +x "${bin}" "${INSTALL_DIR}/scripts/pi/"*.sh 2>/dev/null || true

kiosk_args=()
if [[ "${ENABLE_AUTOLOGIN}" == true ]]; then
    kiosk_args+=(--enable-autologin)
fi

echo "Running install-kiosk.sh..."
bash "${kiosk_install}" "${kiosk_args[@]}"

cat <<EOF

Visona is installed in ${INSTALL_DIR}.

  Test now:  ${INSTALL_DIR}/scripts/pi/visona-kiosk.sh
  Autostart: ~/.config/autostart/visona-kiosk.desktop

EOF

if [[ "${NO_REBOOT}" == false ]]; then
    echo "Reboot so labwc reloads touch settings (pinch zoom):  sudo reboot"
fi
