#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pianoroll {
struct ControllerEntry {
    int number = -1;
    std::vector<int> sources;
    bool hasEvents = false, hasEdits = false;
};
enum class ControllerTargetStatus { invalid, chooseSource, ready };
struct ControllerWriteTarget {
    ControllerTargetStatus status = ControllerTargetStatus::invalid;
    int source = -1, controller = -1;
};
// Contains only controllers with actual events or nonempty user strokes.
// Selecting/adding an empty lane is UI state and must not manufacture an event.
// Eraser gestures must NOT be observed as strokes: rebuild from the final merged
// events after erasure so an empty lane disappears and undo restores it.
class ControllerDirectory {
public:
    void clear() noexcept { entries_.clear(); }
    void observeEvent(int source, int controller);
    void observeStroke(int source, int controller, std::size_t pointCount);
    std::vector<ControllerEntry> entries() const;
    std::vector<int> controllers(int source = -1) const;
    std::vector<int> sources(int controller) const;
    bool contains(int source, int controller) const noexcept;
    // eligibleSources is the current editable destination set, including sources
    // with notes but no CC yet. Never silently writes to multiple sources.
    ControllerWriteTarget writeTarget(int controller, int preferredSource,
                                     const std::vector<int>& eligibleSources) const;
private:
    struct Presence { bool events = false, edits = false; };
    std::map<int, std::map<int, Presence>> entries_;
};
std::string controllerName(int number);
} // namespace pianoroll
