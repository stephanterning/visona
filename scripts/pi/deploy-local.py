#!/usr/bin/env python3
"""Remote half of build-and-deploy-to-pi-over-network.sh; runs as the logged-in desktop user."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time


def processes():
    for directory in Path('/proc').iterdir():
        if directory.name.isdigit():
            try:
                if directory.stat().st_uid == os.getuid():
                    yield int(directory.name), directory
            except FileNotFoundError:
                pass


def session_environment(binary):
    """Prefer the running app's display credentials, then the desktop session's."""
    candidates = []
    pids = []
    for pid, directory in processes():
        try:
            executable = os.readlink(directory / 'exe')
            is_app = executable.removesuffix(' (deleted)') == str(binary)
            name = (directory / 'comm').read_text().strip()
            if is_app:
                pids.append(pid)
            if is_app or name in ('labwc', 'wayfire', 'lxsession', 'Xwayland', 'wf-panel-pi'):
                entries = (directory / 'environ').read_bytes().split(b'\0')
                env = dict(item.decode(errors='surrogateescape').split('=', 1)
                           for item in entries if b'=' in item)
                candidates.append((is_app, env))
        except (OSError, ValueError):
            continue
    env = os.environ.copy()
    # Never forward an SSH X11 display to the kiosk process.
    for key in ('DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY'):
        env.pop(key, None)
    keys = ('DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR',
            'DBUS_SESSION_BUS_ADDRESS', 'XDG_SESSION_TYPE', 'VISONA_PERF_LOG')
    for _, candidate in sorted(candidates, key=lambda pair: pair[0]):
        env.update({key: candidate[key] for key in keys if key in candidate})
    if not env.get('DISPLAY'):
        raise RuntimeError('No local desktop DISPLAY found. Log in to the Pi desktop first.')
    return env, pids


def alive(pid):
    try:
        os.kill(pid, 0)
        # An exited child awaiting reaping is not a running app.
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z'
    except (ProcessLookupError, FileNotFoundError):
        return False


def launch(wrapper, binary, env, log):
    env = dict(env, VISONA_BIN=str(binary))
    with log.open('ab') as output:
        process = subprocess.Popen(['bash', str(wrapper)], env=env,
                                   stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                                   start_new_session=True, close_fds=True)
    # This detects immediate failures, not audio/display correctness.
    for _ in range(10):
        time.sleep(0.5)
        if process.poll() is not None:
            raise RuntimeError(f'Visona exited with {process.returncode}; see {log}')
    return process.pid


def deploy(source, destination):
    destination = destination.expanduser()
    if not destination.is_absolute():
        destination = Path.home() / destination
    destination = destination.resolve()
    binary = destination / 'Visona'
    wrapper = destination / 'scripts/pi/visona-kiosk.sh'
    if not binary.is_file() or not wrapper.is_file():
        raise RuntimeError(f'Expected an existing kiosk installation in {destination}. '
                           'Run install-visona.sh on the Pi first, or set --install-dir.')
    if binary.is_symlink():
        raise RuntimeError('The installed Visona binary must be a regular file, not a symlink.')
    source.chmod(0o755)
    result = subprocess.run(['ldd', str(source)], capture_output=True, text=True)
    if result.returncode or 'not found' in result.stdout + result.stderr:
        raise RuntimeError('Binary dependencies are incompatible; installed app left running:\n'
                           + result.stdout + result.stderr)
    pending = destination / 'Visona.pending'
    backup = destination / 'Visona.previous'
    log = destination / 'visona-deploy.log'
    # Serialize deployments, including the entire stop/start window.
    import fcntl
    with (destination / '.deploy.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        env, pids = session_environment(binary)
        profile = os.environ.get("VISONA_PERF_LOG", "inherit")
        if profile in ("0", "1"):
            env["VISONA_PERF_LOG"] = profile
        shutil.copy2(source, pending)
        try:
            shutil.copy2(binary, backup)
            for pid in pids:
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            deadline = time.monotonic() + 10
            while any(alive(pid) for pid in pids):
                if time.monotonic() > deadline:
                    raise RuntimeError('Visona did not stop within 10 seconds; binary not replaced.')
                time.sleep(0.1)
            os.replace(pending, binary)
            try:
                pid = launch(wrapper, binary, env, log)
            except Exception:
                os.replace(backup, binary)
                print('Startup failed; restored the previous binary.', file=sys.stderr)
                launch(wrapper, binary, env, log)
                raise
            print(f'Installed {binary}; restarted Visona (PID {pid}). Log: {log}')
        finally:
            pending.unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        deploy(Path(sys.argv[1]), Path(sys.stdin.read()))
    except Exception as error:
        print(f'Deployment failed: {error}', file=sys.stderr)
        sys.exit(1)
