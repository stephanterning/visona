#!/usr/bin/env python3
"""Collect live opt-in app telemetry over SSH and summarize each analysis view."""
import argparse
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host', help='Desktop SSH account, e.g. user@raspberrypi.local')
parser.add_argument('--seconds', type=int, default=30)
parser.add_argument('--install-dir', default='visona')
args = parser.parse_args()
if args.host.startswith('-') or any(c.isspace() for c in args.host):
    parser.error('Invalid SSH host')
if not 5 <= args.seconds <= 3600:
    parser.error('--seconds must be between 5 and 3600')

remote = r'''
import json, sys, time
from pathlib import Path
from statistics import mean
directory = Path(sys.argv[1]).expanduser()
if not directory.is_absolute():
    directory = Path.home() / directory
rows = []
with (directory / 'visona-deploy.log').open() as log:
    log.seek(0, 2)
    deadline = time.monotonic() + int(sys.argv[2])
    while time.monotonic() < deadline:
        line = log.readline()
        if not line:
            time.sleep(.1)
            continue
        if not line.startswith('VISONA_PERF '):
            continue
        row = json.loads(line.removeprefix('VISONA_PERF '))
        rows.append(row)
        print(json.dumps(row), flush=True)
if not rows:
    sys.exit('No telemetry. Deploy with --profile first; keep the app running.')
for view in sorted({r['view'] for r in rows}):
    for diagnostics in (False, True):
        group = [r for r in rows if r['view'] == view and r['diagnostics'] == diagnostics]
        if group:
            summary = {key: round(mean(r[key] for r in group), 2)
                       for key in ('fps', 'render_ms', 'paint_ms', 'cpu_percent', 'analysis_percent')}
            print('SUMMARY ' + json.dumps(dict(view=view, diagnostics=diagnostics,
                                               samples=len(group), **summary)), flush=True)
'''
command = 'python3 - ' + shlex.quote(args.install_dir) + ' ' + str(args.seconds)
raise SystemExit(subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                                 args.host, command], input=remote, text=True).returncode)
