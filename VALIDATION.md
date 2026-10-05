# PianoRollCore 0.4.0 validation

## 0.4.0 — piano-roll view, 2026-10-05

Linux container, GCC 13.3, C++17, CMake Release. Shared JUCE targets built against JUCE 7.0.12 (the copy pinned in MIDI Recover's `vendor/`); Xvfb display.

| Check | Result |
|---|---|
| Core CMake build, six CTest groups | PASS: 6/6, unchanged from 0.3.1 (44 core, 80 experience, 54 playback, 181 command/music, 77 note actions, 40 automation) |
| Shared JUCE targets (`CommandUI`, new `View`) | PASS: compile and link |
| `PianoRollViewChecks` (new) | PASS: 29 checks with a minimal host through the real mouse handlers: layout and paint, snap/free move, one before-image and one commit per gesture, read-only and sub-threshold drags, resize, Shift copy, rejected follow ending the gesture, pencil creation, path erase, scoped marquee, velocity and controller strokes, navigation without edits, ruler seek/loop, busy host |
| `PianoRollCommandUIChecks` | PASS: 195 checks, unchanged |
| Behaviour parity with MIDI Recover 0.7.10 | Established in the product: the view was moved line by line, and Recover's `recover_gesture_replay` (103 scripted gestures) and 118 component/replay images match the pre-move baseline. See Recover `docs/VALIDATION.md` |
| Version / source snapshot | PASS: `--refresh-manifest`, `--verify .` |
| Windows DAW, MIDI Lines integration | NOT RUN: Lines adopts the view in a later task |

## Previous: 0.3.1

## Current maintenance validation — 2026-09-21

Native Linux, GCC 13.3, C++17. Core built with CMake Release; shared JUCE controls built with JUCE 7.0.12 and Debug assertions enabled.

| Check | Result |
|---|---|
| Core CMake build, six CTest groups | PASS: 6/6 groups, 476 assertions total (44 core, 80 experience, 54 playback, 181 command/music, 77 note actions, 40 automation) |
| Shared JUCE UI build | PASS: real shared components and standalone harness compile/link |
| Headless control / callback harness | PASS: 195 explicit checks; includes unsaved macro -> search -> execute -> save/discard -> close, and independent Insert/F1–F24 capture/assignment |
| Real window focus / OS keyboard delivery | BLOCKED: the environment cannot open the Xvfb listening sockets. Headless runs log JUCE Component.cpp:2666 focus assertions because controls are not on screen; focus is not claimed as passing |
| Version / source snapshot | PASS: header/CMake/VERSION/manifest agreement, stale VERSION rejection, regeneration, snapshot and ZIP round-trip integrity |
| Windows DAW and product adapter integration | NOT RUN: only the independent shared module is updated |
| Performance comparison / sanitizers | NOT RUN in this maintenance update |

Numeric regressions cover exponent and integer-valued decimal inputs, full-string rejection, int endpoints, process-locale independence, legacy stoi hosts, remembered settings, macro execution and configuration loading. Existing configuration formats and host callback signatures remain unchanged.

Reproduce the core checks with the README CMake/CTest commands. For shared controls, configure `juce/tests` with `-DJUCE_SOURCE_DIR=/path/to/JUCE-7.0.12`, build, then run its CTest target in a graphical Linux session. Headless callbacks do not substitute for an actual window or DAW test. The tests use synthetic MIDI/command data.

## Prior 0.3.0 checks (historical release record)

Each owner ran its own focused checks on Linux. Product integration owners separately build and exercise the Windows plugin targets. No performance comparison was performed for this release.

| Owner / target | Result |
|---|---|
| Core command/music owner: standalone GCC C++17 command and musical-helper harness | 157 assertions passed |
| Core command/music owner: later targeted shared Q/Alt+Q settings regression | 6 assertions passed; no duplicate full-suite run |
| Note-action owner: `NoteActionTests.cpp` | 77 assertions passed |
| Automation owner: `AutomationActionTests.cpp` | 40 assertions passed, including separate clipboard source, linked cut/delete and relative-controller preservation |
| Shared JUCE UI owner: controls, preferences and transform gestures | 52 assertions passed, including the real 616×351 Recover minimum-panel layout; see `juce/tests/VALIDATION.md` |

Command coverage includes bilingual search, exact modifier routing, configurable conflicts/unbinding/default restoration, persisted settings validation, busy/text-focus handling, remembered parameters, sequential macro preflight, single commit, rollback on failed begin/apply/commit and exceptions, nonreentrancy, and forbidden macro actions. Music-helper checks cover folded row mapping, explicit channel mapping, chord release, per-note velocity, disarm cancellation, and cursor movement.

Music-action coverage includes quantization endpoints/strength/Swing/divisions, timing restoration, mirror/reverse pivots, length/linked boundaries/time scaling, condition selection, splitting/repetition/strumming, stable source identity, read-only/Key Switch exclusions and invalid-input rejection. Automation checks cover affine/handle transforms, explicit source/controller scopes, original boundary restoration, empty lanes, overlapping moves/copies, clipboard snapshots and non-latched controller restrictions.

The assertion counts are not independent full-host scenarios. Full Lines/Recover document, key-handler, undo, saved-state, controller-source and GUI integration checks are shipped in their own release reports. FL Studio on Windows still requires real-host use.

## Prior 0.2.0 baseline (retained; not rerun solely for duplication)

Run on 2026-09-20 in Linux, native GCC 13.3 / C++17. The module has no JUCE dependency.

| Verification owner / target | Result |
|---|---|
| Core owner: CMake Release configure and all library/test/example targets build | Passed |
| Core owner: original `PianoRollCoreTests` | 44 checks passed |
| Core owner: `PianoRollExperienceTests` | 80 checks passed |
| Core owner: `PianoRollCoreExample` | One commit, one undo entry; first note tick 144 |
| Playback owner: `PianoRollPlaybackTests` | 54 checks passed |
| Playback owner: playback AddressSanitizer + UndefinedBehaviorSanitizer | 54 checks passed; leak sanitizer disabled due environment ptrace restrictions |
| Playback owner: `PianoRollPlaybackExample` | Paused at 0.01 seconds |

The 178 checks are assertions, not 178 different full-application scenarios. Each owner ran its own tests; plugin integration tests and Windows builds are performed separately by the two integration owners.

## Focused coverage

- Original core: beat/tick geometry, exact pitch boundaries, hit/box selection, editable-only bounds, snapping, copy anchor mapping, no-op/cancel/stale/reentrant edit commit.
- Navigation: keyboard middle zoom versus roll/lane pan; pointer anchoring; repeat drag callback stability; pitch range limits; Ctrl, Ctrl+Shift, Shift and trackpad wheel; zero wheel and invalid layout; original tick equivalence; follow pause/resume/manual interruption; octave labels.
- Interaction: complete transport/tool shortcuts, text-field ownership, consumed ruler releases, reverse loop drag and snapping, cancellation, All/edit scope separate from solo, modifier axis locks, relative velocity offset, interpolated lane sweep and velocity range.
- Controllers: no empty defaults, real CC0 presence, stable/sorted source catalog, empty strokes ignored, explicit new CC destination, ambiguous All-source request, invalid controller rejection, clear-on-document-replacement.
- Meter: implicit 4/4, mixed 4/4→3/8→2/4, partial bar convention, same-tick last event, repeated-signature stability, original PPQ equivalence, grid work limits, snap subdivisions and triplets preserving real beat/signature lines.
- Playback: unchanged old Lines reference waveform; 48/96 kHz position; pause/seek/loop/controller chase/solo; CC64 release; host restore silence and separate device stop/restart gate; 1,500 concurrent schedule replacements; audio-thread allocation/free counters zero. Playback owner also checked ASAN/UBSAN. Thread atomic types have compile-time lock-free requirements.

## Limits

These checks do not substitute for Windows/FL Studio real-host interaction, focus routing, file drag/drop or audio-device lifecycle acceptance. GUI rendering and each plugin's final tempo-map conversion are owned by the adapters. CC-directory deletion/undo integration must rebuild from effective post-edit events; the directory does not interpret application-specific eraser strokes itself. Audio reference audition does not reproduce third-party instrument sounds.

No full plugin tests were re-run merely to duplicate an integration owner's result. No claim is made that leak sanitizer ran successfully in this environment.
