# NanoFrog — Bitimbral Virtual-Analog Synthesizer (VST3 + Standalone)

> **Disclaimer:** this project is vibe-coded for personal use. It is provided
> as-is, without warranty of any kind. The author is not responsible for
> anything — Use at your own risk.

NanoFrog is a bitimbral virtual-analog synthesizer inspired by the
early-2000s VA classics: two independent timbres, each with 2 oscillators
(saw/square/triangle/sine + 128 procedural digital waves + user-imported
single-cycle waves) plus noise through a mixer with osc2→osc1 FM, a resonant
multi-mode filter (LP24/LP12/BP12/HP12), amp with drive, 2 ADSR envelopes,
2 tempo-syncable LFOs, and a 4-slot mod matrix, feeding a shared chain of
modulation FX (chorus/flanger/phaser), tempo-syncable delay, 2-band EQ, and
an output limiter, with a tempo-synced arpeggiator on top. A single header
knob crossfades Timbre 1 ↔ Timbre 2.

It ships 401 original factory presets as editable JSON data (never compiled
in) with category/character tags, a preset browser with search + tag
filters, and user preset save/overwrite/delete, plus a header MUTATE button that slightly
re-rolls the current sound (oscillators included but never pitch; envelopes
frozen for bass/keys/leads/percussive voices; bypassed modules never wake up).

All DSP, wavetables, UI, and presets are original work. This project is not
affiliated with, endorsed by, or connected to any hardware or software
manufacturer, and uses no third-party trademarks, branding, or patch names.

## License

GPL version 3 — see [LICENSE](LICENSE). This also satisfies the JUCE
framework's licensing terms for this project.

The embedded Lato typeface is SIL Open Font License 1.1; see
[Assets/OFL-NOTICE.txt](Assets/OFL-NOTICE.txt).

## Building

Requirements: CMake 3.22+, a C++17 compiler, Ninja (or Make), Git (JUCE is
fetched automatically), and JUCE's Linux dependencies (ALSA, X11, freetype,
etc.) — see the JUCE docs for the full list.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs:

- VST3: `build/NanoFrog_artefacts/Release/VST3/NanoFrog.vst3` (factory bank
  travels inside the bundle under `Contents/Resources/`)
- Standalone: `build/NanoFrog_artefacts/Release/Standalone/NanoFrog`
- Headless self-test: `build/NanoFrogHeadlessTest_artefacts/Release/NanoFrogHeadlessTest`
  (runs the full DSP/UI regression suite; `HEADLESS TEST PASSED` = all good)

Install the VST3 by copying `NanoFrog.vst3` to your plugin folder:

| OS      | VST3 folder                          | Notes                                        |
|---------|--------------------------------------|----------------------------------------------|
| Linux   | `~/.vst3/`                           | Rescan plugins in your DAW afterwards        |
| macOS   | `~/Library/Audio/Plug-Ins/VST3/`     | Unsigned build: right-click → Open once      |
| Windows | `C:\Program Files\Common Files\VST3\`| Rescan plugins in your DAW afterwards        |

User presets live in `Documents/NanoFrog/User Presets/` (created on first
save) as single `.nbpreset` JSON files. The factory bank is data, not code:
edit `FactoryBank/NanoFrogFactory.json` by hand, or regenerate it with
`python3 Tools/gen_bank.py` (deterministic — same seed, same bank).

### OS-specific notes

> Only the Linux build has actually been compiled and tested here. The macOS
> and Windows steps below follow the standard JUCE/CMake flow and *should*
> work, but if you hit an OS-specific snag, please file an issue with the
> failing command and its full output.

#### Linux (verified)

1. Install dependencies (Debian/Ubuntu shown; Fedora/Arch: equivalent
   `-devel` packages):
   ```sh
   sudo apt install build-essential cmake ninja-build git pkg-config \
     libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
     libxinerama-dev libxrandr-dev libfreetype6-dev libfontconfig1-dev \
     libcurl4-openssl-dev libwebkit2gtk-4.1-dev
   ```
2. Configure + build (JUCE is fetched automatically — needs network access):
   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
3. Run the self-test (must print `HEADLESS TEST PASSED`):
   ```sh
   ./build/NanoFrogHeadlessTest_artefacts/Release/NanoFrogHeadlessTest
   ```
4. Install: copy `build/NanoFrog_artefacts/Release/VST3/NanoFrog.vst3` to
   `~/.vst3/` (the factory bank travels inside the bundle), then rescan
   plugins in your DAW. Use a fresh instance for new presets — old project
   states restore their saved parameter values by design.

#### macOS (not yet built here)

1. Install Xcode command-line tools, CMake, Ninja and Git:
   ```sh
   xcode-select --install
   brew install cmake ninja git
   ```
2. Same configure + build as Linux. For a universal (Intel + Apple Silicon)
   binary, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the configure step.
3. Run the headless self-test — expect `HEADLESS TEST PASSED`.
4. Install: copy `NanoFrog.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. These are
   unsigned personal builds: if macOS refuses to load them, ad-hoc sign with
   `codesign --force --deep -s - <path>` and/or strip the quarantine flag
   with `xattr -dr com.apple.quarantine <path>`. Codesigning/notarisation for
   distribution is out of scope for this project.

#### Windows (not yet built here)

1. Install Visual Studio 2022 with the **Desktop development with C++**
   workload (MSVC v143 or newer), plus CMake, Ninja and Git
   (`winget install Kitware.CMake Ninja-build.Ninja Git.Git` works).
2. Open an **x64 Native Tools Command Prompt** (so MSVC is on `PATH`), then
   the same configure + build commands as Linux. Alternatively use the
   Visual Studio generator instead of Ninja:
   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
3. Run `build\NanoFrogHeadlessTest_artefacts\Release\NanoFrogHeadlessTest.exe`
   — expect `HEADLESS TEST PASSED`.
4. Install: copy `NanoFrog.vst3` to `C:\Program Files\Common Files\VST3\`,
   then rescan plugins in your DAW. No ASIO SDK or extra setup required.

## Repository layout

- `Source/` — plugin DSP, UI, preset backend
  (`DSP/` voices/filters/FX, `PluginProcessor.*`, `PluginEditor.*`,
  `PresetBank.*`, `PresetBrowser.*`, `NanoLook.h`)
- `FactoryBank/NanoFrogFactory.json` — the 401 factory presets (data:
  names, tags, raw parameter values; regenerate with `Tools/gen_bank.py`)
- `Assets/` — embedded Lato fonts + OFL notice
- `Tools/gen_bank.py` — deterministic factory-bank generator (reads the
  live parameter order from the sources, so the table can't drift)
- `HeadlessTest.cpp` — headless regression suite: layout geometry,
  control liveness, preset round-trips, DSP behaviour (audibility,
  arp release, digital routing, FM, limiter), user-preset save/load
- `CMakeLists.txt` — VST3 + Standalone + headless test (JUCE via FetchContent)
