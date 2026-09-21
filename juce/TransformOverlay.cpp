#include <PianoRollCore/TransformOverlay.h>
#include <cmath>

namespace pianoroll {
void TransformOverlay::setSelectionBounds(juce::Rectangle<float> bounds)
{
    if (dragging_) cancelGesture();
    selection_ = bounds; delta_ = {}; repaint();
}
juce::Point<float> TransformOverlay::pointFor(Handle handle) const
{
    const float x0 = selection_.getX() + static_cast<float>(delta_.timeOffset) * selection_.getWidth();
    const float x1 = x0 + static_cast<float>(delta_.timeScale) * selection_.getWidth();
    const auto y = [this](double value) { return selection_.getBottom() - static_cast<float>(value) * selection_.getHeight(); };
    switch (handle) {
        case Handle::topLeft: return {x0, y(delta_.highLeft)};
        case Handle::topRight: return {x1, y(delta_.highRight)};
        case Handle::bottomLeft: return {x0, y(delta_.lowLeft)};
        case Handle::bottomRight: return {x1, y(delta_.lowRight)};
        case Handle::top: return {(x0 + x1) * .5f, y((delta_.highLeft + delta_.highRight) * .5)};
        case Handle::bottom: return {(x0 + x1) * .5f, y((delta_.lowLeft + delta_.lowRight) * .5)};
        case Handle::left: return {x0, y((delta_.lowLeft + delta_.highLeft) * .5)};
        case Handle::right: return {x1, y((delta_.lowRight + delta_.highRight) * .5)};
        default: return {(x0 + x1) * .5f, y((delta_.lowLeft + delta_.highLeft + delta_.lowRight + delta_.highRight) * .25)};
    }
}
TransformOverlay::Handle TransformOverlay::handleAt(juce::Point<float> point) const
{
    if (selection_.isEmpty()) return Handle::none;
    for (auto handle : {Handle::topLeft, Handle::topRight, Handle::bottomLeft, Handle::bottomRight,
                        Handle::top, Handle::bottom, Handle::left, Handle::right}) {
        if (!timeEditing_ && (handle == Handle::left || handle == Handle::right)) continue;
        if (point.getDistanceFrom(pointFor(handle)) <= 7.0f) return handle;
    }
    return selection_.contains(point) ? Handle::move : Handle::none;
}
void TransformOverlay::paint(juce::Graphics& graphics)
{
    if (selection_.isEmpty()) return;
    auto colour = getLookAndFeel().findColour(juce::PopupMenu::textColourId);
    juce::Path shape;
    shape.startNewSubPath(pointFor(Handle::topLeft));
    shape.lineTo(pointFor(Handle::topRight)); shape.lineTo(pointFor(Handle::bottomRight));
    shape.lineTo(pointFor(Handle::bottomLeft)); shape.closeSubPath();
    graphics.setColour(colour.withAlpha(.07f)); graphics.fillPath(shape);
    graphics.setColour(colour.withAlpha(.8f)); graphics.strokePath(shape, juce::PathStrokeType(1.0f));
    for (auto handle : {Handle::topLeft, Handle::topRight, Handle::bottomLeft, Handle::bottomRight,
                        Handle::top, Handle::bottom, Handle::left, Handle::right}) {
        if (!timeEditing_ && (handle == Handle::left || handle == Handle::right)) continue;
        auto point = pointFor(handle);
        graphics.fillRect(juce::Rectangle<float>(point.x - 3, point.y - 3, 6, 6));
    }
}
bool TransformOverlay::hitTest(int x, int y) { return handleAt({static_cast<float>(x), static_cast<float>(y)}) != Handle::none; }
void TransformOverlay::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || dragging_) return;
    handle_ = handleAt(event.position);
    if (handle_ == Handle::none || (onBegin && !onBegin())) { handle_ = Handle::none; return; }
    setWantsKeyboardFocus(true); grabKeyboardFocus();
    down_ = event.position; delta_ = {}; dragging_ = true;
}
void TransformOverlay::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging_) return;
    delta_ = {};
    const auto dy = static_cast<double>(event.position.y - down_.y) / juce::jmax(1.0f, selection_.getHeight());
    const auto dx = static_cast<double>(event.position.x - down_.x) / juce::jmax(1.0f, selection_.getWidth());
    switch (handle_) {
        case Handle::move:
            delta_.lowLeft = delta_.lowRight = -dy;
            delta_.highLeft = delta_.highRight = 1.0 - dy;
            if (timeEditing_) delta_.timeOffset = dx;
            break;
        case Handle::top: delta_.highLeft = delta_.highRight = juce::jmax(0.0, 1.0 - dy); break;
        case Handle::bottom: delta_.lowLeft = delta_.lowRight = juce::jmin(1.0, -dy); break;
        case Handle::topLeft: delta_.highLeft = juce::jmax(0.0, 1.0 - dy); break;
        case Handle::topRight: delta_.highRight = juce::jmax(0.0, 1.0 - dy); break;
        case Handle::bottomLeft: delta_.lowLeft = juce::jmin(1.0, -dy); break;
        case Handle::bottomRight: delta_.lowRight = juce::jmin(1.0, -dy); break;
        case Handle::left: delta_.timeOffset = juce::jmin(.99, dx); delta_.timeScale = 1.0 - delta_.timeOffset; break;
        case Handle::right: delta_.timeScale = juce::jmax(.01, 1.0 + dx); break;
        default: break;
    }
    if (onPreview) onPreview(delta_);
    repaint();
}
void TransformOverlay::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_) return;
    const auto delta = delta_;
    dragging_ = false; handle_ = Handle::none;
    if (onCommit) onCommit(delta);
}
void TransformOverlay::cancelGesture()
{
    if (!dragging_) return;
    dragging_ = false; handle_ = Handle::none; delta_ = {};
    if (onCancel) onCancel();
    repaint();
}
void TransformOverlay::mouseMove(const juce::MouseEvent& event)
{
    const auto handle = handleAt(event.position);
    setMouseCursor(handle == Handle::left || handle == Handle::right ? juce::MouseCursor::LeftRightResizeCursor
        : handle == Handle::move ? juce::MouseCursor::DraggingHandCursor
        : handle == Handle::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}
bool TransformOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey && dragging_) { cancelGesture(); return true; }
    return false;
}
} // namespace pianoroll
