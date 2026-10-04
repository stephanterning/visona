# Visona kiosk autostart on Raspberry Pi

This guide makes a Raspberry Pi boot into the desktop and start **Visona fullscreen** without SSH or a manual launch.

Requires Visona on the Pi: either the unpacked `Visona-linux-arm64-pi.tar.gz` from [GitHub Releases](https://github.com/stephanterning/visona/releases), which holds the binary and these scripts, or a source build — see [pi-build.md](pi-build.md) and [pi-setup.md](pi-setup.md).

## What gets installed

1. **`--kiosk` flag** — Visona starts without window chrome and enters compositor fullscreen on Linux (hides the Pi menubar on labwc). The control bar is drawn at twice the size for touch, and the full-screen button is hidden.
2. **`scripts/pi/visona-kiosk.sh`** — disables X11 screen blanking, then launches Visona.
3. **`~/.config/autostart/visona-kiosk.desktop`** — starts the wrapper when the desktop session loads.
4. **`mouseEmulation="no"` in `~/.config/labwc/rc.xml`** — turns on multitouch, so a two-finger pinch zooms (see [Touchscreen](#touchscreen)).

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

## Touchscreen

A USB or DSI multitouch screen, such as a Waveshare 10.1", needs no driver: Raspberry Pi OS sees it as a touchscreen. In kiosk mode a tap works as a click, dragging across the scope zooms to that part, a finger held still for half a second draws the measuring rectangle, and a two-finger pinch zooms around the point between the fingers.

Raspberry Pi OS sets `mouseEmulation="yes"` for touchscreens in labwc. That turns every touch into a mouse event, so taps and drags work but a pinch never reaches Visona. `install-kiosk.sh` sets it to `"no"` in `~/.config/labwc/rc.xml` (copying `/etc/xdg/labwc/rc.xml` there first if the file does not exist, and backing an existing one up to `rc.xml.visona-kiosk.bak`). It takes effect after a reboot. The change applies to the whole desktop for that user; for example, double-tapping no longer opens files in the file manager.

To do it by hand, make the `<touch>` line in `~/.config/labwc/rc.xml` read, keeping its other attributes:

```xml
<touch deviceName="..." mapToOutput="HDMI-A-1" mouseEmulation="no"/>
```

Changing the touchscreen in **Screen Configuration** may write `mouseEmulation="yes"` again; run `install-kiosk.sh` again afterwards. To see whether the screen sends multitouch at all, run `sudo libinput debug-events` and put two fingers on it: each finger shows up as its own `TOUCH_DOWN` with a different slot number.

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

Restore touch mouse emulation, then reboot:

```sh
mv ~/.config/labwc/rc.xml.visona-kiosk.bak ~/.config/labwc/rc.xml
```

If there is no backup, `install-kiosk.sh` created `~/.config/labwc/rc.xml` from the system copy; remove it instead.

## Troubleshooting

| Problem | What to try |
| --- | --- |
| Black screen after login | Log in via SSH, run `./scripts/pi/visona-kiosk.sh` and read errors; check `DISPLAY=:0`. |
| Visona does not start at login | Confirm `~/.config/autostart/visona-kiosk.desktop` exists; log out and in (not just reboot to text console). |
| Screen goes blank | Wrapper runs `xset`; install `x11-xserver-utils` if missing: `sudo apt install x11-xserver-utils`. |
| System menubar still visible | Kiosk uses `setFullScreen(true)` on Linux so labwc hides the panel. Press **f** on a keyboard once to check, and update to the latest release or rebuild after `git pull`. As a fallback, `./scripts/pi/install-kiosk.sh` disables `wf-panel-pi` autostart. |
| `server does not have extension for -dpms` | Harmless on Wayland/XWayland; screen blanking is handled elsewhere. |
| Window not fullscreen, or small with a title bar | Update to the latest release or rebuild after `git pull`. Kiosk asks the compositor for fullscreen and follows display changes (D-103). |
| Quit without a close button | From SSH: `pkill Visona`. Kiosk mode has no window chrome. |
| Wrong audio device | Open Settings once with keyboard/touch and choose Babyface; the choice is saved at once. |
| Pinch does not zoom, but tap and drag work | labwc still emulates a mouse. Check that `~/.config/labwc/rc.xml` has `mouseEmulation="no"` on its `<touch>` lines, and reboot (see [Touchscreen](#touchscreen)). |

## Limitations

- Requires the **desktop session** (X11 / XWayland). Pure headless or Wayland-only kiosk is not supported yet.
- **Escape** still resets zoom; there is no PIN or lock screen.
