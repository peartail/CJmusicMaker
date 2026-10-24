# CLI, Headless, and UI Screenshot Design

## 1. Purpose

MusicBitMaker should be controllable by Codex or another local agent without depending on mouse clicks. The first automation milestone provides three capabilities:

1. Inspect and edit a saved `.tracktionedit` project from a command.
2. Run the command without opening the normal application windows or audio device.
3. Render the MusicBitMaker UI to a PNG so an agent can inspect the visual result.

This is local developer automation. It is not a network API, plug-in protocol, or public scripting language.

## 2. Terms

- **CLI (Command-Line Interface):** A way to start the executable with text options such as `--project` and `list-tracks`.
- **Headless:** Running project logic without showing the normal application window. Headless does not mean that all JUCE GUI code is unavailable; the screenshot command may initialise JUCE graphics without displaying a window.
- **Structured result:** A JSON file containing a stable success or error result that an agent can parse.
- **UI snapshot:** A PNG rendered from the JUCE main content component. It is not a photograph of the entire Windows desktop.
- **Live capture:** Capturing the current state of an already running MusicBitMaker window.

## 3. Current Constraints

- `MusicBitMakerApplication::initialise` recognises fixed self-test and synth-demo switches only.
- `MainComponent` creates its own empty Tracktion `Edit`, keyboard window, settings storage, diagnostic log, and timer in one constructor.
- Project loading and editing operations are private UI methods.
- `anotherInstanceStarted` brings the existing window forward but ignores the forwarded command line.
- The Windows target is a JUCE GUI application, so standard output is not a reliable machine-readable result channel.

The implementation must continue to use Tracktion `Edit` as the authoritative project state. The CLI must not create a second project model.

## 4. Chosen Shape

Use the existing `MusicBitMaker.exe` for GUI and automation:

```text
MusicBitMaker.exe
    normal arguments       -> existing GUI application
    --headless ...         -> load Edit, execute one command, save if requested, exit
    --capture-current-ui   -> request a snapshot from the running GUI instance
```

Do not add an HTTP server, embedded scripting language, or permanent background agent in the first milestone.

### 4.1 Command flow

```text
arguments
   |
   v
parse + validate
   |
   v
create Tracktion Engine without normal UI/audio startup
   |
   v
load .tracktionedit -> operate directly on Tracktion Edit -> optional save
   |
   v
write result JSON + set process exit code
```

The parser and command executor should be ordinary functions, not a class hierarchy:

```cpp
struct AutomationRequest;
struct AutomationResult;

AutomationResult runAutomationCommand (te::Engine&, const AutomationRequest&);
```

GUI methods should reuse the same small Tracktion-editing helpers when a command overlaps existing behaviour. There must not be separate authoritative track or note collections.

## 5. Command Contract

The initial syntax is:

```powershell
MusicBitMaker.exe --headless <command> [options]
```

Every headless command supports:

```text
--project <absolute-or-relative.tracktionedit>
--result-json <path.json>
```

`--result-json` is required in the first implementation because the Windows GUI executable does not provide a reliable standard-output channel.

Mutating commands require exactly one save choice:

```text
--output-project <new.tracktionedit>   Save to a different file
--in-place                             Explicitly overwrite the input project
```

This prevents an agent typo from silently overwriting a project. Read-only commands reject save options.

### 5.1 First command set

| Command | Behaviour |
|---|---|
| `version` | Return application and automation schema versions. Does not require a project. |
| `project-info` | Return project path, BPM, length, track count, loop state, and loop range. |
| `list-tracks` | Return stable Tracktion item IDs, display indices, names, and instrument categories. |
| `list-notes` | Return notes for one track: MIDI pitch, readable name, start, duration, velocity, and a command-local note index. |
| `add-track` | Add `synth`, `kick`, `snare`, `closed-hat`, or `open-hat`. |
| `rename-track` | Rename a track selected by stable item ID. |
| `delete-track` | Delete a track selected by stable item ID. |
| `add-note` | Add a validated note to a synth track. |
| `delete-note` | Delete a note using the track ID and note index returned by `list-notes`. |
| `set-tempo` | Set BPM within the same 40-240 range as the UI. |
| `set-loop` | Set enabled state, start, and end. |
| `screenshot` | Render the project in the main MusicBitMaker UI to a PNG. |

Track item IDs are preferred over display indices because adding or deleting another track changes indices. A note index is initially acceptable because Tracktion MIDI notes do not currently have an app-level persistent ID; the command must reject a stale or out-of-range index.

### 5.2 Examples

```powershell
MusicBitMaker.exe --headless project-info `
  --project "E:\Songs\demo.tracktionedit" `
  --result-json "E:\Songs\demo-info.json"

MusicBitMaker.exe --headless add-note `
  --project "E:\Songs\demo.tracktionedit" `
  --track-id "123456" --pitch C4 --start 2.0 --duration 0.5 --velocity 100 `
  --in-place --result-json "E:\Songs\add-note-result.json"

MusicBitMaker.exe --headless screenshot `
  --project "E:\Songs\demo.tracktionedit" `
  --output "E:\Songs\demo-ui.png" --width 1280 --height 800 `
  --result-json "E:\Songs\demo-ui-result.json"
```

## 6. JSON and Exit Codes

The JSON envelope is the same for every command:

```json
{
  "schemaVersion": 1,
  "ok": true,
  "command": "list-tracks",
  "appVersion": "0.1032",
  "data": {},
  "warnings": []
}
```

On failure:

```json
{
  "schemaVersion": 1,
  "ok": false,
  "command": "add-note",
  "appVersion": "0.1032",
  "error": {
    "code": "track_not_found",
    "message": "No track has item ID 123456."
  }
}
```

Process exit codes:

| Code | Meaning |
|---:|---|
| 0 | Success |
| 2 | Invalid command or argument |
| 3 | Input/output file error |
| 4 | Project could not be loaded or saved |
| 5 | Requested track or note was not found |
| 6 | Screenshot rendering failed |
| 10 | Unexpected internal failure |

JSON should be written with JUCE `JSON`/`var` support already present in the dependency tree. No new JSON library is needed. If `--result-json` cannot be written, the non-zero exit code is still authoritative.

## 7. Headless Rules

- Do not create `MainWindow`, `KeyboardPadWindow`, shortcut editor, native file chooser, or audio settings window.
- Do not start playback, recording, timers, or an audio device for project-data commands.
- Do not read or write application-global shortcut settings unless the command explicitly concerns them.
- Load the requested project explicitly; never fall back silently to an empty project.
- Validate the entire command before changing the `Edit`.
- Save only after the operation succeeds.
- Never modify a project from the real-time audio callback.

The first implementation should verify whether the current Tracktion `Engine` constructor opens an audio device in this build. If it does, introduce the smallest supported engine/device initialisation option that prevents it in headless data commands.

## 8. Screenshot Design

### 8.1 Deterministic headless snapshot

The first screenshot implementation renders the JUCE main content area:

1. Load the requested Tracktion `Edit`.
2. Construct `MainComponent` with automation options:
   - supplied project/Edit;
   - auxiliary keyboard and editor windows disabled;
   - audio playback disabled;
   - UI timer disabled;
   - fixed width and height.
3. Run layout once.
4. Render with `createComponentSnapshot` or `paintEntireComponent` into a JUCE `Image`.
5. Write PNG with `juce::PNGImageFormat`.
6. Destroy the UI and exit.

The image includes the app content—menus, controls, track list, rulers, notes, loop range, playhead, and status—but excludes the native Windows title bar, shadows, other applications, modal dialogs, and the detached keyboard window.

Defaults:

```text
width: 1280
height: 800
scale: 1.0
background: opaque
```

The accepted size range should be limited to prevent accidental huge allocations, for example 820x620 through 3840x2160.

### 8.2 Capture of the currently running app

After deterministic snapshots work, add:

```powershell
MusicBitMaker.exe --capture-current-ui "E:\Temp\current.png" `
  --result-json "E:\Temp\current-result.json"
```

When the GUI is already running, JUCE forwards the command line to `anotherInstanceStarted`. The existing instance validates the path and asks its `MainComponent` to render its current content state. This captures current selection, zoom, scroll position, loop handles, and playhead.

The first version does not add a permanent named-pipe server. The invoking second process may exit before the first process finishes writing, so automation should wait for the result file with a short timeout. If reliable request/response or continuous live control becomes necessary, add a named pipe then.

## 9. Minimal Code Separation

The current `src/Main.cpp` is large, but this feature does not justify a broad rewrite. Split only code that both GUI and headless paths need:

```text
src/Main.cpp                  GUI application and existing UI
src/AutomationCli.h
src/AutomationCli.cpp        parsing, validation, JSON result, Edit commands
```

Add lightweight construction options to `MainComponent` rather than creating a second screenshot-only UI. Project mutations should be small free functions taking `te::Edit&`; existing GUI methods can migrate to them only when needed by a CLI command.

## 10. Security and Data Safety

- Accept local filesystem paths only; do not add network listeners.
- Canonicalise input/output paths and reject an output equal to the input unless `--in-place` is present.
- Refuse to overwrite an unrelated existing output unless an explicit overwrite option is added later.
- Reject unknown options instead of ignoring them.
- Limit screenshot dimensions.
- Return errors without including arbitrary project contents or local settings.
- Do not execute commands stored inside a project file or JSON result file.

## 11. Delivery Phases

### Phase 1: Headless inspection foundation

- Implemented in v0.1032.
- Parser and validation.
- `version`, `project-info`, `list-tracks`, and `list-notes`.
- JSON result files and exit codes.
- No normal window, file chooser, timer, or audio-device initialisation.
- Self-test: project load, track/note listing, JSON schema, and closed audio device.

### Phase 2: Safe project mutation

- `add-track`, `rename-track`, `delete-track`, `add-note`, `delete-note`, `set-tempo`, and `set-loop`.
- Explicit `--output-project` or `--in-place` requirement.
- Reload the saved output in the self-test and verify Tracktion state.

### Phase 3: Deterministic UI screenshot

- Headless main-content PNG.
- Fixed size options and bounds checks.
- Pixel sanity test: correct dimensions, non-empty alpha, and non-uniform image content.

### Phase 4: Current running UI capture

- Implemented in v0.1034.
- Use the `--live capture-ui` Named Pipe request rather than relying on the one-way
  `anotherInstanceStarted` forwarding path.
- The request/response protocol, security guard, live commands, and delivery phases are
  specified in `docs/live-app-control-design.md`.
- Manual Windows check with selection, zoom, and playhead changed before capture.

### Deferred until required

- Continuous playback/recording control from an agent.
- Full desktop/native-title-bar capture.
- Batch command scripts or a custom scripting language.
- HTTP/REST service.
- Offline master audio rendering.

## 12. Acceptance Criteria

The milestone is complete when an external process can:

1. List tracks and notes from a real saved project using only files and an exit code.
2. Add a note, save safely, reload the result, and observe the same Tracktion state in the GUI.
3. Produce a PNG that visually matches the main UI state for the chosen project and dimensions.
4. Run the above without showing the normal application or opening an audio device for data-only commands.
5. Receive deterministic JSON errors for bad paths, bad track IDs, invalid note values, and save failures.

Each user-testable phase increments `VERSION` by `0.0001` and adds one focused self-test command to `AGENTS.md`.
