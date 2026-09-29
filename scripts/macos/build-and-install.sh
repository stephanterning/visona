#!/usr/bin/env bash
# Fetch latest main, build the macOS app and VST3 plugins, install plugins, and launch the app.
set -euo pipefail

SKIP_GIT=false
SKIP_TESTS=false
SKIP_LAUNCH=false

usage() {
    cat <<'EOF'
Usage: build-and-install.sh [options]

Fetch origin/main, build Visona.app and both VST3 plugins with the macos preset,
install the plugins into ~/Library/Audio/Plug-Ins/VST3/, ad-hoc sign everything,
clear Gatekeeper quarantine on the app bundle, and open the app.

Options:
  --skip-git      Do not fetch or update the repository.
  --skip-tests    Skip ctest after the build.
  --skip-launch   Build and install only; do not open the app.
  -h, --help      Show this help.

Requires macOS on Apple Silicon, Xcode, CMake 3.22+, and Ninja (brew install cmake ninja).
Run from anywhere inside the Visona repository.
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --skip-git) SKIP_GIT=true ;;
        --skip-tests) SKIP_TESTS=true ;;
        --skip-launch) SKIP_LAUNCH=true ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: ${arg}" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script must run on macOS." >&2
    exit 1
fi

if [[ "$(uname -m)" != "arm64" ]]; then
    echo "Visona macOS builds target Apple Silicon (arm64) only." >&2
    exit 1
fi

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

require_command git
require_command cmake
require_command ninja
require_command codesign
require_command xattr

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode is not selected. Install Xcode, then run:" >&2
    echo "  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ ! -f "${REPO_ROOT}/CMakePresets.json" ]]; then
    echo "Could not find the Visona repository root (expected CMakePresets.json)." >&2
    exit 1
fi

cd "${REPO_ROOT}"

if [[ "${SKIP_GIT}" == false ]]; then
    echo "==> Fetching latest main"
    git fetch origin main
    git checkout main
    git pull --ff-only origin main
fi

echo "==> Configuring and building (macos preset)"
cmake --preset macos
cmake --build --preset macos

if [[ "${SKIP_TESTS}" == false ]]; then
    echo "==> Running tests"
    ctest --preset macos
fi

APP_BUNDLE="${REPO_ROOT}/build/macos/app/Visona_artefacts/Release/Visona.app"
VISONA_VST3="${REPO_ROOT}/build/macos/plugin/VisonaPlugin_artefacts/Release/VST3/Visona.vst3"
SYNC_VST3="${REPO_ROOT}/build/macos/plugin/VisonaSyncPlugin_artefacts/Release/VST3/Visona Sync.vst3"
VST3_DIR="${HOME}/Library/Audio/Plug-Ins/VST3"

for path in "${APP_BUNDLE}" "${VISONA_VST3}" "${SYNC_VST3}"; do
    if [[ ! -e "${path}" ]]; then
        echo "Build output missing: ${path}" >&2
        exit 1
    fi
done

echo "==> Installing VST3 plugins into ${VST3_DIR}"
mkdir -p "${VST3_DIR}"
rm -rf "${VST3_DIR}/Visona.vst3" "${VST3_DIR}/Visona Sync.vst3"
cp -R "${VISONA_VST3}" "${SYNC_VST3}" "${VST3_DIR}/"

echo "==> Ad-hoc signing plugins"
codesign --force --sign - --timestamp=none --deep "${VST3_DIR}/Visona.vst3"
codesign --force --sign - --timestamp=none --deep "${VST3_DIR}/Visona Sync.vst3"

echo "==> Clearing Gatekeeper quarantine and signing app"
xattr -cr "${APP_BUNDLE}"
codesign --force --sign - --timestamp=none --deep "${APP_BUNDLE}"
codesign --verify --strict --verbose=2 "${APP_BUNDLE}"

echo
echo "Build complete."
echo "  App:     ${APP_BUNDLE}"
echo "  VST3:    ${VST3_DIR}/Visona.vst3"
echo "  Sync:    ${VST3_DIR}/Visona Sync.vst3"
echo
echo "Restart your DAW or rescan plugins before testing the new VST3 bundles."

if [[ "${SKIP_LAUNCH}" == false ]]; then
    echo "==> Launching Visona"
    open "${APP_BUNDLE}"
fi
