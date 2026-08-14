# WiFiProvision

[![Compile Examples](https://github.com/ngk0/WiFiProvision/actions/workflows/compile-examples.yml/badge.svg)](https://github.com/ngk0/WiFiProvision/actions/workflows/compile-examples.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

On-device WiFi setup for SAMD boards with an ATWINC1500. Scan list, on-screen
keyboard, credential store and a connect flow, so the SSID and password are not
compiled into the sketch.

ESP32 has WiFiManager and everyone uses it. WiFi101 has nothing equivalent.
This is that.

```cpp
wp.begin(&tft);

if (!wp.restore(credStore.read()) || !wp.connectStored())
  wp.start();                       // scan and show the picker

// in loop()
if (pressEdge(&x, &y)) wp.touch(x, y);
wp.handleSerial(Serial);
if (wp.justConnected()) credStore.write(wp.credentials());
```

## What you get

**A picker** listing what the radio can see, with signal strength and
encryption type.

**A keyboard** with three layers: lower case, upper case and symbols. The
symbol layer is not decoration. A WPA password with a punctuation mark in it is
ordinary, and a keyboard that cannot type one is useless for the people who
need it most. Shift falls back after one letter, the way a phone's does.

**A credential record** with a magic number, a version and a CRC. An erased
flash page reads back as all ones or all zeros and neither is mistaken for
valid data, which is what stops a failed write from turning into a device that
tries to join a network named after garbage.

**A serial interface** mirroring every on-screen action:

```
list | scan | pick N | pass TEXT | connect | forget | status
```

That exists so the flow can be tested without a finger on the glass, and
because it is the only way back when a touch panel is miscalibrated badly
enough that the on-screen keyboard cannot be used.

**The password shown in the clear.** This is a device on a bench being set up
by the person holding it. A row of asterisks makes a mistyped character on a
resistive panel impossible to find, and hides nothing from anyone who is not
already looking at your bench.

## What it does not do

**It blocks.** WiFi101 scanning takes a couple of seconds and association takes
up to its timeout, and neither can be made cooperative without rewriting the
driver. The library paints a status screen before it blocks, so the device does
not look dead, but your loop stops for the duration. If you have something that
must keep running, this is not the library for it and no amount of API design
around it would change that.

**No captive portal.** WiFiManager's trick of standing up an access point and a
web server is not practical on a WINC1500 with 32 KB of SRAM alongside a
display. The screen is the interface here.

**2.4 GHz only.** That is the radio, not the library. A 5 GHz network will not
appear in the scan, and the failure screen says so, because that is the reason
about half of failed attempts fail.

## Install

```
git clone https://github.com/ngk0/WiFiProvision.git
```

Requires `WiFi101` and `Adafruit_GFX`. The example also uses
`Adafruit_ILI9341`, `Adafruit_STMPE610` and `FlashStorage`, but the library
itself depends on none of those: touch coordinates and persistence are both
supplied by you.

## Touch and persistence are yours

The library never talks to a touch controller. You call `touch(x, y)` with
screen coordinates from whatever you have, so STMPE610, TSC2007 and XPT2046 all
work the same way.

It never writes to storage either. `credentials()` hands back a `WPCreds`,
which is plain old data, and you put it wherever you like. The example uses
`FlashStorage`, whose reserved page is erased on every sketch upload, so
credentials there survive power cycles and resets but not reflashing.

For a touch calibration that does not require you to work out by hand which
panel axis is swapped and which is inverted, see
[TouchTune](https://github.com/ngk0/TouchTune).

## API

| Method | Purpose |
| --- | --- |
| `begin(gfx)` | Bind to a display and lay out for its size |
| `start()` | Scan and show the picker. Blocks for the scan |
| `scan()` | Scan only. Blocks. Returns the count |
| `addNetwork()` / `clearNetworks()` | Inject a list, for tests with no radio |
| `select(i)` | Choose a network and open the keyboard |
| `setPassword(s)` | Set the password without typing it |
| `connect()` / `connectStored()` | Associate. Blocks until joined or timed out |
| `touch(x, y)` | Deliver a press in screen coordinates |
| `handleSerial(io)` | Run the command interface |
| `justConnected()` | True once after success, so you know when to save |
| `restore(c)` / `credentials()` | Load and read the credential record |
| `forget()` | Clear and disconnect |
| `makeCreds()` / `crcOf()` | Build and check a record, for tests |

## Verification status

Both examples compile for the Feather M0. `Provision` is 61,360 bytes and
`SelfTest` 55,256.

**Neither has been run.** The board used to develop this wedged its USB
endpoint partway through the session and has been stuck in its bootloader
since, so nothing here has executed on hardware. I am not going to describe
untested code as working.

The `SelfTest` example is built to make checking it cheap when a board is
available. It needs no radio and no touch panel: networks are injected, touches
are delivered as coordinates, and it draws into a `GFXcanvas1` rather than a
display. It covers the credential record including erased-page rejection, the
network list including its bounds, selection, the keyboard across all three
layers, delete past empty, and over-long passwords.

It prints machine-readable results for
[arduino-hil](https://github.com/ngk0/arduino-hil), so verification is one
command:

```
python hil.py run tests.toml
```

`tests.toml` in this repo also has a commented-out end-to-end test that drives
the whole provisioning flow over serial against a real network.

## Licence

MIT. See [LICENSE](LICENSE).
