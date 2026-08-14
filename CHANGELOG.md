# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0]

First release.

### Added

* Network picker with signal strength and encryption type
* On-screen keyboard with lower case, upper case and symbol layers, shift
  falling back after one letter
* `WPCreds`, a plain-old-data credential record with magic number, version and
  CRC16, rejecting all-zero and all-ones pages so a failed write cannot be
  mistaken for data
* Connect flow with a timeout, and a failure screen that names the 2.4 GHz
  limitation, which accounts for a large share of failed attempts
* Serial command interface mirroring every on-screen action, so the flow is
  testable and there is a way back from a miscalibrated panel
* Layout derived from the display dimensions rather than hardcoded
* Touch input and persistence both supplied by the caller, so any touch
  controller works and the library has no opinion about storage
* `addNetwork()` and `clearNetworks()` for exercising the picker and keyboard
  with no radio present
* Examples: `Provision`, `SelfTest`
* `tests.toml` for arduino-hil

### Notes

Both examples compile for the Feather M0. Neither has been run: the development
board wedged its USB endpoint and has been stuck in its bootloader since. See
the verification section of the README.
