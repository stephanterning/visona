# Visona kiosk autostart on Raspberry Pi

This guide makes a Raspberry Pi boot into the desktop and start **Visona fullscreen** without SSH or a manual launch.

Requires Visona on the Pi: either the unpacked `Visona-linux-arm64-pi.tar.gz` from [GitHub Releases](https://github.com/stephanterning/visona/releases), which holds the binary and these scripts, or a source build — see [pi-build.md](pi-build.md) and [pi-setup.md](pi-setup.md).

## Install or upgrade over SSH

`scripts/pi/install-visona.sh` downloads a tarball, unpacks it to `~/visona`, and runs `install-kiosk.sh`. Settings in `~/.config/Visona/` are kept.

Latest **release** (no argument):

```sh
curl -fsSL https://raw.githubusercontent.com/stephanterning/visona/main/scripts/pi/install-visona.sh | bash
```

A **Development builds** URL from the workflow summary (copy the full link):

```sh
curl -fsSL https://raw.githubusercontent.com/stephanterning/visona/main/scripts/pi/install-visona.sh | bash -s -- \
  'https://github.com/stephanterning/visona/releases/download/dev-builds/Visona-linux-arm64-pi-cursor-kiosk-touch-807e-df72c7b.tar.gz'
```

If the script is already on the Pi (inside `~/visona` after a previous install):

```sh
~/visona/scripts/pi/install-visona.sh 'https://github.com/…/Visona-linux-arm64-pi-….tar.gz'
```

Add `--enable-autologin` before the URL when piping, or after `bash -s --`. Reboot when the script finishes so labwc reloads touch settings.

## What gets installed

1. **`--kiosk` flag** — Visona starts without window chrome and enters compositor fullscreen on Linux (hides the Pi menubar on labwc).
2. **`scripts/pi/visona-kiosk.sh`** — disables X11 screen blanking, then launches Visona.
3. **`~/.config/autostart/visona-kiosk.desktop`** — starts the wrapper when the desktop session loads.

Autologin (boot straight to desktop without a login prompt) is optional and configured separately.

## One-time setup

From the release tarball:

```sh
mkdir -p ~/visona && tar xzf Visona-linux-arm64-pi.tar.gz -C ~/visona
cd ~/visona
./scripts/pi/install-kiosk.sh --enable-autologin
sudo reboot
```

From a source build, after the `pi` build from [pi-build.md](pi-build.md):

```sh
cd ~/visona
./scripts/pi/install-kiosk.sh --enable-autologin
sudo reboot
```

The scripts use the `Visona` binary next to `scripts/` when there is one, as in the tarball, and otherwise `build/pi/app/Visona_artefacts/Release/Visona`.

Without autologin (you still log in once at the desk, then Visona starts):

```sh
./scripts/pi/install-kiosk.sh
```

## Manual autologin

If you prefer the GUI:

```sh
sudo raspi-config
```

**System Options → Boot / Auto Login → Desktop Autologin**

Or non-interactive:

```sh
sudo raspi-config nonint do_boot_behaviour B4
```

## Run kiosk manually (testing)

```sh
./scripts/pi/visona-kiosk.sh
```

Or run the binary directly: `./Visona --kiosk` from the tarball, or `./build/pi/app/Visona_artefacts/Release/Visona --kiosk` from a source build.

## Babyface and MIDI interface

Visona restores the **last saved** audio device and MIDI port (same as macOS):

1. Run Visona once, select Babyface and the correct channels in Settings, then quit normally so settings are saved.
2. Use the Babyface in **Class Compliant mode**.
3. Use a **powered USB hub** or external PSU for the interface.

The interfaces can be plugged in before or after boot, and unplugged and plugged in again while Visona runs (D-102). While the saved audio device is missing, Visona shows **NO AUDIO INPUT** and opens no other device; a few seconds after the device appears, the scope runs again. The MIDI input reconnects the same way; until it does, the status bar shows **MIDI CLOCK LOST** or **FREE**.

## Display

The display can be switched on after the Pi has booted, switched off and on, or changed to another resolution: the kiosk window asks the compositor for fullscreen and follows the display (D-103).

## Disable kiosk autostart

```sh
rm ~/.config/autostart/visona-kiosk.desktop
```

Restore the Pi menubar if `install-kiosk.sh` disabled it:

```sh
sudo cp /etc/xdg/labwc/autostart.visona-kiosk.bak /etc/xdg/labwc/autostart
killall wf-panel-pi 2>/dev/null || true
/usr/bin/lwrespawn /usr/bin/wf-panel-pi &
```

## Troubleshooting

| Problem | What to try |
| --- | --- |
| Black screen after login | Log in via SSH, run `./scripts/pi/visona-kiosk.sh` and read errors; check `DISPLAY=:0`. |
| Visona does not start at login | Confirm `~/.config/autostart/visona-kiosk.desktop` exists; log out and in (not just reboot to text console). |
| Screen goes blank | Wrapper runs `xset`; install `x11-xserver-utils` if missing: `sudo apt install x11-xserver-utils`. |
| System menubar still visible | Kiosk uses `setFullScreen(true)` on Linux so labwc hides the panel. Press **f** or the fullscreen button once to check, and update to the latest release or rebuild after `git pull`. As a fallback, `./scripts/pi/install-kiosk.sh` disables `wf-panel-pi` autostart. |
| `server does not have extension for -dpms` | Harmless on Wayland/XWayland; screen blanking is handled elsewhere. |
| Window not fullscreen, or small with a title bar | Update to the latest release or rebuild after `git pull`. Kiosk asks the compositor for fullscreen and follows display changes (D-103). |
| Quit without a close button | From SSH: `pkill Visona`. Kiosk mode has no window chrome. |
| Wrong audio device | Open Settings once with keyboard/touch and choose Babyface; the choice is saved at once. |

## Limitations

- Requires the **desktop session** (X11 / XWayland). Pure headless or Wayland-only kiosk is not supported yet.
- **Escape** still resets zoom; there is no PIN or lock screen.
