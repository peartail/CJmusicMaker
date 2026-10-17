# MusicBitMaker

## Project goal

Build a private Windows x64 desktop application for playing, recording, arranging, mixing, and rendering music.

## Architecture decision

- Production target: C++20, JUCE native UI, Tracktion Engine, CMake, and MSVC.
- Canonical generator: `Visual Studio 17 2022`, architecture `x64`.
- Pin Tracktion Engine to `v3.2.0` and use the JUCE submodule commit that tag references (`19edd538429c93d277bf95b55aaa7e3eb545f951`). Do not track `develop` or independently upgrade JUCE.
- Use Tracktion's `Edit` and related model as the source of truth for tracks, clips, notes, transport, plugins, and automation. Do not duplicate authoritative music state in UI components.
- `Program.cs` and `MusicBitMaker.csproj` are a temporary C# WinForms input prototype, not the production architecture. Preserve them until the native replacement covers the same behavior.

## Working rules

- Treat Windows as the primary platform. Verify Windows paths, packaging, audio devices, and keyboard input behavior.
- Keep the first native milestone small: app window, audio device, playable keyboard, Tracktion Edit, and transport.
- Prefer platform APIs and existing project dependencies over new dependencies.
- Never perform file I/O, dynamic allocation, UI calls, logging, or long/blocking locks in the real-time audio callback.
- Keep UI, project model, and audio-device responsibilities separated. Use Tracktion/JUCE-supported state and messaging mechanisms between threads.
- Only add bundled audio with a license that permits the intended use. Record its source, author, license, and attribution requirements.
- Never commit API keys, credentials, user recordings, generated builds, or local machine settings.
- Current licensing assumption is private, local, non-distributed use under JUCE AGPLv3 and Tracktion GPLv3. Re-check both licenses before any binary/source distribution, outside testing, network service, revenue, or closed-source release.

## Change workflow

- `VERSION` is the single source of the app version. Start at `0.1001` and increment by `0.0001` for every user-testable build (`0.1002`, `0.1003`, ...). The UI and window title must show it with a `v` prefix.
- Inspect the relevant code and callers before editing.
- Make the smallest complete change that solves the request.
- Add or update the smallest useful automated check for non-trivial behavior.
- Run the relevant build and tests before handing work back; if commands do not exist yet, say so clearly.
- Update `README.md` and this file when the pinned dependencies or canonical build, test, and packaging commands change.

## Commands

Canonical native commands:

- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- Debug build: `cmake --build build --config Debug --target MusicBitMaker`
- Release build: `cmake --build build --config Release --target MusicBitMaker`
- Run: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe`
- Self-test: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --self-test`
- Keyboard octave UI/shortcut test: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --self-test-keyboard-octave`
- Instrument diagnostic log test: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --self-test-instrument-log`
- Real-time audio tap test: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --self-test-audio-tap`
- Synth comparison WAVs: `.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --render-synth-demos` (writes ignored `build/synth-demos/`; offline diagnostic, not the full Edit rendering test).
- Compare/concatenate demos: `python tools/compare-synth-demos.py` (requires NumPy).

Temporary C# prototype:

- Build: `dotnet build`
- Run: `dotnet run`
- Self-test: `dotnet run -- --self-test`
- Windows EXE: `dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -o dist`
