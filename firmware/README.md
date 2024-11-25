<p align="center">
    <h1 align="center">Nervous ECG Firmware</h1>
</p>

<p align="center">
    <img alt="C Language" src="https://img.shields.io/badge/C-00599C?logo=C&logoSize=auto" />
    <a href="https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK">
        <img alt="nRF Connect SDK v2.6.0" src="https://img.shields.io/badge/v2.6.0-grey?label=nRF%20Connect%20SDK&labelColor=%2300A9CD">
    </a>
    <a href="https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html">
        <img alt="Zephyr SDK v0.16.5" src="https://img.shields.io/badge/v0.16.5-grey?label=Zephyr%20SDK&labelColor=%237929D2">
    </a>
    <a href="https://opensource.org/licenses/MIT">
        <img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-yellow.svg" />
    </a>
</p>

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Overview](#overview)
- [Instructions](#instructions)
  - [SDK Versions](#sdk-versions)
  - [Install SDK](#install-sdk)
  - [Dependencies](#dependencies)
  - [Build](#build)
- [Release](#release)

---

## Overview

The firmware for the nRF52840, which resides in the ISP1807, is developed using the Zephyr project. Specifically, the entire source code is a standalone application compatible with the nRF Connect extension for VS Code. A `west.yml` file has been added to enable the compilation of the project outside the extension, as a native Zephyr project.

---

## Instructions

### SDK Versions

- **nRF Connect SDK**: v2.6.0
- **Zephyr SDK**: v0.16.5

### Install SDK

On Windows, the easiest way to develop and build applications for the nRF platform is by installing the **nRF Connect SDK** and **nRF Connect for VS Code** through the [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) application.

Alternatively, you can install the [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) and build the project using `west`.

### Dependencies

As required by Zephyr, all dependencies necessary to build a Zephyr project must be installed as described in the [Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

Additionally, this project uses [Protocol Buffers](https://protobuf.dev/) to serialize data for transmission over Bluetooth Low Energy. The [Protocol Buffers compiler](https://docs.zephyrproject.org/latest/services/serialization/nanopb.html) must be installed to generate source and header files from `.proto` files.

### Build

If using nRF Connect for VS Code, simply open the `firmware` folder with VS Code, and it will be automatically recognized as an nRF Connect SDK application by the nRF Connect extension.

- Ensure the toolchain matches the nRF Connect SDK version mentioned above.
- Create a new build configuration and select `ecg_board` as the board and `proj.conf` as the base configuration file.
- Add `overlay-ota.conf` as an extra K-config fragment if you wish to enable over-the-air updates for the sensor, so you won't need to disassemble the device for updates after the initial programming.

If using the Zephyr SDK directly, prepare a parent folder for the `firmware` folder. Run the following commands in this parent folder to download the proper nRF Connect SDK and build the firmware:

```console
west init -l firmware
west update --narrow -o=--depth=1
west build firmware --board ecg_board --pristine --build-dir ./build -- -DNCS_TOOLCHAIN_VERSION=NONE -DBOARD_ROOT=./ -DCONF_FILE=prj.conf -DEXTRA_CONF_FILE=overlay-ota.conf
```

`--pristine` is optional and rebuilds the whole project from scratch. It can be removed if you are just rebuilding after modifications.

`--build-dir ./build` is optional and creates a `build` directory in the parent folder. It can be removed to use the default output build directory.

`-DEXTRA_CONF_FILE=overlay-ota.conf` is optional and enables over-the-air updates for the sensor, which eliminates the need to disassemble the device for updates after the initial programming.

---

## Release

The firmware is built continuously in CI at each release. Zephyr output files from the build are made available in the [release](https://github.com/sensors-inl/Nervous-ECG/releases/latest):

- `Nervous-ECG-firmware-full-vX.X.X.hex`: The full firmware to program the nRF52840 in the ISP1807 (originally the `merged.hex` file from Zephyr output).
- `Nervous-ECG-firmware-ota-V=vX.X.X.bin`: The OTA binary to send for an update if the sensor is already programmed (originally the `app_update.bin` file from Zephyr output).

---
