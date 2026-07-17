# U4323QE USB control protocol

This document separates facts observed on the connected U4323QE from
inferences and experiments. Dell software was inspected only as data; it was
not installed or executed.

## Confirmed hardware path

The monitor exposes a vendor-defined HID device behind its integrated
Microchip hub:

- USB VID:PID: `0424:7260`
- product string: `USB2 Controller Hub`
- usage page / usage: `0xFF / 0x01`
- input and output reports: 64 bytes
- HID descriptor has no declared report IDs

Dell calls this backend `UMCP7260`. It avoids the M2 Ultra's problematic
HDMI DDC transport and reaches the monitor controller over USB.

## HID-to-I2C reports

All reports are 64 bytes. Unused bytes are `0xFF`.

I2C write (`0x92`):

| Byte | Meaning |
| ---: | --- |
| 0 | `0x92` |
| 1 | 7-bit I2C address |
| 2 | payload size, at most 60 |
| 3 | `0xFF` if another chunk follows, otherwise `0x00` |
| 4… | payload |

I2C read request (`0x93`):

| Byte | Meaning |
| ---: | --- |
| 0 | `0x93` |
| 1 | 7-bit I2C address |
| 2…5 | requested size, little-endian `uint32` |

I2C read response (`0x94`):

| Byte | Meaning |
| ---: | --- |
| 0 | `0x94` |
| 2 | returned byte count |
| 4… | returned data, at most 59 bytes |

Dell uses I2C address `0x37` for both DDC writes and reads.

On open, the UMCP7260 backend is initialized to a 100 kHz I2C clock with:

```text
95 00 00 FF ... (64 bytes total)
```

This changes only the USB bridge's bus timing; it is required before monitor
queries and does not alter a saved monitor setting.

## Dell private DDC layer

A private read request without a session token is:

```text
51 (80 | (payload_length + 2)) EB command payload... checksum
```

The transmit checksum is XOR over `0x6E` and every preceding request byte.
A successful response is:

```text
6E (80 | response_body_length) 02 00 command data... checksum
```

The receive checksum is XOR over `0x50` and every preceding response byte.
Byte 3 is a status code. The current Dell SDK uses private command `0x30` for
brightness.

## Raw DDC/CI tunnel

Private command `0xFE` carries a complete DDC/CI packet. Dell prefixes the
packet with two zero bytes. For example, a standard Get VCP request for
brightness (`0x10`) is nested like this:

```text
51 89 EB FE 00 00 51 82 01 10 AC <outer-checksum>
```

The private response's data field is the standard 11-byte VCP reply. This
gives the client both Dell's richer private API and generic MCCS access over
the same USB bridge.

## Set VCP

Static inspection of Dell's `SetDDCCICommand` confirms that a standard Set
VCP packet uses the same two-zero-byte `0xFE` tunnel. The outer Dell envelope
changes from read opcode `0xEB` to write opcode `0xEA`:

```text
51 (80 | (payload_length + 2)) EA command payload... checksum
```

For brightness 50 (`VCP 0x10 <- 0x0032`), the inner MCCS packet is:

```text
51 84 03 10 00 32 9A
```

The complete private payload is `00 00` followed by that packet. After 100
ms, Dell's SDK requests a six-byte private acknowledgement inside an
eight-byte I2C read:

```text
6E 83 02 00 FE checksum 00 00
```

Only the first `(response[1] & 0x7F) + 3` bytes belong to the checksummed
message. This write path is statically confirmed and locally self-tested but
has not yet been exercised against the live monitor.

## Safety boundary in the prototype

The CLI requires the literal `--enable-writes` on every setting command.
Named PIP/PBP and input commands accept only values advertised by the live
U4323QE. The generic `set-vcp` escape hatch is intentionally more powerful
and should be used only with the live capability string in hand.

Read-back verification is used where the control remains reachable. Input,
power and USB-owner changes can legitimately move or disable the HID bridge
before a read-back; for those operations the private write acknowledgement
is the last reliable local result.
