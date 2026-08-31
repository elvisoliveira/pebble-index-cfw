# Pebble Index 01 CFW

A minimal alternative firmware for the **Pebble Index 01** smart ring
(Renesas DA14535 Bluetooth LE SoC).

> ⚠️ Unofficial, experimental firmware. Not affiliated with Pebble or Core
> Devices. Flashing it is at your own risk.

## What it does

It is intentionally *featureless*: a small, working foundation rather than a
replacement for the official firmware. Out of the box it:

- Advertises over BLE as **`Pebble Index CFW`**.
- Counts button presses and exposes the count in its advertising data, so a
  click is visible from any BLE scanner without connecting.
- Uses a slow advertising interval to keep battery drain low while staying
  discoverable.
- Can hand the ring back to its **failsafe** image with a gesture (five quick
  clicks) or a BLE command. It never touches the failsafe bootloader (see
  [Can it brick?](#can-it-brick)).

That is the whole feature set.

## How it fits on the ring

The best analogy is a PC with a proprietary BIOS running Linux. The
**failsafe bootloader** is the BIOS here: it came with the ring, it belongs to
Core Devices, and this project never touches it. Everything above it is the
CFW. Once installed, nothing from the factory app is running anymore.

Installation happens the same way official updates do: the image is pushed
over BLE through the ring's normal sync/SUOTA update flow and lands in the
application slot, replacing the factory app. The ring checks the image header
(magic + valid flag) and a CRC32 over the body; there is no cryptographic
signature.

Getting back to stock takes two steps, and the CFW only handles the first one.
**Five quick clicks** (or a BLE command) mark the CFW image as invalid, and on
the next boot the ring wakes up in failsafe mode. The CFW cannot restore the
factory app by itself. From failsafe mode the official Pebble app takes over
and reinstalls the stock firmware, using the same recovery flow it already has
for failsafe rings.

Someday I would like to replace the failsafe too and have the ring running
100% FOSS, but for now that is just a plan.

## Can it brick?

Short answer: very unlikely, but yes, it can.

While the CFW is running, every line of code on the ring comes from this
project. A bad enough bug could leave it stuck, and at that point the only way
back would be SWD, wiring a debugger to the ring's test points. That means
opening the ring, so it stays a last resort, but it is a real path: the boot
ROM samples a hardware reset pin fixed to `P0_0` before it runs any firmware,
and on the ring that pin is the flash clock. Holding it high through power-up
keeps the core in reset, so firmware never runs and the debug port stays open.

That said, there are several safety nets that put the ring back into failsafe
mode on their own:

- Crashes, hangs and watchdog resets all land in the failsafe instead of
  freezing the ring.
- The failsafe keeps count: after 4 boots where the app never reports back as
  healthy, it gives up on the image and stays in recovery mode.
- And there are the manual escape hatches: the five-click gesture and the BLE
  command.

To actually brick the ring, an image would have to boot well enough to get
past all of that and then lose both the button and BLE. The releases here
should not be able to do that. Still, that is why this README says
"recoverable" and not "unbrickable".

## Test kit

Before flashing a ring, the firmware is validated on the
**DA14535-00FXDEVKT-U** (SmartBond DA14535 USB Development Kit). It carries the
**same SoC as the ring** (DA14535), so BLE and the click counter are exercised
on real silicon first. Build it with `-DKIT_DEFS=TARGET_KIT`.

The kit button is an **external momentary button on P0_7** (MikroBUS J3 pin 3 to
GND). The on-board SW2 is not usable on this build: it sits on P0_11, which the
RGB LED's channel C drives. On the ring the button is P0_1.

## Building

Requires the `arm-none-eabi` GCC toolchain and the Renesas **SDK 6.0.22.1401**.
The SDK is proprietary and cannot be redistributed, so it is **not** included
here. You will have to download it from Renesas yourself.

```sh
./build-linux.sh <GCC_TOOLCHAIN_PATH> <DIALOG_SDK_PATH>
```

Output: `build/DA14531_App.bin` (and `.hex`).

## Flashing & recovery

The companion app that puts this firmware on a ring is
[**pebble-index-flasher**](https://github.com/elvisoliveira/pebble-index-flasher),
an offline Android app that flashes the CFW and can put the ring back into
failsafe mode, all over BLE. It does not restore the stock firmware; the
official Pebble app does that once the ring is in failsafe mode. It bundles
the latest CFW release built from this repo, so no PC, cable or internet is
needed.

## Documentation

The [project wiki](https://github.com/elvisoliveira/pebble-index-cfw/wiki) documents the
ring itself: the [hardware](https://github.com/elvisoliveira/pebble-index-cfw/wiki/Inside-the-ring)
(chip, memory, pins, debug pads) and
[how it boots](https://github.com/elvisoliveira/pebble-index-cfw/wiki/How-the-ring-boots)
(the failsafe, image validation, and the ways back to it).

How firmware is actually written over Bluetooth is documented in the
[flasher wiki](https://github.com/elvisoliveira/pebble-index-flasher/wiki).

## License

MIT, see [`LICENSE`](LICENSE). Based on
[stawiski/da14531-cmake-template](https://github.com/stawiski/da14531-cmake-template).
The Renesas DA145xx SDK is proprietary and used under its own license.
