#include <PianoRollCore/Controllers.h>
#include <algorithm>

namespace pianoroll {
void ControllerDirectory::observeEvent(int source, int controller) {
    if (source >= 0 && controller >= 0 && controller < 128) entries_[controller][source].events = true;
}
void ControllerDirectory::observeStroke(int source, int controller, std::size_t pointCount) {
    if (pointCount > 0 && source >= 0 && controller >= 0 && controller < 128) entries_[controller][source].edits = true;
}
std::vector<ControllerEntry> ControllerDirectory::entries() const {
    std::vector<ControllerEntry> result;
    result.reserve(entries_.size());
    for (const auto& controller : entries_) {
        ControllerEntry entry;
        entry.number = controller.first;
        for (const auto& source : controller.second) {
            entry.sources.push_back(source.first);
            entry.hasEvents = entry.hasEvents || source.second.events;
            entry.hasEdits = entry.hasEdits || source.second.edits;
        }
        result.push_back(std::move(entry));
    }
    return result;
}
std::vector<int> ControllerDirectory::controllers(int source) const {
    std::vector<int> result;
    for (const auto& entry : entries_)
        if (source < 0 || entry.second.count(source)) result.push_back(entry.first);
    return result;
}
std::vector<int> ControllerDirectory::sources(int controller) const {
    std::vector<int> result;
    const auto found = entries_.find(controller);
    if (found != entries_.end()) for (const auto& source : found->second) result.push_back(source.first);
    return result;
}
bool ControllerDirectory::contains(int source, int controller) const noexcept {
    const auto found = entries_.find(controller);
    return found != entries_.end() && found->second.count(source) > 0;
}
ControllerWriteTarget ControllerDirectory::writeTarget(int controller, int preferredSource,
                                                       const std::vector<int>& eligibleSources) const {
    if (controller < 0 || controller > 127) return {};
    std::set<int> eligible;
    for (int source : eligibleSources) if (source >= 0) eligible.insert(source);
    if (eligible.empty()) return {ControllerTargetStatus::invalid, -1, controller};
    if (preferredSource >= 0 && eligible.count(preferredSource))
        return {ControllerTargetStatus::ready, preferredSource, controller};
    if (eligible.size() == 1) return {ControllerTargetStatus::ready, *eligible.begin(), controller};
    return {ControllerTargetStatus::chooseSource, -1, controller};
}
std::string controllerName(int number) {
    if (number < 0 || number > 127) return {};
    const char* label = "";
    switch (number) {
        case 1: label = " - Modulation"; break;
        case 7: label = " - Volume"; break;
        case 10: label = " - Pan"; break;
        case 11: label = " - Expression"; break;
        case 64: label = " - Sustain"; break;
        default: break;
    }
    return "CC" + std::to_string(number) + label;
}
} // namespace pianoroll
