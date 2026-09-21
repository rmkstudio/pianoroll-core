#pragma once
#include <PianoRollCore/Commands.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace pianoroll {

// These helpers and components are message-thread only. The plugin owns their
// lifetime; destroy panels before destroying the host or its LookAndFeel.
KeyChord chordFromKeyPress(const juce::KeyPress&);
bool textInputHasFocus(const juce::Component* origin = nullptr);

class CommandPreferences {
public:
    explicit CommandPreferences(juce::File);
    static juce::File defaultFile(const juce::String& productId);
    juce::Result load(CommandDispatcher&);
    juce::Result save(const CommandDispatcher&);
    const juce::File& file() const noexcept { return file_; }
    const juce::String& warning() const noexcept { return warning_; }
private:
    juce::File file_;
    juce::String warning_, lastContents_;
    bool invalidOriginal_ = false;
};

class CommandPanel final : public juce::Component {
public:
    enum class Page { search, shortcuts, macros };
    CommandPanel(CommandDispatcher&, std::function<juce::Result()> save,
                 std::function<void()> close);
    ~CommandPanel() override;
    void showPage(Page);
    void setSearchText(const juce::String&);
    void setNotice(const juce::String&);
    void focusSearch();
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommandPanel)
};

// A small optional JUCE integration target keeps the core independent of JUCE.
// Panel labels are intentionally compact; hosts may add localized command
// names/keywords in their registry while retaining stable command IDs.
} // namespace pianoroll
