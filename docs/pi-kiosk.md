# Visona kiosk autostart on Raspberry Pi

This guide makes a Raspberry Pi boot into the desktop and start **Visona fullscreen** without SSH or a manual launch.

Requires Visona on the Pi: either the unpacked `Visona-linux-arm64-pi.tar.gz` from [GitHub Releases](https://github.com/stephanterning/visona/releases), which holds the binary and these scripts, or a source build — see [pi-build.md](pi-build.md) and [pi-setup.md](pi-setup.md).

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

## Babyface at startup

Visona restores the **last saved** audio device and MIDI port (same as macOS). For a working scope at boot:

1. Run Visona once, select Babyface and the correct channels in Settings, then quit normally so settings are saved.
2. Connect the Babyface in **Class Compliant mode** before powering the Pi (or before login).
3. Use a **powered USB hub** or external PSU for the interface.

If the saved device is missing at startup, Visona shows **NO AUDIO INPUT** until you open Settings (touch ⚙).

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
| Window not fullscreen | Update to the latest release or rebuild after `git pull`; kiosk uses the full display, not JUCE's Linux work-area fullscreen. |
| Quit without a close button | From SSH: `pkill Visona`. Kiosk mode has no window chrome. |
| Wrong audio device | Open Settings once with keyboard/touch, save Babyface, restart. |

## Limitations

- Requires the **desktop session** (X11 / XWayland). Pure headless or Wayland-only kiosk is not supported yet.
- No automatic **hotplug** if the interface is plugged in after boot (MVP behaviour, D-033).
- **Escape** still resets zoom; there is no PIN or lock screen.
