# Pebble Index 01 — CFW

A minimal alternative firmware for the **Pebble Index 01** smart ring
(Renesas DA14535 Bluetooth LE SoC).

> ⚠️ Unofficial, experimental firmware. Not affiliated with Pebble or Core
> Devices. Flashing it is at your own risk.

## What it does

It is intentionally *featureless* — a small, working foundation rather than a
replacement for the official firmware. Out of the box it:

- Advertises over BLE as **`Pebble Index CFW`**.
- Counts button presses and exposes the count in its advertising data, so a
  click is visible from any BLE scanner without connecting.
- Uses a slow advertising interval to keep battery drain low while staying
  discoverable.
- Can hand the ring back to its **failsafe** image — either with a gesture
  (five quick clicks) or a BLE command — and never overwrites the failsafe
  bootloader, so the ring stays recoverable.

That is the whole feature set.

## How it fits on the ring

The CFW does **not** replace the ring's whole software stack. It is delivered the
same way the official firmware ships updates — as an image pushed over BLE through
the ring's sync/SUOTA update flow — and it lands in the **primary application image
slot**, running in place of the factory app. Validation is by image header (magic
+ valid flag) and a CRC32 over the body, not a cryptographic signature. The
lower-level **failsafe bootloader** underneath is never written, so the ring keeps
its built-in recovery path at all times.

Going back to the official app needs no flashing tools: **five quick clicks** (or
the BLE control-point command) invalidate the CFW image, so the next boot drops
into the failsafe. From there the official app restores the stock firmware
automatically — the same recovery flow the ring already uses when it finds a
failsafe ring in range.

## Test kit

Before flashing a ring, the firmware is validated on the
**DA14535-00FXDEVKT-U** (SmartBond DA14535 USB Development Kit). It carries the
**same SoC as the ring** (DA14535), so BLE and the click counter are exercised
on real silicon first. On the kit the button is SW2 (P0_11); on the ring it is
P0_1.

## Building

Requires the `arm-none-eabi` GCC toolchain and the Renesas **SDK 6.0.22.1401**.
The SDK is proprietary and cannot be redistributed, so it is **not** included
here — download it from Renesas.

```sh
./build-linux.sh <GCC_TOOLCHAIN_PATH> <DIALOG_SDK_PATH>
```

Output: `build/DA14531_App.bin` (and `.hex`).

## Flashing & recovery

The companion app that puts this firmware on a ring is
[**pebble-index-flasher**](https://github.com/elvisoliveira/pebble-index-flasher) —
an offline Android app that flashes the CFW, restores the official firmware, and
drives the failsafe recovery, all over BLE. It bundles the latest CFW release
(built from this repo) so no PC or cable is needed.

## License

MIT — see [`LICENSE`](LICENSE). Based on
[stawiski/da14531-cmake-template](https://github.com/stawiski/da14531-cmake-template).
The Renesas DA145xx SDK is proprietary and used under its own license.
