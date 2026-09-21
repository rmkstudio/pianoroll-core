# Optional JUCE command interface

Link `PianoRollCore::CommandUI`, include `PianoRollCore/CommandUI.h`, and own a
`CommandPanel` in the plugin editor. It inherits the enclosing LookAndFeel.
Destroy the panel before its dispatcher, command host, or LookAndFeel.

- `CommandPanel(dispatcher, save, close)` offers command search, key assignment,
  and macro editing. `setSearchText` can open a specific tool's parameter form.
  Search Enter executes; Escape closes or cancels key capture. Unsaved macro
  drafts require Save or Discard before closing. Parameter schemas generate the
  same form for single actions and macro steps.
  Successful command execution also uses this close policy: with a dirty macro,
  the command completes once and the panel returns to the intact macro draft.
  Saving or discarding the draft does not execute the command again.
- `CommandPreferences::defaultFile(productId)` gives each product a distinct
  local settings file. Call `load(dispatcher)` on editor creation and `save` only
  after an explicit settings change. These settings do not belong in host
  project state. A failed load leaves session settings intact; invalid original
  bytes are copied to an `.invalid-*.json` sibling before a later explicit save.
  Writes use a same-directory temporary file, closed and checked before replace.
- `chordFromKeyPress` normalizes JUCE input; dispatch only when piano-roll focus
  owns the shortcut. `textInputHasFocus` keeps typing inside text editors.
  Product menus and shortcuts should call the same dispatcher.
  Insert and F1 through F24 are converted with their exact modifiers. Actual
  delivery still depends on the keyboard, operating system and DAW focus.
- Macros contain at most 32 synchronous steps. Their command host stages all
  data/clipboard/selection changes and publishes once; no step may trigger a
  background task, dialog, file operation, nested macro, or partial undo entry.
- Use human-readable source choices. Current track/channel is selected by the
  product's named source selector, never by asking users for internal IDs.

`TransformOverlay` is an optional child over the active controller lane. Enable
it only while transforming a selected range, set component bounds to the lane,
and pass a selection rectangle in the overlay's local coordinates. Top/bottom
handles scale values, corners produce ramps, the interior shifts values, and
optional left/right handles stretch time. `ValueTransformBox.timeOffset` is a
fraction of the original selection duration. Call `valueTransformFromBox` and
always transform the mouse-down snapshot. `onBegin` opens a gesture, `onPreview`
updates its draft, `onCommit` publishes one undoable edit, and `onCancel` restores
the snapshot. Velocity lanes normally disable time editing.

The command interface uses no timer, global key hook, worker, or audio callback.
It runs when opened, when a control is edited, or when a command is executed.
`tests/CMakeLists.txt` builds standalone real-control checks with JUCE 7.
