#!/usr/bin/env bash
# Build local sources in Linux ARM64, then replace and restart the Pi app over SSH.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${VISONA_PI_HOST:-}"
INSTALL_DIR="${VISONA_PI_DIR:-visona}"
JOBS="${VISONA_PI_JOBS:-4}"
BUILD_ONLY=false
PERF_LOG=inherit
HOST_ARGUMENT=false
usage() {
    cat <<'HELP'
Usage: scripts/pi/build-and-deploy-to-pi-over-network.sh [user@pi-host] [options]
  --install-dir DIR  Existing Pi installation (default: visona, relative to home)
  --jobs N           Parallel compiler jobs (default: 4; raise if Docker has enough RAM)
  --profile          Enable FPS/CPU telemetry in visona-deploy.log
  --no-profile       Disable telemetry
  --build-only       Build/export without SSH or deployment
  -h, --help         Show help

Requires running Docker (Linux containers), SSH, and an existing Pi kiosk installation
with a logged-in desktop session. Use the desktop user's SSH account, not root.
SSH aliases/config are supported. Set VISONA_PI_HOST to avoid repeating the host;
VISONA_PI_DIR and VISONA_PI_JOBS provide defaults for the options above.
Builds current local sources, including uncommitted edits; never fetches or pushes Git.
Output: build/pi-local/Visona. First build downloads dependencies; later builds are cached.
Only the app is built (no plugins or tests). Restarts Visona, not the Pi itself.
HELP
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-dir|--jobs)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            if [[ "$1" == --jobs ]]; then JOBS="$2"; else INSTALL_DIR="$2"; fi
            shift 2 ;;
        --profile) PERF_LOG=1; shift ;;
        --no-profile) PERF_LOG=0; shift ;;
        --build-only) BUILD_ONLY=true; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *) ! $HOST_ARGUMENT || { echo 'Pass only one host.' >&2; exit 2; }
           HOST="$1"; HOST_ARGUMENT=true; shift ;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'Jobs must be a positive integer.' >&2; exit 2; }
[[ -n "$INSTALL_DIR" ]] || { echo 'Installation directory cannot be empty.' >&2; exit 2; }
if ! $BUILD_ONLY; then
    [[ -n "$HOST" && "$HOST" != -* && "$HOST" != *[[:space:]]* ]] || { usage >&2; exit 2; }
    command -v ssh >/dev/null
    command -v scp >/dev/null
    # Check access before spending time compiling. Do not disable host-key verification.
    ssh -o ConnectTimeout=10 "$HOST" 'test "$(uname -m)" = aarch64 && command -v python3 >/dev/null' || {
        echo 'Need SSH access to a 64-bit Linux Pi with Python 3.' >&2; exit 1;
    }
fi
command -v docker >/dev/null || { echo 'Install and start Docker Desktop first.' >&2; exit 1; }
docker info >/dev/null || { echo 'Start Docker Desktop and retry.' >&2; exit 1; }
# Separate caches for different checkouts, with all build I/O inside the Linux VM.
KEY="$(printf '%s' "$ROOT" | cksum | awk '{print $1}')"
IMAGE=visona-pi-builder:bookworm
mkdir -p "$ROOT/build/pi-local"
docker build --platform linux/arm64 -t "$IMAGE" "$ROOT/scripts/pi/docker"
docker run --rm --platform linux/arm64 \
    --mount "type=bind,source=$ROOT,target=/src,readonly" \
    --mount "type=volume,source=visona-pi-build-$KEY,target=/build" \
    --mount 'type=volume,source=visona-pi-ccache,target=/cache' \
    --mount "type=bind,source=$ROOT/build/pi-local,target=/out" \
    -e "BUILD_JOBS=$JOBS" -e "CMAKE_BUILD_PARALLEL_LEVEL=$JOBS" "$IMAGE" bash -euc '
        exec 9>/build/.build.lock
        flock 9
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DVISONA_BUILD_APP=ON -DVISONA_BUILD_PLUGIN=OFF -DVISONA_BUILD_TESTS=OFF \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
        cmake --build /build --target Visona --parallel "$BUILD_JOBS"
        install -m 755 /build/app/Visona_artefacts/Release/Visona /out/Visona
        strip /out/Visona
        ccache --show-stats
    '
$BUILD_ONLY && { echo "Built $ROOT/build/pi-local/Visona"; exit 0; }
STAGE="$(ssh "$HOST" 'mktemp -d /tmp/visona-deploy.XXXXXXXX')"
[[ "$STAGE" =~ ^/tmp/visona-deploy\.[A-Za-z0-9]+$ ]] || { echo 'Invalid remote staging path.' >&2; exit 1; }
cleanup() { ssh "$HOST" "rm -rf -- '$STAGE'" </dev/null || true; }
trap cleanup EXIT
scp -C "$ROOT/build/pi-local/Visona" "$ROOT/scripts/pi/deploy-local.py" "$HOST:$STAGE/"
# Pass the destination on stdin, never interpolate it into a remote shell command.
printf '%s' "$INSTALL_DIR" | ssh "$HOST" "VISONA_PERF_LOG='$PERF_LOG' python3 '$STAGE/deploy-local.py' '$STAGE/Visona'"
