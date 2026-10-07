# Optional JUCE command interface

Link `PianoRollCore::CommandUI`, include `PianoRollCore/CommandUI.h`, and own a
`CommandPanel` in the plugin editor. It inherits the enclosing LookAndFeel.
Destroy the panel before its dispatcher, command host, or LookAndFeel.
Command and macro rows, parameter labels and populated text editors explicitly
bind the enclosing LookAndFeel's default typeface. This is refreshed after late
parent attachment, parameter form rebuilding and subsequent LookAndFeel changes;
no application-wide default font is changed.

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

## Piano-roll view (0.5.0)

Link `PianoRollCore::View` and include `PianoRollCore/PianoRollView.h`.
`PianoRollView` is a `juce::Component` that draws the ruler, keys, notes,
velocity lane, controller lane, playhead, loop and hover feedback, and runs
every mouse gesture: pencil, select/marquee, move, resize, Shift copy, erase,
velocity stroke, controller stroke, middle-button and wheel navigation, ruler
seek/loop and lane boundaries. It also owns the generic one-row toolbar
(play/stop, clock, undo/redo, draw/select buttons, snap, the "编辑" edit menu,
follow) and the scroll bar. Buttons carry an `icon` property and joined ones a
`segment` property (`first`/`last`); the snap box carries `prefix`. A product
look-and-feel may draw icons from these, otherwise the button text is shown.

The product implements `PianoRollHost`:

- It supplies notes by index as `ViewNote` (start, length, pitch, velocity,
  group, channel, editable), selection, group scope, the controller lane
  (`LanePoint`), transport position and loop. Times use the product's unit;
  `roundTime` applies the product's final rounding.
- It receives each draft edit (`moveNote`, `setNoteLength`, `createNote`,
  `appendBaselineCopy`, `eraseNote`, `setNoteVelocity`, controller strokes and
  selection changes). `beginEdit` captures the product's before-image,
  `editChanged` marks the draft dirty and `commitEdit` publishes at most once
  per gesture. The view never keeps a second document or undo stack.
- `followMovedNotes` lets the product apply its own follow rules after a move;
  returning false after calling `endGesture()` cancels the gesture.
- `noteLabels` and `keySwitches` return per-pass lookups so a product index is
  built once per paint or drag step, not once per note.
- `viewLayout` defaults to `defaultViewLayout`; product controls (group
  selector, channel, All, controller, fit, expand, export drag) are placed in
  `placeProductControls`. A product may leave a slot empty and put the control
  elsewhere (Recover puts export drag in its editor footer). `ViewPalette`
  holds colours, the font and the group colour function.
- `addEditMenuItems` appends the product's own entries (view options, audition,
  solo...) to the edit menu, each with its own action.

A product may derive its editor from `PianoRollView` (MIDI Recover does, so its
existing tests keep sending mouse events to the same component) or own one as
a child. Do not call host functions from the host's own constructor before the
view is fully constructed. With "no snap", the step is one time unit; a product
that uses quarter-note beats as its unit must account for that until a host
setting exists.

## Indexed lane and fractional-time hosts (0.5.1)

Override `controllerLaneRange(start, end)` to return a sorted subset including the last point before start and first after end; points with equal times retain their order. The default delegates to `controllerLane()` for compatibility. Hidden controller lanes do not call either reader during paint.

Tick hosts can keep the default `minimumNoteLength()` of 1. Hosts using fractional beats must return their smallest supported positive duration, and retain fractional values in `roundTime()`. The view uses this duration for free/snap resolution, resize limits and the minimum zoom window; it does not assume that a time unit is a tick.
