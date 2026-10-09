#!/usr/bin/env bash
# Render the app icon's master SVG to the PNG that the app build uses.
set -euo pipefail

if ! command -v rsvg-convert >/dev/null 2>&1; then
    echo "error: rsvg-convert not found. Install it with: brew install librsvg" >&2
    exit 1
fi

icon_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../assets/icon" && pwd)"

rsvg-convert --width 1024 --height 1024 \
    --output "${icon_dir}/visona-icon-1024.png" \
    "${icon_dir}/visona-icon.svg"

echo "Wrote ${icon_dir}/visona-icon-1024.png"
