# Solstice

![Solstice](solstice.svg)

Solstice is a self-hosted game stream host for [Moonlight](https://moonlight-stream.org). It captures your Windows
desktop or a single application, encodes it in hardware, and streams it with low latency to any Moonlight client —
phone, tablet, TV, or another computer. A web UI handles pairing, configuration, and logging from any browser on your
network.

[![GitHub stars](https://img.shields.io/github/stars/paulwongkwan/Sunshine.svg?logo=github&style=for-the-badge)](https://github.com/paulwongkwan/Sunshine)
[![Latest release](https://img.shields.io/github/v/release/paulwongkwan/Sunshine.svg?style=for-the-badge&logo=github)](https://github.com/paulwongkwan/Sunshine/releases/latest)
[![CI build](https://img.shields.io/github/actions/workflow/status/paulwongkwan/Sunshine/ci.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge)](https://github.com/paulwongkwan/Sunshine/actions/workflows/ci.yml?query=branch%3Amaster)

## Highlights

- **Windows 11 only.** x64 and ARM64 builds; Windows 11 build 22000 or newer is required, and older versions exit at
  startup with an explanatory message.
- **Hardware encoding.** NVENC on NVIDIA, AMF on AMD, QuickSync on Intel, Media Foundation on Qualcomm, with a software
  encoder as a fallback.
- **Desktop capture.** DXGI Desktop Duplication in both portable and service mode, Windows.Graphics.Capture in portable
  mode.
- **Virtual gamepads.** Xbox 360, Xbox One, Xbox Series, DualShock 4, DualSense, Switch Pro, and a generic gamepad through
  the [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver.
- **Web UI with sign-in.** Pair clients, change settings, and read logs from `https://localhost:47990`, protected by a
  login screen with an optional "remember me" session.
- **Sunshine-compatible.** Existing Sunshine configuration is picked up automatically, and the client, application, and
  state file formats are unchanged.

## Requirements

| Component | Requirement |
|:----------|:------------|
| OS | Windows 11 (build 22000) or newer, x64 or ARM64 |
| GPU | NVIDIA: NVENC-capable GPU ([support matrix](https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new)) |
| | AMD: VCE 1.0 or newer ([supported hardware](https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support)) |
| | Intel: Skylake or newer with QuickSync |
| | Qualcomm: Media Foundation encoding |
| CPU | AMD Ryzen 3 / Intel Core i3 or better |
| RAM | 4 GB or more |
| Network | 5 GHz 802.11ac or wired Ethernet on both host and client |

A 4K or HDR stream benefits from a modern mid-range GPU (GTX 1080 / VCE 3.1 / HD Graphics 730 or better), a Ryzen 5 or
Core i5, and wired Ethernet on both ends. These recommendations are a work in progress — do not buy hardware based on
them alone.

> [!NOTE]
> Virtual gamepads require the ViGEmBus driver. The installer offers to download and install it; without the driver,
> keyboard, mouse, touch, and pen input still work.

## Install

1. Download the latest installer or portable archive from the
   [releases page](https://github.com/paulwongkwan/Sunshine/releases/latest).
2. Run the installer, or extract the portable archive to a folder of your choice.
3. Start Solstice. The installed build runs as a service so it keeps streaming after you sign out of Windows.
4. Open <https://localhost:47990> and create your Web UI credentials on first launch.

## Pair a client

1. In Moonlight, add a host and enter the address of the machine running Solstice.
2. Moonlight shows a four-digit PIN.
3. Enter that PIN in the Solstice Web UI, or from the Solstice tray icon, and accept the pairing request.

## Configuration

Solstice keeps its state in a `config` directory beside the executable:

| File | Purpose |
|:-----|:--------|
| `solstice.conf` | Host settings, including encoder, resolution, and audio configuration |
| `apps.json` | Applications and desktop entries shown in Moonlight |
| `solstice_state.json` | Persistent runtime state such as the unique host ID |
| `solstice.log` | Log output, also viewable in the Web UI |
| `credentials/` | TLS key and certificate used by the Web UI |

The Web UI username and password hash are stored in `solstice.conf`.

The installed build uses `%ProgramFiles%\Solstice\config`. On first start Solstice copies any configuration it finds in
`%ProgramFiles%\Sunshine\config`, so an existing Sunshine installation can be replaced without redoing the setup.

See [configuration.md](docs/configuration.md) for every option, and [getting_started.md](docs/getting_started.md) for a
guided setup.

## Web UI sign-in

- The first visit redirects to a welcome page where you set the username and password.
- Later visits show a login screen. Select **Remember me** to stay signed in on that browser for 30 days; otherwise the
  session ends when the browser closes.
- Changing the password invalidates every remembered session, and **Log out** clears the cookie on the current browser.
- Scripted clients can keep using HTTP basic authentication against the same credentials; see [api.md](docs/api.md).

## Build from source

Solstice builds on Windows with MSYS2 UCRT64 (or CLANGARM64 on ARM64); cross-compilation is not supported. The full
prerequisites, including Boost, Qt, and Node.js, are listed in [building.md](docs/building.md).

```bash
cmake -B cmake-build-release -G Ninja -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build cmake-build-release
```

The resulting binaries are `cmake-build-release/solstice.exe` and `cmake-build-release/tools/solsticesvc.exe`. The
Windows installer and portable archive are produced by CPack:

```bash
cpack --config cmake-build-release/CPackConfig.cmake
```

### Tests

The test suite uses GoogleTest and is built by default. Run it with:

```bash
cmake -B cmake-build-release -G Ninja -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build cmake-build-release
cmake-build-release/tests/test_sunshine
```

Pass `-DBUILD_TESTS=OFF` to skip building the tests, and `-DBUILD_DOCS=OFF` to skip the documentation target.

The tests cover configuration parsing, legacy configuration migration, and the web assets; see
[contributing.md](docs/contributing.md) for the layout and conventions.

## Documentation

- [Getting started](docs/getting_started.md)
- [Configuration reference](docs/configuration.md)
- [API reference](docs/api.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Performance tuning](docs/performance_tuning.md)
- [Building](docs/building.md)
- [Contributing](docs/contributing.md)

## Contributing

Bug reports and pull requests are welcome. Please open an issue with the observed behavior, the expected behavior, and
the relevant log lines before sending a change, and read [contributing.md](docs/contributing.md) for the code style,
localization, and testing rules.

## Credits

Solstice is built on [Moonlight](https://moonlight-stream.org) for the client side, [ViGEmBus](https://github.com/nefarius/ViGEmBus)
for virtual gamepads, and [FFmpeg](https://ffmpeg.org) for media handling. Thank you to everyone who maintains those
projects.

## License

Solstice is distributed under the terms in [LICENSE](LICENSE). Third-party components and their licenses are listed in
[NOTICE](NOTICE).
