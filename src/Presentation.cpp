#include <PianoRollCore/Presentation.h>

#include <cmath>

namespace pianoroll {
namespace {
VisualColor baseColor(const ActionNote& note) noexcept {
    if (note.muted) return {.42, .45, .48, .72};
    if (note.keySwitch) return {.74, .48, .88, 1};
    if (note.generated) return {.91, .61, .30, 1};
    if (!note.editable) return {.38, .46, .52, .82};
    if (note.selected) return {.30, .62, .95, 1};
    return {.34, .65, .45, 1};
}
}

VisualColor mixVisualColor(VisualColor from, VisualColor to, double amount) noexcept {
    amount = std::clamp(std::isfinite(amount) ? amount : 0.0, 0.0, 1.0);
    return {
        from.r + (to.r - from.r) * amount,
        from.g + (to.g - from.g) * amount,
        from.b + (to.b - from.b) * amount,
        from.a + (to.a - from.a) * amount,
    };
}

NoteVisual deriveNoteVisual(const ActionNote& note, NoteFeedbackState state,
                            bool keyRowActive, double pixelWidth,
                            const PresentationConfig& config) noexcept {
    NoteVisual visual;
    visual.velocityLightness = velocityLightness(note.velocity);
    visual.fill = mixVisualColor(baseColor(note), {1, 1, 1, 1},
                                 std::max(0.0, visual.velocityLightness - .55));
    if (state == NoteFeedbackState::hover)
        visual.fill = mixVisualColor(visual.fill, {1, 1, 1, 1}, config.hoverBoost);
    else if (state == NoteFeedbackState::pressed)
        visual.fill = mixVisualColor(visual.fill, {1, 1, 1, 1}, config.pressedBoost);
    else if (state == NoteFeedbackState::selected)
        visual.fill = mixVisualColor(visual.fill, {1, 1, 1, 1}, config.selectedBoost);
    else if (state == NoteFeedbackState::overlapCandidate)
        visual.fill = mixVisualColor(visual.fill, {1, .86, .38, 1}, .24);

    visual.border = note.selected ? VisualColor{.86, .94, 1, 1} :
                    (state == NoteFeedbackState::pressed ? VisualColor{1, 1, 1, 1} :
                     VisualColor{.52, .68, .58, .95});
    visual.velocityBar = mixVisualColor(visual.fill, {0, .12, .08, 1}, .55);
    visual.velocityBarFraction = std::clamp(note.velocity / 127.0, 0.0, 1.0);
    visual.velocityBarAlpha = std::clamp(.28 + visual.velocityBarFraction * .35,
                                         config.velocityBarMinimumAlpha, 1.0);
    if (note.muted) {
        visual.fill.a *= .55;
        visual.border = {.48, .52, .55, .8};
        visual.velocityBarAlpha *= .35;
    }
    visual.showVelocityBar = pixelWidth >= config.velocityBarMinimumWidth && note.editable;
    visual.keyRowActive = keyRowActive;
    return visual;
}

VisualColor keyboardRowColor(int pitch, bool blackKey, bool active,
                             bool pressed) noexcept {
    VisualColor color = blackKey ? VisualColor{.12, .13, .16, 1} :
                                   VisualColor{.70, .73, .77, 1};
    if (active) color = mixVisualColor(color, {.96, .60, .25, 1}, pressed ? .72 : .48);
    return color;
}

double fadeOpacity(const NoteFadeEffect& effect, double nowSeconds) noexcept {
    if (!std::isfinite(nowSeconds) || !std::isfinite(effect.startSeconds) ||
        !std::isfinite(effect.durationSeconds) || effect.durationSeconds <= 0) return 0;
    const double t = std::clamp((nowSeconds - effect.startSeconds) /
                                effect.durationSeconds, 0.0, 1.0);
    // A short smoothstep keeps the disappearance quick without a hard pop.
    const double eased = t * t * (3.0 - 2.0 * t);
    return 1.0 - eased;
}

void TransientEffects::beginDeleteFade(const ActionNote& note, double nowSeconds,
                                       double durationSeconds) {
    if (!std::isfinite(nowSeconds)) nowSeconds = 0;
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0) durationSeconds = .10;
    fades_.push_back({note, nowSeconds, durationSeconds});
}

void TransientEffects::update(double nowSeconds) {
    fades_.erase(std::remove_if(fades_.begin(), fades_.end(),
        [&](const NoteFadeEffect& effect) { return fadeOpacity(effect, nowSeconds) <= 0; }),
        fades_.end());
}

} // namespace pianoroll
