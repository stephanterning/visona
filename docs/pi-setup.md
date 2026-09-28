# Raspberry Pi setup for Visona

This guide prepares a **Raspberry Pi 4** with a **7" display**, **WiFi**, **SSH**, and an **RME Babyface Pro FS** so you can build and run Visona. It assumes you prepare the SD card on a **Mac** (card reader adapter) and want to avoid a keyboard and mouse after the first boot.

For compiling and running Visona on the Pi, see [pi-build.md](pi-build.md).

Kiosk mode (autostart, fullscreen on boot) is **not** implemented yet. After this guide you can SSH from the Mac, build Visona once, and launch it from the desktop or over SSH with `DISPLAY` set.

## What you need

| Item | Notes |
| --- | --- |
| Raspberry Pi 4 | Any RAM size is fine for a first build |
| MicroSD card (64 GB) | Flash with Raspberry Pi Imager |
| Official Pi power supply | 5 V / 3 A USB-C |
| 7" display | HDMI; if it has **USB touch**, note that for later (Visona is touch-first) |
| HDMI cable | |
| Mac with SD adapter | For Raspberry Pi Imager |
| Babyface Pro FS | Class Compliant mode; see below |
| Powered USB hub or Babyface PSU | Often required; Pi USB power is marginal for the interface |
| MIDI source (optional) | Same chain as on Mac (e.g. UFX III → DIN → Babyface) |

## Step 1 — Flash Raspberry Pi OS (on the Mac)

1. Download and install **[Raspberry Pi Imager](https://www.raspberrypi.com/software/)** on the Mac.
2. Insert the microSD card.
3. In Imager, choose:
   - **Device:** Raspberry Pi 4
   - **OS:** **Raspberry Pi OS (64-bit)** — the desktop version (Bookworm or later), not Lite, so you get a GUI on the 7" screen.
   - **Storage:** your SD card
4. Open **OS customization** (gear icon or “Edit settings”):
   - **General:** set hostname (e.g. `visona-pi`), username and password
   - **WiFi:** SSID, password, country
   - **Services:** enable **SSH** — prefer **Allow public-key authentication only** and paste your Mac’s public key (`~/.ssh/id_ed25519.pub` or `id_rsa.pub`). Generate one with `ssh-keygen -t ed25519` if needed.
   - **Locale:** timezone and keyboard as you prefer
5. Write the image and wait until verification finishes.
6. Eject the card, insert it in the Pi, connect **HDMI**, **power**, and **Ethernet or WiFi** (WiFi credentials are already on the card).

## Step 2 — First boot (no keyboard)

1. Power on the Pi. Wait until the desktop appears on the 7" screen (first boot can take a few minutes).
2. On the Mac, find the Pi:
   - `ping visona-pi.local` (or the hostname you chose), or
   - your router’s DHCP list, or
   - `ssh <user>@<hostname>.local`
3. Confirm SSH works:

   ```sh
   ssh <user>@visona-pi.local
   ```

If SSH fails, connect a keyboard once to check WiFi, or re-flash with corrected credentials.

## Step 3 — System updates and audio group (SSH)

On the Pi (SSH session):

```sh
sudo apt update
sudo apt full-upgrade -y
sudo usermod -aG audio "$USER"
```

Log out of SSH and reboot so the `audio` group applies:

```sh
sudo reboot
```

## Step 4 — Babyface Pro FS (Class Compliant mode)

RME does **not** ship Linux drivers for the Babyface Pro FS. On Linux (including Pi), use **Class Compliant (CC) mode** — the same idea as on iPad:

1. **Power off** the Babyface (unplug USB).
2. Hold **SELECT** and **DIM** while connecting USB power (or while plugging into a **powered hub**).
3. Level meters should **run up** to confirm CC mode. Normal Mac/Windows driver mode uses a single LED per side at power-on instead.
4. Connect the Babyface to the Pi via a **powered USB hub** or use RME’s external PSU. Avoid relying on the Pi’s USB port alone.
5. Prepare **routing and clock on the Mac** in advance (TotalMix is not available in CC mode the same way as with RME drivers). ADAT/SPDIF inputs used on Mac should be configured on the unit or in CC as needed.

Verify on the Pi:

```sh
arecord -l
aplay -l
```

You should see a USB audio device (often named **Babyface** or similar). List MIDI ports:

```sh
aconnect -l
```

## Step 5 — Build dependencies and Visona

Follow [pi-build.md](pi-build.md):

1. Install `apt` packages listed there (build tools + JUCE/X11 deps).
2. Clone the repo and check out the branch with the Pi preset (e.g. `cursor/pi-port-149a` until merged):

   ```sh
   git clone https://github.com/stephanterning/visona.git
   cd visona
   git checkout cursor/pi-port-149a
   cmake --preset pi
   cmake --build --preset pi
   ```

3. Run from a **desktop session** on the Pi (local terminal or SSH with display forwarding is awkward; prefer the 7" screen):

   ```sh
   ./build/pi/app/Visona_artefacts/Release/Visona
   ```

4. In **Settings**, choose the Babyface ALSA device and the correct input channels (e.g. ADAT/SPDIF). Select the MIDI port if you use MIDI Clock.

Settings are stored under `~/.config/Visona/`.

## Step 6 — Optional checks without a keyboard

| Check | How |
| --- | --- |
| Network | SSH from Mac |
| Audio device | `arecord -l` over SSH |
| MIDI ports | `aconnect -l` over SSH |
| Run Visona | Tap the desktop terminal icon, or add a desktop shortcut later |
| Touch | If the 7" panel is USB touch, it should work as mouse events in the desktop |

Diagnostics in Visona: press **D** (needs a keyboard once, or map touch to keyboard later).

## What is not ready yet

- **Autostart / fullscreen on boot** — planned for the Pi appliance milestone; not in the current Pi port.
- **Auto-select Babyface** — same as macOS MVP: last saved device is restored; if missing at startup, Visona shows **NO AUDIO INPUT** (D-064).
- **Headless-only** — Visona needs X11/XWayland; the 7" HDMI desktop session is the right test setup.

## Troubleshooting

| Problem | Things to try |
| --- | --- |
| Pi not on network | Re-flash with correct WiFi; temporary keyboard + `raspi-config` |
| SSH permission denied | Re-flash with correct public key or password auth |
| Babyface not in `arecord -l` | CC mode (SELECT+DIM), powered hub, different USB port |
| Silence in Visona | `audio` group, correct ALSA device in Settings, routing set on Mac/CC |
| Build fails on Pi | See [pi-build.md](pi-build.md) troubleshooting; first build can take 30+ minutes |

## Next steps after this guide

1. Confirm waveform and MIDI sync on Pi match your Mac PoC.
2. Report CPU use (diagnostics overlay) at 48 kHz and your typical window size.
3. When the Pi port merges, we can add **kiosk autostart** so power-on goes straight to fullscreen Visona without SSH.
