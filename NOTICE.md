# Source and license notices

PianoRollCore 0.3.1 is a shared extraction and adaptation of piano-roll interaction and geometry logic from the user's MIDI Lines and MIDI Recover plugin projects. Distribution follows the existing GNU GPL version 3 route. The complete license is in LICENSE. This module does not claim new ownership of the original contributions.

Sources examined for this extraction:

- MIDI Lines 0.5.4: `Source/Editor.cpp`, `Source/Interaction.cpp`, `Source/EditScope.h`, `Source/Processor.cpp`. Its README states: “This native application is distributed under GPL-3.0; complete corresponding source and build scripts are included.” That attribution is preserved without inventing an author or changing it to MIT.
- MIDI Recover 0.4.7: `Source/PianoRoll.cpp`, `Source/PianoRoll.h`, `Source/Processor.cpp`. The bundled NOTICE attributes “New plugin UI and integration code: R Audio, 2026. GPL-3.0-or-later.” That source notice remains applicable to its contributions. The bundled NOTICE still refers to 0.4.6; the input archive version used for this extraction is 0.4.7.

Extracted concepts include viewport transforms, note rectangles and hit selection, snapping, eligible group bounds, copied-note anchor mapping, lasso intersection, and the edit gesture's revision/commit boundary. They are implemented here with unit-neutral C++ value types and configurable policies; plugin-specific MIDI data, recovery code and algorithms remain in their projects.

No JUCE source, rmkstudio's separate MIT voice-separation algorithm, PyFLP, CPython, Steinberg SDK, or Bach test dataset is included in this module. Their licenses remain with the respective plugin distributions. This package's test notes are synthetic inputs for the operations being extracted.

No additional copyright author names have been inferred or created.

Version 0.2.0 extends the extraction using MIDI Lines 0.5.5 and MIDI Recover 0.4.8: pointer navigation, complete keyboard command mapping, ruler and follow behavior, group scopes, velocity stroke rules, CC catalog, musical meter grid, and the Lines reference audition/transport engine. These remain adaptations of the same user projects under the existing GPL distribution route. The meter and controller tests use synthetic events; no private song or FLP project is included.

Version 0.3.0 adds original implementations of generic note processing, controller transformations, musical display/entry models, command dispatch, preferences and an optional JUCE command panel. Workflow ideas were discussed using the public English Studio One / Fender Studio Pro manual; no product implementation code, proprietary assets or manual text are embedded. Quantization and mirror behavior are this module's explicit policies and do not claim binary or preset compatibility with another DAW. The optional UI uses the plugin's existing JUCE dependency under its existing license.
