"""Fetch hash-verified upstream sources and build LSP locally; never install anything."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/stereo-spike"))
    parser.add_argument("--archive-cache", type=Path)
    parser.add_argument("--make", default="gmake", help="GNU Make 4 or newer")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--asan", action="store_true", help="Build upstream C++ with AddressSanitizer")
    args = parser.parse_args()
    version = subprocess.check_output([args.make, "--version"], text=True).splitlines()[0]
    if not version.startswith("GNU Make ") or int(version.split()[2].split('.')[0]) < 4:
        parser.error("LSP requires GNU Make >= 4; macOS /usr/bin/make is too old")

    root = args.build_dir.resolve()
    cache = (args.archive_cache or root / "archives").resolve()
    cache.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(Path(__file__).with_name("dependencies.json").read_text())
    for dep in manifest:
        archive = cache / dep["archive"]
        if not archive.exists():
            print("Downloading", dep["name"], flush=True)
            urllib.request.urlretrieve(dep["url"], archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != dep["sha256"]:
            raise RuntimeError(f"Checksum mismatch: {archive}; remove it and retry")
        dest = root / "deps" / dep["destination"]
        marker = dest / ".visona-source-sha256"
        if marker.exists() and marker.read_text().strip() == digest:
            continue
        # Only overwrite archive-owned paths in the isolated dependency directory.
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                parts = member.name.split("/", 1)
                if len(parts) == 2 and parts[1]:
                    member.name = parts[1]
                    source.extract(member, dest, filter="data")
        marker.write_text(digest + "\n")

    lsp = root / "deps" / manifest[0]["destination"]
    environment = dict(os.environ, MACOSX_DEPLOYMENT_TARGET="14.0")
    with (root / "lsp-build.log").open("w") as log:
        config = ["config", "CC=clang", "CXX=clang++"]
        if args.asan:
            config.append("ADD_FEATURES=asan")
        # Upstream Make does not reliably track changes to compiler flags.
        for arguments in (["clean"], config, [f"-j{args.jobs}"]):
            subprocess.run([args.make, "-C", str(lsp), *arguments],
                           stdout=log, stderr=subprocess.STDOUT, check=True, env=environment)
    print("LSP_ROOT=" + str(lsp))
    print("Build log:", root / "lsp-build.log")


if __name__ == "__main__":
    main()
