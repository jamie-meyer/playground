# U4323QE KVM and multi-input control

This is the model-specific map for the connected U4323QE. Values marked
live were read through our own USB HID client. The Dell installer was never
installed or executed.

## What can and cannot be resized

The monitor has two unrelated ways to divide its panel:

- **Screen Partition** uses DisplayPort MST to make one computer see two,
  three, or four logical monitors. It does not combine two computers.
- **PIP/PBP** combines independent physical inputs. This is the mode relevant
  to USB-C plus HDMI.

The connected monitor advertises these PIP/PBP values through VCP `0xE9`:

| Name in `dellctl` | Value | Windows |
| --- | ---: | ---: |
| `off` | `0x00` | 1 |
| `pip-small` | `0x21` | 2 |
| `pip-large` | `0x22` | 2 |
| `side-by-side` | `0x24` | 2 |
| `top-bottom` | `0x2F` | 2 |
| `three-a`…`three-e` | `0x31`…`0x35` | 3 |
| `quad` | `0x41` | 4 |

Dell's generic DDPM mode table also contains 25:75, 33:67, and other
two-window ratios (`0x25`…`0x2E`). The U4323QE does **not** advertise those
values. Its two-computer regions can be changed among the fixed modes above,
but there is no exposed continuous divider or arbitrary sizing control.

Values `0x01` and `0x02` are advertised but have no user-facing name in the
current DDPM mode map. They remain intentionally unnamed until a controlled
live test establishes their behavior.

## Inputs and window placement

VCP `0x60` selects the main input:

| Input | Value |
| --- | ---: |
| USB-C | `0x1B` |
| DP1 | `0x0F` |
| DP2 | `0x13` |
| HDMI1 | `0x11` |
| HDMI2 | `0x12` |

VCP `0xE8` packs the other three window inputs into 5-bit fields:

```text
window 2 | (window 3 << 5) | (window 4 << 10)
```

The live value is `0x6F71`, which decodes to HDMI1, USB-C, USB-C for windows
2–4. These are cached assignments while PIP/PBP is off.

VCP `0xE5` swaps two visible positions. Its payload is:

```text
0xF000 | ((window A - 1) << 4) | (window B - 1)
```

## USB/KVM mapping

The monitor's four upstream connections are numbered USB-C1 through USB-C4.
USB-C1 is the video-capable 90 W USB-C input; USB-C2–4 are data-only upstream
connections.

VCP `0xE7` associates DP1, DP2, HDMI1, and HDMI2 with those upstreams:

```text
((DP1 - 1) << 12) |
((DP2 - 1) << 10) |
((HDMI1 - 1) << 8) |
((HDMI2 - 1) << 6)
```

The live value `0x1640` decodes as:

| Video input | USB upstream |
| --- | --- |
| DP1 | USB-C2 |
| DP2 | USB-C2 |
| HDMI1 | USB-C3 |
| HDMI2 | USB-C2 |

Writing `0xFF00` to `0xE7` asks the monitor to advance the current USB owner.
This is the fastest monitor-mediated keyboard/mouse-only handoff when both
computers remain visible in PBP, because it avoids a video input or layout
change.

## Fast full-screen USB-C / HDMI1 switching

For this setup, a single input write is the important fast path:

```text
USB-C: VCP 0x60 <- 0x001B
HDMI1: VCP 0x60 <- 0x0011
```

The existing HDMI1-to-USB-C3 association lets the monitor move its USB hub
with the selected video input. A hotkey agent should run on both computers:

```sh
# On the computer whose video is HDMI1:
./build/dellctl hotkey --enable-writes usb-c

# On the computer whose video is the monitor's USB-C input:
./build/dellctl hotkey --enable-writes hdmi1
```

Both agents intercept the exact global `Option-A` chord and suppress it from
the foreground application. Each sends its predetermined target immediately,
without a preliminary monitor read. macOS requires Accessibility permission
for this locally compiled binary; it does not require root or a user group.

Test the source command once before enabling the hotkey:

```sh
./build/dellctl switch-input --enable-writes usb-c
./build/dellctl switch-input --enable-writes hdmi1
```

The command can move the monitor's keyboard and mouse away from the machine
that invokes it. Have the agent running on the destination computer, or be
ready to return with the monitor's OSD.

## Where switch latency comes from

The direct command removes OSD navigation and DDPM UI delays. The remaining
time is mostly physical:

1. the scaler locks onto the already-driven USB-C or HDMI signal;
2. the monitor disconnects its downstream USB hub from one host;
3. the other host enumerates the hub, keyboard, mouse, Ethernet, storage,
   and any other attached devices;
4. the destination OS delivers the first input event.

For the shortest practical handoff:

- keep both computers awake and continuously driving their video outputs;
- enable the monitor's Fast Wake feature;
- connect each monitor upstream to a native USB-C controller when practical;
- attach only the keyboard and mouse, or one receiver, to the moving hub;
- avoid USB storage on that hub, as Dell explicitly warns against switching
  while storage is attached;
- move Ethernet and other slow-to-enumerate devices off the switching hub if
  interruptions matter;
- do not change `0xE9` during a normal source handoff.

Firmware M2T102 adds “USB-C Switch When PC Sleeps.” That helps automatic
sleep behavior but does not remove normal USB re-enumeration. A firmware
update is not required for the direct hotkey and should be treated as a
separate, explicit operation.

## Observed Mac Studio latency

On the tested M2 Ultra Mac Studio, the Dell upstream was initially connected
through a built-in USB-A port backed by an ASMedia ASM3142 controller. Returning
the KVM to HDMI1 produced a deterministic controller recovery:

```text
ASMedia controller reports lost power
5.001 s forcePowerGated timeout
Dell USB hubs enumerate
keyboard and receiver drivers attach
```

Moving the same Dell upstream to a native Mac Studio USB-C port eliminated the
five-second controller timeout. The measured interval from the first Dell USB
2 hub enumeration to the final Logitech receiver HID attachment was about
1.12 seconds and overlapped the video switch enough to feel immediate.

The first replacement cable negotiated only USB 2.0. That is sufficient for
keyboard and mouse latency, but a USB 3.x-capable C-to-C data cable is required
for full Dell Ethernet and high-speed downstream bandwidth. A future
diagnostic command should report the host controller, negotiated speed, hub
depth, and attached devices before enabling a hotkey.
