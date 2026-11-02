# UecpSource

UecpSource captures audio from a sound card and encodes it as MP2 or AAC. It
embeds RDS/UECP data received over TCP in each encoded frame and streams the
result to an Icecast server. It comes as a desktop GUI and as a headless
command-line program that uses the same settings file.

## How it works

```
sound card ──► encoder (MP2 or AAC) ──► Icecast SOURCE connection
                  ▲
UECP over TCP ────┘  embedded a few bytes per audio frame
```

- **Audio input:** any capture device, 16-bit PCM, 44.1 or 48 kHz, mono or
  stereo.
- **UECP input:** a TCP server (default port 9000) accepts one client at a
  time. It extracts complete `0xFE … 0xFF` frames and queues them. The frame
  contents aren't parsed.
- **MP2** ([libtoolame-dab](libtoolame-dab/)): up to 8 UECP bytes per frame,
  stored in the frame's ancillary data as
  `[zero padding][UECP bytes, reversed][N][0xFD]`. A decoder reads it
  backwards from the end of the frame. You can choose joint or plain stereo
  and the psychoacoustic model (0–3, default 2).
- **AAC** ([fdk-aac](https://github.com/mstorsjo/fdk-aac)): AAC-LC, HE-AAC or
  HE-AACv2. Up to 4 UECP bytes per frame go in a DSE (data_stream_element).
  The transport is LATM (LOAS-framed, the default) or ADTS.
- **Output:** Icecast `SOURCE` with `Content-Type` set to `audio/mpeg`,
  `audio/aac` (ADTS) or `audio/MP4A-LATM` (LATM). If the connection drops,
  it reconnects automatically with backoff.

UECP throughput depends on the frame rate. At 48 kHz that's about 330 B/s for
MP2 and about 190 B/s for AAC-LC. HE-AAC frames are twice as long, so you get
about half the AAC-LC rate.

## Building

You need CMake 3.20 or later, a C++17 compiler and git. The first configure
downloads fdk-aac, GLFW, Dear ImGui and miniaudio, so it needs internet access.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This builds two programs:

| Program | What it is |
|---|---|
| `uecp_source` | The GUI |
| `uecp_source_cli` | The headless command-line version |

- **Windows:** Visual Studio 2019 or later. The executables are statically
  linked against the MSVC runtime, so they don't need the VC++
  Redistributable.
- **Linux:** the GUI needs the X11 and OpenGL development packages for GLFW.
  On Debian or Ubuntu, for example:
  `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`.
- **Headless only:** configure with `-DUECP_BUILD_GUI=OFF`. This builds only
  `uecp_source_cli` and needs no X11 or OpenGL packages.
- **Offline builds:** point CMake at a local copy of a dependency with
  `-DFETCHCONTENT_SOURCE_DIR_FDKAAC=<path>` (and `..._GLFW`, `..._IMGUI` or
  `..._MINIAUDIO` for the others).

## Running

### GUI

Run `uecp_source`, then choose the capture device, format and Icecast
settings and press **Start**. Settings are saved to `source_config.ini` next
to the executable. Use `--config <path>` to run several copies with separate
settings.

### Headless

```
uecp_source_cli [--config <path>] [--list-devices] [--help]
```

The CLI reads the same settings file and starts streaming straight away. It
logs a timestamped status line every 10 s, and immediately whenever the state
changes. It stops cleanly on Ctrl+C or SIGTERM, and never writes to the
settings file.

The easiest way to set it up is to configure in the GUI, then copy the `.ini`
file to the machine that will run the CLI. Use `--list-devices` to find the
`input_device_name` value for each capture device there. If
`input_device_name` is empty, the system default device is used. If it names
a device that isn't present, the CLI exits with an error instead of streaming
from a different device.

Exit codes: `0` after a clean stop, `1` if startup fails, `2` for a bad
command-line argument.

### Settings file

The settings file is a plain `key=value` file. Any key you leave out uses the
default shown.

| Key | Values | Default |
|---|---|---|
| `input_device_name` | as printed by `--list-devices`; empty = system default | *(empty)* |
| `format_index` | `0` MP2, `1` AAC | `1` |
| `sample_rate_index` | `0` 44.1 kHz, `1` 48 kHz | `1` |
| `channels_index` | `0` mono, `1` stereo | `1` |
| `bitrate_kbps` | 32–384. For MP2, must be a standard Layer II rate (e.g. 128, 192, 256) | `192` |
| `mp2_joint_stereo` | `1` joint stereo, `0` plain stereo | `1` |
| `mp2_psy_model` | `0`–`3` | `2` |
| `aac_aot_index` | `0` AAC-LC, `1` HE-AAC, `2` HE-AACv2 | `0` |
| `aac_transport_index` | `0` ADTS, `1` LATM (LOAS) | `1` |
| `icecast_host` | hostname or IP address (**required**) | *(empty)* |
| `icecast_port` | TCP port | `8000` |
| `icecast_mount` | mount point | `/stream.aac` |
| `icecast_user` | source username | `source` |
| `icecast_password` | source password | *(empty)* |
| `icecast_name` | sent as `ice-name` | `UecpSource` |
| `icecast_buffer_kb` | groups output into writes of this many KB; `0` sends each frame immediately | `16` |
| `uecp_port` | TCP port for UECP input | `9000` |

The settings file holds the Icecast password in plain text, so keep it out of
version control. `.gitignore` already excludes `*.ini` files.

## Third-party components

| Component | License | How it's included |
|---|---|---|
| [fdk-aac](https://github.com/mstorsjo/fdk-aac) v2.0.3 | Software License for The Fraunhofer FDK AAC Codec Library for Android | Fetched by CMake, unmodified |
| [libtoolame-dab](https://github.com/Opendigitalradio/ODR-AudioEnc) | LGPL 2.1 or later | Copied into [`libtoolame-dab/`](libtoolame-dab/) with changes, listed in its [`MODIFICATIONS.md`](libtoolame-dab/MODIFICATIONS.md) |
| [GLFW](https://www.glfw.org/) 3.4 | zlib | Fetched by CMake (GUI only) |
| [Dear ImGui](https://github.com/ocornut/imgui) v1.91.5 | MIT | Fetched by CMake (GUI only) |
| [miniaudio](https://miniaud.io/) 0.11.21 | Public domain or MIT-0 | Fetched by CMake |

**Patents:** The Fraunhofer FDK AAC license explicitly grants no patent
rights. AAC and HE-AAC are covered by patents in some countries. If you
distribute builds that include the AAC encoder, check whether you need a
patent license where you distribute them.

## License

UecpSource is free software. You can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License or (at your option) any later
version. See [`LICENSE`](LICENSE).

The GPL's terms aren't compatible with the Fraunhofer FDK AAC license. So
that UecpSource can be distributed linked with fdk-aac, it comes with an
additional permission under GPLv3 section 7, given in
[`LICENSE-EXCEPTION`](LICENSE-EXCEPTION). This permission applies only to
UecpSource's own code. libtoolame-dab remains under the LGPL, and fdk-aac
under its own license.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
