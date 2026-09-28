# Visona kiosk autostart on Raspberry Pi

This guide makes a Raspberry Pi boot into the desktop and start **Visona fullscreen** without SSH or a manual launch.

Requires a built Pi binary — see [pi-build.md](pi-build.md) and [pi-setup.md](pi-setup.md).

## What gets installed

1. **`--kiosk` flag** — Visona starts borderless and fullscreen (close button only).
2. **`scripts/pi/visona-kiosk.sh`** — disables X11 screen blanking, then launches Visona.
3. **`~/.config/autostart/visona-kiosk.desktop`** — starts the wrapper when the desktop session loads.

Autologin (boot straight to desktop without a login prompt) is optional and configured separately.

## One-time setup

After building on the Pi:

```sh
cd ~/visona
git pull
cmake --build --preset pi   # if not already built
./scripts/pi/install-kiosk.sh --enable-autologin
sudo reboot
```

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

Or:

```sh
./build/pi/app/Visona_artefacts/Release/Visona --kiosk
```

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

## Troubleshooting

| Problem | What to try |
| --- | --- |
| Black screen after login | Log in via SSH, run `./scripts/pi/visona-kiosk.sh` and read errors; check `DISPLAY=:0`. |
| Visona does not start at login | Confirm `~/.config/autostart/visona-kiosk.desktop` exists; log out and in (not just reboot to text console). |
| Screen goes blank | Wrapper runs `xset`; install `x11-xserver-utils` if missing: `sudo apt install x11-xserver-utils`. |
| Window not fullscreen | Use `--kiosk`; press **F** toggles fullscreen in non-kiosk runs. |
| Wrong audio device | Open Settings once with keyboard/touch, save Babyface, restart. |

## Limitations

- Requires the **desktop session** (X11 / XWayland). Pure headless or Wayland-only kiosk is not supported yet.
- No automatic **hotplug** if the interface is plugged in after boot (MVP behaviour, D-033).
- **Escape** still resets zoom; there is no PIN or lock screen.
