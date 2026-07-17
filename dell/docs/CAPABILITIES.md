# U4323QE control surface

Status labels:

- **Documented**: present in Dell's U4323QE user guide or DDPM administration
  guide.
- **SDK-visible**: an operation exists in Dell's monitor SDK; this does not by
  itself prove that the U4323QE implements it.
- **Live-confirmed**: successfully read from this connected monitor with our
  client.

## Documented U4323QE controls

| Area | Controls |
| --- | --- |
| Picture | Brightness, contrast, sharpness, response time, underscan, aspect ratio (`16:9`, Auto Resize, `4:3`, `1:1`) |
| Color | Presets, 5000–10000 K color temperature, custom RGB gains, RGB/YCbCr input format |
| Inputs | USB-C (90 W), DP1, DP2, HDMI1, HDMI2, auto-select, USB-C auto-select, input renaming |
| USB-C | High Resolution / High Data Speed prioritization, charging while off, DPBS |
| Multi-view | PIP/PBP, internal MST partitions, source per Window 1–4, video swap, per-window USB switch |
| Audio | Volume, speaker mute, Window 1–4 audio source |
| KVM / USB | Four upstream USB-C connections and input-to-upstream associations |
| OSD | Language, transparency, timer, lock |
| Power / service | Power LED, standby USB charging, fast wake, DDC/CI, HDMI CEC, LCD conditioning, diagnostics and resets |

The eight layouts shown by the monitor manual include two-, three-, and
four-window arrangements, including four equal 1920×1080 quadrants.

## SDK-visible operations

Static SDK inspection exposes substantially more than DDPM's normal UI:

- raw Get/Set DDC/CI commands;
- power, brightness, contrast, input, aspect, sharpness and response;
- preset, gamma, gamut, white point, hue, saturation and custom color;
- PIP/PBP layout, inputs, audio, brightness, contrast, sharpness, gamma,
  gamut, white point, video range and zoom;
- USB association/switching and video swap;
- OSD settings, uniformity, LCD conditioning and MST-related operations.

Support must be determined per model through the live capability string and
non-mutating reads. The SDK also contains colorimeter and calibration
operations intended for other Dell monitor families; they must not be
presented as U4323QE features without live evidence.

## Live standard and Dell VCP values

All 38 opcodes in the live capability string returned a syntactically valid
VCP reply. The controls most relevant to the client are:

| Opcode | Meaning | Live value |
| ---: | --- | --- |
| `0x10` | Brightness | `6 / 100` |
| `0x12` | Contrast | `75 / 100` |
| `0x14` | Color preset | `0x05` |
| `0x16/18/1A` | RGB gains | `100 / 100` each |
| `0x60` | Main input | raw `0x8411`; low byte `0x11` = HDMI1 |
| `0x62` | Volume | `6 / 100` |
| `0x8D` | Audio mute | `0x01` |
| `0xD6` | Power mode | `0x01` |
| `0xDF` | MCCS version | `0x0201` |
| `0xE5` | Video swap command | read value `0x0000` |
| `0xE7` | USB/KVM association | `0x1640` |
| `0xE8` | PBP sub-inputs | `0x6F71` |
| `0xE9` | PIP/PBP mode | `0x0000` = off |

The exact KVM and multi-input encodings are in
[`KVM_PBP.md`](KVM_PBP.md), and the full live capability string is preserved
in [`../profiles/u4323qe.json`](../profiles/u4323qe.json).

## Live PIP/PBP limits

The monitor advertises `E9(00 01 02 21 22 24 2F 31 32 33 34 35 41)`.
Cross-referencing Dell's current mode-name table establishes named support
for:

- off;
- small and large PIP;
- equal/fill side-by-side and top/bottom PBP;
- five three-window layouts;
- four equal quadrants.

Dell's generic ratio modes `0x25` through `0x2E` are absent. Therefore the
two physical computer regions have fixed presets, not a continuously
movable divider. This is a model capability limit, not a limitation of the
client.
