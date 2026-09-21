#pragma once
#include <PianoRollCore/AutomationActions.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoroll {
// Place over the current controller lane only while Transform mode is active.
// All deltas refer to the immutable snapshot taken by onBegin; never apply a
// preview to the previous preview. One mouse-up calls one commit. Escape cancels.
class TransformOverlay final : public juce::Component {
public:
    std::function<bool()> onBegin;
    std::function<void(const ValueTransformBox&)> onPreview;
    std::function<void(const ValueTransformBox&)> onCommit;
    std::function<void()> onCancel;
    void setSelectionBounds(juce::Rectangle<float>);
    void setTimeEditingEnabled(bool enabled) noexcept { timeEditing_ = enabled; }
    bool isDragging() const noexcept { return dragging_; }
    void cancelGesture();
    void paint(juce::Graphics&) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    enum class Handle { none, move, top, bottom, left, right, topLeft, topRight, bottomLeft, bottomRight };
    Handle handleAt(juce::Point<float>) const;
    juce::Point<float> pointFor(Handle) const;
    juce::Rectangle<float> selection_;
    juce::Point<float> down_;
    ValueTransformBox delta_;
    Handle handle_ = Handle::none;
    bool dragging_ = false, timeEditing_ = false;
};
} // namespace pianoroll
