#!/usr/bin/env bash
# Fetch latest main, build the macOS app and plugins, install all formats, and launch the app.
set -euo pipefail

SKIP_GIT=false
SKIP_TESTS=false
SKIP_LAUNCH=false

usage() {
    cat <<'EOF'
Usage: build-and-install.sh [options]

Fetch origin/main, build Visona.app and both plugins (VST3, AU and CLAP) with the macos
preset, install the bundles into ~/Library/Audio/Plug-Ins/, ad-hoc sign everything, refresh
the macOS AU cache, clear Gatekeeper quarantine on the app bundle, and open the app.

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
BUILD_PLUGIN_DIR="${REPO_ROOT}/build/macos/plugin"
VISONA_ARTEFACTS="${BUILD_PLUGIN_DIR}/VisonaPlugin_artefacts/Release"
SYNC_ARTEFACTS="${BUILD_PLUGIN_DIR}/VisonaSyncPlugin_artefacts/Release"

PLUGIN_BUNDLES=(
    "${VISONA_ARTEFACTS}/VST3/Visona.vst3"
    "${SYNC_ARTEFACTS}/VST3/Visona Sync.vst3"
    "${VISONA_ARTEFACTS}/AU/Visona.component"
    "${SYNC_ARTEFACTS}/AU/Visona Sync.component"
    "${VISONA_ARTEFACTS}/CLAP/Visona.clap"
    "${SYNC_ARTEFACTS}/CLAP/Visona Sync.clap"
)

for path in "${APP_BUNDLE}" "${PLUGIN_BUNDLES[@]}"; do
    if [[ ! -e "${path}" ]]; then
        echo "Build output missing: ${path}" >&2
        exit 1
    fi
done

PLUGINS_DIR="${HOME}/Library/Audio/Plug-Ins"
VST3_DIR="${PLUGINS_DIR}/VST3"
AU_DIR="${PLUGINS_DIR}/Components"
CLAP_DIR="${PLUGINS_DIR}/CLAP"

echo "==> Installing plugins into ${PLUGINS_DIR}"
mkdir -p "${VST3_DIR}" "${AU_DIR}" "${CLAP_DIR}"
rm -rf \
    "${VST3_DIR}/Visona.vst3" \
    "${VST3_DIR}/Visona Sync.vst3" \
    "${AU_DIR}/Visona.component" \
    "${AU_DIR}/Visona Sync.component" \
    "${CLAP_DIR}/Visona.clap" \
    "${CLAP_DIR}/Visona Sync.clap"

cp -R "${VISONA_ARTEFACTS}/VST3/Visona.vst3" "${SYNC_ARTEFACTS}/VST3/Visona Sync.vst3" "${VST3_DIR}/"
cp -R "${VISONA_ARTEFACTS}/AU/Visona.component" "${SYNC_ARTEFACTS}/AU/Visona Sync.component" "${AU_DIR}/"
cp -R "${VISONA_ARTEFACTS}/CLAP/Visona.clap" "${SYNC_ARTEFACTS}/CLAP/Visona Sync.clap" "${CLAP_DIR}/"

echo "==> Clearing quarantine and ad-hoc signing plugins"
xattr -cr \
    "${VST3_DIR}/Visona.vst3" \
    "${VST3_DIR}/Visona Sync.vst3" \
    "${AU_DIR}/Visona.component" \
    "${AU_DIR}/Visona Sync.component" \
    "${CLAP_DIR}/Visona.clap" \
    "${CLAP_DIR}/Visona Sync.clap"

codesign --force --sign - --timestamp=none --deep "${VST3_DIR}/Visona.vst3"
codesign --force --sign - --timestamp=none --deep "${VST3_DIR}/Visona Sync.vst3"
codesign --force --sign - --timestamp=none --deep "${AU_DIR}/Visona.component"
codesign --force --sign - --timestamp=none --deep "${AU_DIR}/Visona Sync.component"
codesign --force --sign - --timestamp=none --deep "${CLAP_DIR}/Visona.clap"
codesign --force --sign - --timestamp=none --deep "${CLAP_DIR}/Visona Sync.clap"

echo "==> Refreshing macOS AU cache"
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "==> Clearing Gatekeeper quarantine and signing app"
xattr -cr "${APP_BUNDLE}"
codesign --force --sign - --timestamp=none --deep "${APP_BUNDLE}"
codesign --verify --strict --verbose=2 "${APP_BUNDLE}"

echo
echo "Build complete."
echo "  App:   ${APP_BUNDLE}"
echo "  VST3:  ${VST3_DIR}/Visona.vst3"
echo "         ${VST3_DIR}/Visona Sync.vst3"
echo "  AU:    ${AU_DIR}/Visona.component"
echo "         ${AU_DIR}/Visona Sync.component"
echo "  CLAP:  ${CLAP_DIR}/Visona.clap"
echo "         ${CLAP_DIR}/Visona Sync.clap"
echo
echo "Restart your DAW or rescan plugins before testing. Do not delete DAW preference folders."

if [[ "${SKIP_LAUNCH}" == false ]]; then
    echo "==> Launching Visona"
    open "${APP_BUNDLE}"
fi
