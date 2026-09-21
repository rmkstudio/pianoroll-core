# Commands and musical editing integration

All new APIs run on the message/UI thread, only when requested. They do not add work to `PlaybackEngine::render`, create a polling thread, or register a timer. A plugin rebuilds its preview schedule after a successful musical edit using its existing scheduling path. No claim is made that executing a large edit or macro costs zero CPU.

## One route for every action

`standardPianoRollCommands()` supplies stable string IDs, bilingual names/search keywords, parameter schemas, and default bindings. Add supported commands to a `CommandRegistry` once, then keep a `CommandConfiguration`, a product-specific `CommandHost`, and a `CommandDispatcher` together for the editor lifetime. Menus, buttons, shortcuts and macros must reach the same adapter actions. The old `editorCommand` helper remains source compatible, but must not execute alongside custom bindings.

`resolveKey(chord, textInputFocused)` returns `consumed=true` when a binding exists, regardless of current availability. The adapter consumes that event even if `execute` reports Busy or Select notes first; otherwise the host could receive an unexpected Delete/Space. Text-entry focus returns no match. Keys use exact modifiers; `command` means Ctrl on Windows and Command on macOS. Persisted IDs survive display-name and language changes.

## Parameters and settings

Parameters use a string dictionary with a typed schema (`integer`, finite bounded `number`, `choice`, or `text`). The registry rejects unknown names and invalid values before any adapter action. UI numerical time parameters are **quarter-note beats**; an original-tick adapter converts them once before invoking musical math. Pure `NoteActions` and `AutomationActions` accept the adapter's native time units.

Use `parseCommandNumber(text)` and `parseCommandInteger(text)` in adapters. Both use the registry's classic-locale, full-string finite-number parser and return `std::optional`; integer reads also require an integral value within the C++ `int` range. They do not supply silent fallback values. Existing whitespace acceptance is preserved: leading whitespace is accepted, trailing whitespace is rejected. Registry schema bounds remain authoritative.

Since 0.3.1, validation also canonicalises declared integer arguments to ordinary decimal strings: `1e2` and `100.0` become `100`. This applies to execution, remembered settings, macro steps and configuration loading, so legacy integer consumers receive the same value that validation accepted. Number/text/choice strings are preserved; the JSON schema and host callback signatures are unchanged. Custom integer schemas may exceed C++ `int`; such hosts must choose a suitable numeric representation instead of using the int reader.

`setArguments` remembers ordinary command settings; explicit invocation arguments override the remembered settings. Q and Alt+Q share one grid, division, Swing, endpoint and range template. Alt+Q fixes both strengths at 50%; editing its shared options preserves Q's strength. Shift+Q invokes the adapter's original-timing ledger, not undo-history traversal. M is this project's chosen default for mirror, not a claim about another application's default.

Configuration replacement is validated before publication. Empty binding overrides deliberately remove defaults; they are not missing configuration. Conflicts require an explicit replace action. Macro IDs start `macro.` and removal also removes their shortcuts. The optional `CommandPreferences` validates bounded JSON and writes through a temporary file; corrupt settings remain available for recovery rather than being silently overwritten.

## Atomic macros

A macro contains 1–32 ordered steps and an independent saved parameter dictionary per step. There are at most 64 macros. No nested macros, file operations, transport, undo/redo or asynchronous command is eligible. Registration's `macroAllowed` flag must only be set for adapter actions that can be staged without external side effects.

1. Core structurally validates the complete sequence and fills declared defaults. It never reads per-command remembered settings into a saved macro step.
2. `preflightMacro` simulates steps on a disposable working copy **in sequence**, including selection and internal clipboard. `Select all → Copy` must be evaluated using the new selection, not rejected because the live selection started empty.
3. `beginMacro` creates an isolated working copy; `applyMacroStep` updates it. Preview, document, external clipboard and undo remain untouched.
4. `commitMacro` publishes once and creates one undo item. Any failed begin/step/commit or exception invokes `rollbackMacro`. Adapter commit must be atomic itself; throwing after publishing cannot be repaired by core.

The core does not know a plugin document or its revision. The adapter owns revision checks, stale-work rejection, original IDs, CC data, original timing, selection and clipboard snapshots. Reentrant dispatch is rejected. Cancellation inside a UI dialog must happen before the transaction begins.

## Musical helpers

`NoteActions` preserves fields it does not transform. `originalIndex` identifies the source note for copying unknown product fields; `generated=true` requires a fresh product note ID. Original timing is supplied through `OriginalTiming`, so restore timing can leave subsequent pitch and velocity edits intact. `ActionRules` bound pitch/time/note counts and exclude read-only notes or explicitly tagged Key Switches from musical transforms.

`AutomationScope` identifies one original source (track **and** channel), a controller set, and a phrase interval. It never means all visible voices. Controllers with action semantics (96/97 and 120–127) are excluded from curve transformations. Real event boundary states are preserved, empty lanes remain empty, and unknown required restore states reject the whole operation. Use `copyAutomation` for clipboard source snapshots that differ from the current destination document, and `eraseAutomationRange` for the source half of linked cut/delete. Manual lane erasure remains an explicit separate operation and may remove an entire lane. Coalesce after the adapter's final tick rounding.

`MusicTools::PitchLayout` changes only row mapping: drawing, pitch labels and hit testing must all use it. Original MIDI pitches remain intact. `ArticulationMap` is explicitly supplied **per original source track** by the adapter; its optional channel filter cannot authorize cross-track guesses based on low pitch. `StepInput` accumulates a chord until all pressed keys release, then returns one chord and advances. Route input through a bounded audio-to-UI queue and never call `StepInput` from the audio callback; disable/overflow/state replacement cancels held chords. Gate percentage and recorded/fixed velocity are adapter entry settings.

## Verification and extension

Pure tests cover command routing/configuration/transaction semantics and musical helpers. `juce/tests` covers actual shared controls, preferences and transform gestures. Each product additionally verifies its real keyboard handler, document/undo/state/export paths and source mapping. New actions should add one registry specification and one staged adapter operation, then use the existing UI and dispatch path. User documentation is in `EDITING-GUIDE.zh-CN.md`.
