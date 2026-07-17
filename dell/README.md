# Dell monitor control research

This directory is a clean-room-style research prototype for communicating
with the connected Dell U4323QE from macOS.

An optional local Dell package in `DDPMv2.3.0.0023/` was used as static
reference material only and is excluded from Git. It is not linked, loaded,
installed or executed by this project. `dellctl` is original source compiled
locally with Apple's public ApplicationServices, CoreFoundation and IOKit
APIs.

## Current result

The useful control path is the monitor's integrated `0424:7260` `HID I2C`
interface. This bypasses video-link DDC, which is unreliable on the M2 Ultra
HDMI path in use here. The protocol and command layers are documented in
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

The client can read the complete monitor capability surface and exposes
model-specific names for the U4323QE's KVM, source, PIP/PBP and video-swap
registers:

```sh
make
./build/dellctl self-test
./build/dellctl list
./build/dellctl get-vcp 0x10
./build/dellctl capabilities
./build/dellctl probe
./build/dellctl status
./build/dellctl layout-list
```

Every mutation requires the literal `--enable-writes`. Input switching and
the hotkey path have been exercised against the live monitor; other mutations
remain opt-in:

```sh
./build/dellctl switch-input --enable-writes usb-c
./build/dellctl switch-input --enable-writes hdmi1
./build/dellctl hotkey --enable-writes usb-c
./build/dellctl kvm-next --enable-writes
./build/dellctl kvm-map --enable-writes 2 2 3 2
./build/dellctl pbp-layout --enable-writes side-by-side
./build/dellctl pbp-source --enable-writes 2 usb-c
./build/dellctl video-swap --enable-writes 1 2
./build/dellctl set-vcp --enable-writes 0x10 50
```

For the requested rapid two-computer handoff, run the hotkey agent on both
computers: target `usb-c` on the HDMI1 computer and target `hdmi1` on the
USB-C computer. The exact global `Ctrl-A` chord is consumed by the agent.
See [`docs/KVM_PBP.md`](docs/KVM_PBP.md) before the first live switch.

## Research map

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md): byte-level HID, I2C, private Dell and
  raw DDC/CI framing.
- [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md): documented controls,
  SDK-visible operations and live verification status.
- [`docs/KVM_PBP.md`](docs/KVM_PBP.md): exact layout, input, USB association,
  rapid hotkey and latency map.
- [`profiles/u4323qe.json`](profiles/u4323qe.json): machine-readable model
  profile and live snapshot.
- [`tools/macho_objc_methods.js`](tools/macho_objc_methods.js): static Mach-O
  Objective-C metadata reader used to inspect the downloaded app as data.

## Safety and provenance

The local Dell package in `DDPMv2.3.0.0023/` remains untracked reference
material only. It is not linked, loaded, installed or executed. `dellctl` is
original C source compiled locally with public macOS frameworks.

The direct USB-C/HDMI1 source path and global hotkey are live-tested. Further
work can add structured handoff timing and configuration diagnostics.
