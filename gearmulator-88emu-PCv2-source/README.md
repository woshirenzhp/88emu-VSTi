# Gearmulator — 88emuPlayer VST3

Low-level emulation of classic synthesizers and sound modules, based on
[Gearmulator](https://github.com/dsp56300/gearmulator) by The Usual Suspects.
The emulators reproduce the original hardware and run its firmware (ROM).

This source snapshot extends Gearmulator 2.2.28 with an **88emuPlayer VST3
instrument for Windows x64**, four MIDI input buses, improved Program Change
compatibility, and event-by-event MIDI diagnostics. This is a modified project
snapshot; upstream releases do not describe this build. The original README is
preserved in [doc/README.upstream.md](doc/README.upstream.md).

## Project overview

| Emulator | Devices supported by the upstream project |
| --- | --- |
| Osirus | Access Virus A, B, C |
| OsTIrus | Access Virus TI, TI2, Snow |
| Vavra | Waldorf microQ |
| Xenia | Waldorf Microwave II / XT |
| Nodal Red 2x | Clavia Nord Lead / Rack 2x |
| JE-8086 | Roland JP-8000 |
| 88emu | Roland MT-32, CM-32L, CM-32P, CM-64, MV-30, and Sound Canvas models including SC-55, SC-88, SC-88Pro, SC-8820, SC-8850 and variants |

The wider codebase contains VST3, FST/VST2-compatible, AU, CLAP and LV2 targets,
with x64 and ARM64 support depending on the target and platform. These modified
88emuPlayer builds were tested as **Windows x64 VST3**, using Visual Studio 2026
Build Tools. Other formats, operating systems and Windows 7 compatibility have
not been verified for this snapshot. A 32-bit host needs a suitable 64-bit plugin bridge.

**Firmware ROMs are required and are not included.** The source archive also
excludes personal MIDI files, runtime logs, compiled plugins and proprietary SDKs.
The main project uses GNU GPL v3; see [LICENSE.md](LICENSE.md). Third-party source
retains its own license notices.

## 88emuPlayer features

- VST3 instrument with stereo audio output and four independent event input buses:
  **MIDI Input A, B, C and D**, each with 16 channels.
- Port-aware notes, CC, pitch bend, pressure, Program Change and SysEx input,
  retaining offsets within the audio block. Bank Select uses CC0 and CC32.
- 64 standard VST3 channel units and program lists, each exposing 128 programs
  through a discrete `kIsProgramChange` parameter. Existing MIDI parameter IDs are retained.
- Compatibility with hosts sending PC inside a SysEx DataEvent as `Cn pp` or
  `Cn pp 00`. Identical PC events received through parameter and DataEvent paths
  at the same port and sample offset are delivered once.
- MIDI parameter decoding in the audio component, independent of a direct pointer
  to the edit controller.

The selected emulated model determines how many hardware groups are available.
Use SC-8850 for all four groups. Models with fewer groups do not gain additional
hardware parts simply because four plugin buses are exposed.

### Host routing requirements

These are plugin inputs inside the host, not four new Windows MIDI devices.
Channel 2 on input A is different from channel 1 on input B.

The host or MIDI player must translate MIDI-file port metadata into VST3 input
buses: file port 0 to A (`busIndex=0`), file port 1 to B (`busIndex=1`), and so on.
Ordinary MIDI message bytes contain no port number. If a host or bridge merges
both streams into A, the plugin cannot reconstruct their original routing, and
program/controller settings on matching channels can overwrite each other.
Independent buses are verified by headless tests; commercial DAW and bridge
routing support must be checked separately.

## Installation and use

1. Build the plugin below. Copy the **entire** `88emuPlayer.vst3` folder into a
   host-scanned VST3 location, such as `C:\Program Files\Common Files\VST3`.
   Do not copy only the DLL inside it.
2. Put your ROM files under Windows Documents at
   `The Usual Suspects\88emuPlayer\roms`. Model-specific subdirectories are supported.
3. Rescan plugins, create an **88emuPlayer** instrument instance, select a model
   with available ROMs, and allow its firmware to boot.
4. Route tracks to the required input bus and channel. The host supplies the
   audio device, sample rate and block size.

After updating an older build, restart the host, rescan and create a new instance.
Hosts may cache old parameter flags and program lists. The plugin identity is preserved.

### Saved-state limitations

Plugin state saves the model, output gain, limiter, resampling mode and analog
output mode. It does **not** serialize complete hardware RAM, active voices, all
controller state or every front-panel edit. Reopening a project boots the selected
device again. Keep Bank Select, PC, CC and SysEx initialization in your MIDI
arrangement. ROMs must remain available on the machine.

## Building on Windows

Requirements: an x64 Windows environment, a C++17 compiler, Visual Studio 2022 or
2026 C++ Build Tools, the Windows SDK, and the Visual Studio CMake component.
The verified build used Visual Studio 2026 with toolset v145.

The prepared source archive includes available dependency source, including the
nested dependencies needed by the tested 88emu target. Git history is omitted.
Optional targets may require additional dependencies. For a separate Git checkout,
initialize submodules recursively and use the revisions recorded in
[doc/88emu_vst3_dependencies.json](doc/88emu_vst3_dependencies.json); the default
JUCE and RmlUi branches are not substitutes for the tested revisions.

Run from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_88vst3.ps1 -BuildTests
```

The script selects 88emu/VST3 and disables the other instruments and installer.
`-BuildTests` builds the ABI test executable; it does not execute it.

Output bundle:

```text
bin/plugins/Release/VST3/88emuPlayer.vst3/
  Contents/x86_64-win/88emuPlayer.vst3
```

### Diagnostic build

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_88vst3.ps1 -BuildTests -MidiTraceFile 88emu-midi.log
```

This is an optimized build with diagnostics enabled and no MSVC Debug CRT
dependency. Copy the bundle to a separate folder before rebuilding the normal
version: both builds share the output path and plugin identity.

The log is written **beside the outer `.vst3` bundle**, or beside the DLL for a
flat layout. It follows the plugin location, not the host working directory.
Use a writable folder. Set `EMU88_MIDI_TRACE_PATH` before starting the host and
bridge to override the log path.

Diagnostics record every parameter point, offset and normalized value; decoded
port/channel/controller; incoming event bus; raw DataEvent bytes; PC duplicate
handling; and every MIDI event delivered to the engine queue. `DELIVER` proves
queue submission, not that the firmware has applied a setting. Logging is
synchronous and can affect real-time audio. Return to the normal build after
collecting a reproduction. Detailed log tags and Chinese notes are available in
[doc/88emu_pc_v2_zh.md](doc/88emu_pc_v2_zh.md).

## Tests

The VST3 ABI host checks four 16-channel buses, 8,384 distinct MIDI mappings,
standard program metadata and normalization, per-port audio/CC7 isolation,
state round-trip, editor metadata and invalid mapping requests. Fixture modes
check PC/SysEx through mapped, standard and raw paths, duplicate handling,
multiple parameter points, and CC/pitch bend after disconnecting the direct
edit-controller connection.

Supply your own SC-8850 ROMs and configuration. For an isolated test root, set
`TUS_DATA_FOLDER` before launching the test, with this layout:

```text
<test-root>/The Usual Suspects/88emuPlayer/
  roms/sc8850/...
  config/88emuPlayer.xml
```

Select `deviceModel=3` (SC-8850) in the configuration. The test executable is:

```text
temp/cmake_88vst3/source/ronaldo/88emu/88emuplayer/Release/88emuVst3_test.exe
```

Pass the absolute DLL path inside the VST3 bundle as its first argument. With no
fixture argument it runs audio/isolation checks. See
[Vst3Ports_test.cpp](source/ronaldo/88emu/88emuplayer/tests/Vst3Ports_test.cpp)
for fixture modes.

[scripts/test_vsthost_headless.py](scripts/test_vsthost_headless.py) optionally
extracts three original MIDI functions from the supplied legacy `vsthost-main`
source and tests them as x86 code, then checks plugin reception through an
explicit VST3 test adapter. That legacy source contains VST2 code, **not the VST3
implementation of the newer VSTHost binary**. These tests are not end-to-end tests
of that newer host. Python 3 and x86 MSVC tools are required; run the helper with
`--help` for its arguments. Test songs and ROMs are excluded from the archive.

## Source layout

| Path | Purpose |
| --- | --- |
| `source/ronaldo/88emu/88lib` | Device integration and emulation |
| `source/ronaldo/88emu/88emuplayer` | Processor, player, UI and tests |
| `HostMidiPorts.h` in the player directory | Four-port MIDI buffer interface |
| `vst3Ports.cmake`, `vst3Programs.cmake` | Product-local JUCE wrapper adaptation |
| `Vst3MidiProgramInfo.h` | Stable parameter decoding and standard program metadata |
| `MidiTrace.h` | Optional diagnostic writer |
| `source/3rdparty`, `source/cpu` | Dependency source and processor emulators |
| `scripts`, `doc` | Build/test helpers and documentation |
| `vsthost-main` | Legacy host source for the optional source-path test |

The adapted wrapper is generated in the build directory. It does not modify the
shared JUCE wrapper or apply four-port behavior to the other instruments.

## General CMake options

The original project uses CMake and C++17. Common defaults in this snapshot are:

| Variable | Description | Default |
| --- | --- | --- |
| `gearmulator_BUILD_JUCEPLUGIN` | Build JUCE-based plugins | ON |
| `gearmulator_BUILD_JUCEPLUGIN_VST3` | Build VST3 plugins | ON |
| `gearmulator_BUILD_JUCEPLUGIN_CLAP` | Build CLAP plugins | ON |
| `gearmulator_BUILD_JUCEPLUGIN_LV2` | Build LV2 plugins | OFF |
| `gearmulator_BUILD_FX_PLUGIN` | Build additional FX variants | OFF |
| `gearmulator_SYNTH_OSIRUS` | Build Osirus | ON |
| `gearmulator_SYNTH_OSTIRUS` | Build OsTIrus | ON |
| `gearmulator_SYNTH_VAVRA` | Build Vavra | ON |
| `gearmulator_SYNTH_XENIA` | Build Xenia | ON |
| `gearmulator_SYNTH_NODALRED2X` | Build Nodal Red 2x | ON |
| `gearmulator_SYNTH_JE8086` | Build JE-8086 | ON |
| `gearmulator_SYNTH_88EMU` | Build 88emu | ON |
| `gearmulator_COMPONENT_DSPBRIDGE` | Build DSPBridge and device plugins | ON |
| `EMU88_MIDI_TRACE_FILE` | Diagnostic path; empty disables logging | Empty |

The ASIO SDK is not bundled. On Windows, configuration tries to download it into
the build directory if no SDK is available; a failed download leaves standalone
ASIO disabled. Set `gearmulator_ASIO_SDK_PATH` to use a separately supplied SDK.
ASIO device selection concerns the standalone player; a VST3 instrument uses its
host's audio driver. Review any separately downloaded SDK's license terms.

## Creating a clean source archive

```powershell
python scripts/package_88emu_source.py
```

The helper creates a ZIP under `dist`, includes dependency source and license
notices, and writes a SHA-256 file manifest. It excludes Git internals, local
outputs, ROM backups, personal MIDI files, logs and compiled binaries. The
generated `SOURCE_PACKAGE.json` records the package scope. Its manifest records
file contents; it does not imply a clean upstream Git checkout.

## Upstream community

- Website and audio/video examples: [dsp56300.com](https://dsp56300.com/)
- Discord: [The Usual Suspects community](https://discord.gg/WJ9cxySnsM)
- Upstream source: [dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
