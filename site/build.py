#!/usr/bin/env python3
"""Builds the static visona.org site from the GitHub releases.

Picks the newest alpha, beta and stable release, renders index.html with their
download links, sizes and SHA-256 digests, and writes stable redirect URLs such as
/download/latest/macos-arm64/ and /download/alpha/vst3-windows-x64/.

Standard library only. Usage:
    python3 site/build.py --out _site
    python3 site/build.py --out _site --releases-json releases.json   # offline
    python3 site/build.py --serve   # build, then preview on http://localhost:8000
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

REPO = os.environ.get("GITHUB_REPOSITORY", "stephanterning/visona")
SITE_URL = "https://visona.org"
ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parent

TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.?(\d+))?$")
STAGE_ORDER = {"alpha": 0, "beta": 1, "rc": 2, None: 3}

# Most mature first; /download/latest/ follows the first channel that has a release.
CHANNELS = [
    ("stable", "Stable", "Recommended for everyday use."),
    ("beta", "Beta", "Feature-complete, still being tested."),
    ("alpha", "Alpha", "Newest features. Expect rough edges."),
]

PLATFORMS = {
    "macos-arm64": "macOS · Apple Silicon",
    "windows-x64": "Windows · x64",
    "linux-x86_64": "Linux · x86_64",
    "linux-arm64-pi": "Raspberry Pi · ARM64",
}

GROUPS = [
    ("app", "Standalone app"),
    ("plugin", "Visona plugin"),
    ("sync", "Visona Sync"),
    ("other", "Other files"),
]

DOCS = f"https://github.com/{REPO}/blob/main/docs"


@dataclass
class Asset:
    name: str
    key: str
    url: str
    size: int
    sha256: str
    group: str
    format: str
    platform: str


@dataclass
class Release:
    tag: str
    name: str
    url: str
    published: str
    channel: str
    version: tuple
    assets: list[Asset] = field(default_factory=list)


def fetch_releases() -> list[dict]:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    releases: list[dict] = []
    page = 1
    while True:
        request = urllib.request.Request(
            f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}",
            headers={"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"},
        )
        if token:
            request.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(request, timeout=30) as response:
            batch = json.load(response)
        releases.extend(batch)
        if len(batch) < 100:
            return releases
        page += 1


def parse_asset(raw: dict) -> Asset:
    name = raw["name"]
    key = re.sub(r"\.(zip|tar\.gz|tgz|dmg|pkg)$", "", name)
    key = re.sub(r"^Visona-", "", key)

    group, fmt, platform = "other", "", key
    parts = key.split("-")
    if parts[0] == "sync" and len(parts) > 1:
        group, fmt, platform = "sync", parts[1].upper(), "-".join(parts[2:])
    elif parts[0] in ("vst3", "au", "clap"):
        group, fmt, platform = "plugin", parts[0].upper(), "-".join(parts[1:])
    elif key in PLATFORMS:
        group, fmt = "app", "App"

    digest = raw.get("digest") or ""
    return Asset(
        name=name,
        key=key,
        url=raw["browser_download_url"],
        size=raw.get("size", 0),
        sha256=digest.removeprefix("sha256:") if digest.startswith("sha256:") else "",
        group=group,
        format=fmt,
        platform=platform,
    )


def pick_latest(raw_releases: list[dict]) -> dict[str, Release]:
    latest: dict[str, Release] = {}
    for raw in raw_releases:
        match = TAG_RE.match(raw.get("tag_name", ""))
        # Skip drafts, dev-builds and releases whose assets the Release workflow has not uploaded yet.
        if raw.get("draft") or not match or not raw.get("assets"):
            continue
        major, minor, patch, stage, number = match.groups()
        if stage is None and raw.get("prerelease"):
            continue
        channel = {"alpha": "alpha", "beta": "beta", "rc": "beta", None: "stable"}[stage]
        version = (int(major), int(minor), int(patch), STAGE_ORDER[stage], int(number or 0))
        current = latest.get(channel)
        if current and current.version >= version:
            continue
        latest[channel] = Release(
            tag=raw["tag_name"],
            name=raw.get("name") or raw["tag_name"],
            url=raw["html_url"],
            published=(raw.get("published_at") or "")[:10],
            channel=channel,
            version=version,
            assets=sorted((parse_asset(a) for a in raw["assets"]), key=lambda a: a.name),
        )
    return latest


def human_size(size: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return ""


def esc(value: str) -> str:
    return html.escape(value, quote=True)


def render_channel(channel: str, title: str, blurb: str, release: Release | None, open_: bool) -> str:
    if release is None:
        return f"""
      <details class="channel channel-empty">
        <summary><span class="badge badge-{channel}">{title}</span><span class="version">Not released yet</span></summary>
        <p class="muted">{esc(blurb)} The first {title.lower()} release will show up here automatically.</p>
      </details>"""

    rows = []
    for group, group_title in GROUPS:
        assets = [a for a in release.assets if a.group == group]
        if not assets:
            continue
        rows.append(f'<tr class="group"><th colspan="4">{group_title}</th></tr>')
        for a in assets:
            platform = PLATFORMS.get(a.platform, a.platform)
            what = platform if group == "app" else f"{a.format} · {platform}"
            sha = (
                f'<code class="sha" title="SHA-256">{esc(a.sha256)}</code>' if a.sha256 else '<span class="muted">–</span>'
            )
            rows.append(
                f"""<tr>
            <td><a class="dl" href="{esc(a.url)}">{esc(what)}</a><div class="file">{esc(a.name)}</div></td>
            <td class="size">{human_size(a.size)}</td>
            <td>{sha}</td>
            <td class="perma"><a href="/download/{channel}/{esc(a.key)}/" title="Permanent link to the newest {title.lower()} build">link</a></td>
          </tr>"""
            )

    return f"""
      <details class="channel"{" open" if open_ else ""}>
        <summary>
          <span class="badge badge-{channel}">{title}</span>
          <span class="version">{esc(release.tag)}</span>
          <span class="muted date">{esc(release.published)}</span>
        </summary>
        <p class="muted">{esc(blurb)} <a href="{esc(release.url)}">Release notes</a> ·
          <a href="/download/{channel}/SHA256SUMS">SHA256SUMS</a></p>
        <div class="table-wrap">
        <table>
          <thead><tr><th>Download</th><th>Size</th><th>SHA-256</th><th></th></tr></thead>
          <tbody>
          {"".join(rows)}
          </tbody>
        </table>
        </div>
      </details>"""


def render_index(latest: dict[str, Release]) -> str:
    template = (ROOT / "index.template.html").read_text(encoding="utf-8")
    first_available = next((c for c, _, _ in CHANNELS if c in latest), None)
    channels = "".join(
        render_channel(c, t, b, latest.get(c), c == first_available) for c, t, b in CHANNELS
    )
    newest = latest.get(first_available) if first_available else None
    replacements = {
        "{{CHANNELS}}": channels,
        "{{LATEST_TAG}}": esc(newest.tag) if newest else "Coming soon",
        "{{REPO}}": REPO,
        "{{DOCS}}": DOCS,
        "{{SITE_URL}}": SITE_URL,
        "{{YEAR}}": str(datetime.now().year),
    }
    for placeholder, value in replacements.items():
        template = template.replace(placeholder, value)
    return template


def redirect_page(url: str, label: str) -> str:
    target = esc(url)
    script_target = json.dumps(url).replace("</", "<\\/")
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Downloading {esc(label)}</title>
<meta name="robots" content="noindex">
<meta http-equiv="refresh" content="0; url={target}">
<link rel="canonical" href="{target}">
<script>location.replace({script_target})</script>
</head>
<body><p>Downloading <a href="{target}">{esc(label)}</a>…</p></body>
</html>
"""


def write_downloads(out: Path, latest: dict[str, Release]) -> None:
    channels = [(c, latest[c]) for c, _, _ in CHANNELS if c in latest]
    if channels:
        channels.append(("latest", channels[0][1]))
    for channel, release in channels:
        base = out / "download" / channel
        for asset in release.assets:
            page = base / asset.key / "index.html"
            page.parent.mkdir(parents=True, exist_ok=True)
            page.write_text(redirect_page(asset.url, f"{asset.name} ({release.tag})"), encoding="utf-8")
        sums = "".join(f"{a.sha256}  {a.name}\n" for a in release.assets if a.sha256)
        base.mkdir(parents=True, exist_ok=True)
        (base / "SHA256SUMS").write_text(sums, encoding="utf-8")
        (base / "index.html").write_text(redirect_page(release.url, release.tag), encoding="utf-8")


def write_json(out: Path, latest: dict[str, Release]) -> None:
    data = {
        channel: {
            "tag": r.tag,
            "name": r.name,
            "url": r.url,
            "published": r.published,
            "assets": [
                {"name": a.name, "key": a.key, "url": a.url, "size": a.size, "sha256": a.sha256}
                for a in r.assets
            ],
        }
        for channel, r in latest.items()
    }
    (out / "releases.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "_site")
    parser.add_argument("--releases-json", type=Path, help="read releases from a file instead of the GitHub API")
    parser.add_argument("--serve", nargs="?", const=8000, type=int, metavar="PORT",
                        help="serve the built site on localhost (default port 8000)")
    args = parser.parse_args()

    raw = json.loads(args.releases_json.read_text()) if args.releases_json else fetch_releases()
    latest = pick_latest(raw)

    out: Path = args.out
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "static", out)
    images = out / "images"
    images.mkdir(exist_ok=True)
    for image in (REPO_ROOT / "docs" / "images").glob("visona-*.png"):
        shutil.copy2(image, images / image.name)

    (out / "index.html").write_text(render_index(latest), encoding="utf-8")
    write_downloads(out, latest)
    write_json(out, latest)

    for channel, _, _ in CHANNELS:
        release = latest.get(channel)
        print(f"{channel:7} {release.tag if release else '-':20} {len(release.assets) if release else 0} assets")

    if args.serve:
        serve(out, args.serve)


def serve(directory: Path, port: int) -> None:
    import functools
    import http.server

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(directory))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"Serving {directory} on http://localhost:{port}/ (Ctrl+C to stop)", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
