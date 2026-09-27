#pragma once

#include "Interaction.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pianoroll {

// Toolkit-neutral colour.  JUCE, Win32 and other adapters map this to their
// native colour type; the core never depends on a windowing library.
struct VisualColor {
    double r = 0, g = 0, b = 0, a = 1;
};

VisualColor mixVisualColor(VisualColor from, VisualColor to, double amount) noexcept;

struct PresentationConfig {
    double velocityBarMinimumWidth = 48;
    double velocityBarMinimumAlpha = .18;
    double pressedBoost = .24;
    double hoverBoost = .16;
    double selectedBoost = .08;
    double deleteFadeSeconds = .10;
};

struct NoteVisual {
    VisualColor fill{};
    VisualColor border{};
    VisualColor velocityBar{};
    double velocityLightness = .8;
    double velocityBarFraction = 0;
    double velocityBarAlpha = 0;
    bool showVelocityBar = false;
    bool keyRowActive = false;
};

// Derives every persistent note visual from document data plus the current
// interaction snapshot.  It does not mutate ActionNote or create undo work.
NoteVisual deriveNoteVisual(const ActionNote&, NoteFeedbackState state,
                            bool keyRowActive, double pixelWidth,
                            const PresentationConfig& = {}) noexcept;

VisualColor keyboardRowColor(int pitch, bool blackKey, bool active,
                             bool pressed = false) noexcept;

struct NoteFadeEffect {
    ActionNote snapshot;
    double startSeconds = 0;
    double durationSeconds = .10;
};

double fadeOpacity(const NoteFadeEffect&, double nowSeconds) noexcept;

// Render-only animation storage.  The document is removed immediately by
// the adapter; this class keeps a short snapshot for the fade-out paint pass.
class TransientEffects {
public:
    void clear() noexcept { fades_.clear(); }
    void beginDeleteFade(const ActionNote&, double nowSeconds,
                         double durationSeconds = .10);
    void update(double nowSeconds);
    const std::vector<NoteFadeEffect>& fades() const noexcept { return fades_; }
private:
    std::vector<NoteFadeEffect> fades_;
};

} // namespace pianoroll
