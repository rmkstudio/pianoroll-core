#include <PianoRollCore/CommandUI.h>
#include <PianoRollCore/TransformOverlay.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace pianoroll;
namespace {
int checks = 0;
void require(bool condition, const char* message)
{ ++checks; if (!condition) throw std::runtime_error(message); }
juce::Component* find(juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* found = find(*child, id)) return found;
    return nullptr;
}
template <typename T> T& control(juce::Component& root, const char* id)
{
    auto* result = dynamic_cast<T*>(find(root, id));
    if (result == nullptr) throw std::runtime_error(std::string("Missing UI control: ") + id);
    return *result;
}
void click(juce::Component& root, const char* id)
{ auto& button = control<juce::TextButton>(root, id); require(button.isEnabled(), id); button.onClick(); }
void search(CommandPanel& panel, const char* query)
{ panel.setSearchText(juce::String::fromUTF8(query)); control<juce::TextEditor>(panel, "command.search").onTextChange(); }
struct Host final : CommandHost {
    int pitch = 60, velocity = 100, commits = 0, stagedPitch = 0, stagedVelocity = 0;
    bool transaction = false;
    CommandResult checkCommand(const CommandInvocation& command) const override
    { return command.id == "note.blocked" ? CommandResult::failure("Select notes first.") : CommandResult{}; }
    CommandResult apply(const CommandInvocation& command, int& p, int& v) const
    {
        if (auto checked = checkCommand(command); !checked.ok) return checked;
        const auto delta = command.arguments.find("delta");
        const auto amount = delta == command.arguments.end() ? std::optional<int>(1) : parseCommandInteger(delta->second);
        if (!amount) return CommandResult::failure("Invalid delta.");
        if (command.id == "note.pitch") p += *amount;
        if (command.id == "note.velocity") v += *amount;
        return {};
    }
    CommandResult executeCommand(const CommandInvocation& command) override { return apply(command, pitch, velocity); }
    CommandResult preflightMacro(const Macro& macro) const override
    {
        int p = pitch, v = velocity;
        for (const auto& step : macro.steps) { auto result = apply(step, p, v); if (!result.ok) return result; }
        return {};
    }
    CommandResult beginMacro(const std::string&) override
    { transaction = true; stagedPitch = pitch; stagedVelocity = velocity; return {}; }
    CommandResult applyMacroStep(const CommandInvocation& command) override { return apply(command, stagedPitch, stagedVelocity); }
    CommandResult commitMacro() override
    { pitch = stagedPitch; velocity = stagedVelocity; transaction = false; ++commits; return {}; }
    void rollbackMacro() noexcept override { transaction = false; }
};
CommandRegistry registry()
{
    CommandRegistry result;
    result.add({"note.pitch", "Transpose", "Notes", "pitch 音高", {{"P"}},
        {{"delta", "Semitones", "1", ParameterType::integer, -127, 127, {}}}, true});
    result.add({"note.velocity", "Velocity offset", "Notes", "volume 力度", {{"V"}},
        {{"delta", "Velocity", "-10", ParameterType::integer, -127, 127, {}}}, true});
    result.add({"note.blocked", "Blocked", "Notes", "", {}, {}, false});
    return result;
}
juce::MouseEvent mouse(juce::Component& component, juce::Point<float> point,
                       juce::Point<float> down, bool drag)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        &component, &component, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, drag};
}
void panelChecks(const juce::File& output)
{
    auto commands = registry(); Host host; CommandConfiguration config;
    CommandDispatcher dispatcher(commands, config, host);
    int saves = 0, closes = 0;
    CommandPanel panel(dispatcher, [&] { ++saves; return juce::Result::ok(); }, [&] { ++closes; });
    panel.setVisible(true);
    search(panel, "not-a-command");
    require(control<juce::ListBox>(panel, "command.results").getListBoxModel()->getNumRows() == 0, "empty search is honest");
    require(control<juce::Label>(panel, "command.status").getText().contains("No matching"), "empty search hint");
    search(panel, "blocked");
    require(control<juce::Label>(panel, "command.status").getText().contains("Select notes"), "unavailable reason");
    click(panel, "command.run"); require(closes == 0 && host.pitch == 60, "unavailable action does not run or close");
    search(panel, "transpose");
    control<juce::TextEditor>(panel, "parameter.delta").setText("12", false);
    panel.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    require(host.pitch == 72 && closes == 1 && saves == 1, "parameterized Enter executes and persists");
    require(dispatcher.argumentsFor("note.pitch").at("delta") == "12", "last parameter remembered");
    panel.showPage(CommandPanel::Page::shortcuts); search(panel, "transpose");
    click(panel, "shortcut.capture"); panel.keyPressed(juce::KeyPress('V'));
    require(control<juce::TextButton>(panel, "shortcut.assign").getButtonText() == "Replace", "conflict shown");
    click(panel, "shortcut.cancel");
    require(dispatcher.resolveKey({"V"}).invocation.id == "note.velocity", "cancel retains conflicting owner");
    click(panel, "shortcut.capture"); panel.keyPressed(juce::KeyPress('V')); click(panel, "shortcut.assign");
    require(dispatcher.resolveKey({"V"}).invocation.id == "note.pitch", "explicit Replace changes owner");
    require(dispatcher.bindingsFor("note.velocity").empty(), "old owner released");
    click(panel, "shortcut.default");
    require(dispatcher.resolveKey({"P"}).invocation.id == "note.pitch", "selected default restored");
    click(panel, "shortcut.resetAll");
    require(dispatcher.resolveKey({"V"}).invocation.id == "note.velocity", "all defaults restored");
    panel.setSize(616,351);
    require(!control<juce::TextButton>(panel,"shortcut.capture").getBounds().intersects(control<juce::TextButton>(panel,"shortcut.default").getBounds()),
            "Recover minimum window keeps shortcut controls separate");
    {
        juce::Image small(juce::Image::ARGB,panel.getWidth(),panel.getHeight(),true);
        juce::Graphics drawing(small);panel.paintEntireComponent(drawing,true);
        juce::FileOutputStream stream(output.getChildFile("command-shortcuts-minimum.png"));
        juce::PNGImageFormat().writeImageToStream(small,stream);
    }
    panel.setSize(720,590);
    panel.showPage(CommandPanel::Page::macros); click(panel, "macro.new");
    auto& name = control<juce::TextEditor>(panel, "macro.name"); name.setText("Octave layer", false); name.onTextChange();
    auto& choice = control<juce::ComboBox>(panel, "macro.command");
    choice.setSelectedId(1, juce::dontSendNotification); choice.onChange();
    auto* form = find(panel, "macro.parameters"); require(form != nullptr, "macro parameter form exists");
    auto* delta = &control<juce::TextEditor>(*form, "parameter.delta"); delta->setText("12", false);
    click(panel, "macro.addStep");
    choice.setSelectedId(2, juce::dontSendNotification); choice.onChange();
    delta = &control<juce::TextEditor>(*form, "parameter.delta"); delta->setText("-10", false);
    click(panel, "macro.addStep"); click(panel, "macro.up"); click(panel, "macro.down"); click(panel, "macro.save");
    require(config.macros.size() == 1 && config.macros[0].steps.size() == 2, "UI creates real two-step macro");
    require(config.macros[0].steps[0].id == "note.pitch" && config.macros[0].steps[0].arguments.at("delta") == "12", "step order and values retained");
    click(panel, "macro.run"); require(host.pitch == 84 && host.velocity == 90 && host.commits == 1, "macro is a single transaction");
    click(panel, "macro.shortcut"); click(panel, "shortcut.capture");
    panel.keyPressed(juce::KeyPress('M', juce::ModifierKeys::ctrlModifier, 0)); click(panel, "shortcut.assign");
    require(dispatcher.resolveKey({"M", true}).invocation.id == config.macros[0].id, "UI binds macro shortcut");
    require(chordFromKeyPress(juce::KeyPress('Q', juce::ModifierKeys::altModifier, 0)) == KeyChord{"Q", false, false, true}, "JUCE key conversion");
    panel.showPage(CommandPanel::Page::macros);
    juce::Image screenshot(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics graphics(screenshot); panel.paintEntireComponent(graphics, true);
    juce::FileOutputStream stream(output.getChildFile("command-macros.png")); juce::PNGImageFormat().writeImageToStream(screenshot, stream);
}
void closeAndKeyChecks()
{
    for (const bool saveDraft : {false, true}) {
        auto commands = registry(); Host host; CommandConfiguration config;
        CommandDispatcher dispatcher(commands, config, host);
        int closes = 0;
        CommandPanel panel(dispatcher, [] { return juce::Result::ok(); }, [&] { ++closes; });
        panel.setVisible(true); panel.showPage(CommandPanel::Page::macros);
        click(panel, "macro.new");
        auto& name = control<juce::TextEditor>(panel, "macro.name");
        name.setText("Unsaved draft", false); name.onTextChange();
        click(panel, "macro.addStep");
        panel.showPage(CommandPanel::Page::search); search(panel, "transpose");
        control<juce::TextEditor>(panel, "parameter.delta").setText("1e1", false);
        click(panel, "command.run");
        require(host.pitch == 70 && closes == 0, "successful command does not close over an unsaved macro");
        require(name.isVisible() && name.getText() == "Unsaved draft" &&
                control<juce::ListBox>(panel, "macro.steps").getListBoxModel()->getNumRows() == 1,
                "closing redirects to the intact macro draft");
        panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(closes == 0, "Escape uses the same unsaved draft policy");
        click(panel, saveDraft ? "macro.save" : "macro.discard");
        panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(closes == 1 && config.macros.size() == (saveDraft ? 1u : 0u),
                "saving or discarding permits close without replaying the command");
        require(host.pitch == 70, "resolving the draft does not run the command again");
    }
    auto commands = registry(); Host host; CommandConfiguration config;
    CommandDispatcher dispatcher(commands, config, host);
    CommandPanel panel(dispatcher, [] { return juce::Result::ok(); }, [] {});
    panel.setVisible(true); panel.showPage(CommandPanel::Page::shortcuts); search(panel, "transpose");
    for (int number = 0; number <= 24; ++number) {
        const int code = number == 0 ? juce::KeyPress::insertKey : juce::KeyPress::F1Key + number - 1;
        const auto key = juce::KeyPress(code, juce::ModifierKeys::altModifier, 0);
        const KeyChord expected{number == 0 ? "Insert" : "F" + std::to_string(number), false, false, true};
        require(chordFromKeyPress(key) == expected, "JUCE converts every advertised Insert/function key");
        click(panel, "shortcut.clear"); // Test each key independently of the 16-bindings-per-command limit.
        click(panel, "shortcut.capture"); panel.keyPressed(key); click(panel, "shortcut.assign");
        require(dispatcher.resolveKey(chordFromKeyPress(key)).invocation.id == "note.pitch",
                "captured key reaches its assigned command");
    }
}

struct FontLook final : juce::LookAndFeel_V4 {
    juce::Typeface::Ptr face;
    explicit FontLook(bool alternate)
    {
        auto* custom = new juce::CustomTypeface();
        face = custom;
        custom->setCharacteristics(alternate ? "Panel fixture B" : "Panel fixture A", "Regular", .8f, '?');
        const auto characters = juce::String::fromUTF8(u8"命令分类中文参数宏");
        for (int code = 32; code < 127; ++code) add(*custom, static_cast<juce::juce_wchar>(code), alternate);
        for (int i = 0; i < characters.length(); ++i) add(*custom, characters[i], alternate);
    }
    static void add(juce::CustomTypeface& custom, juce::juce_wchar code, bool alternate)
    {
        juce::Path path;
        if (code != ' ') {
            if (alternate) {
                path.startNewSubPath(.05f, -.72f); path.lineTo(.48f, -.72f);
                path.lineTo(.25f, -.06f); path.closeSubPath();
            } else {
                path.addRectangle(.05f, -.72f, .35f, .62f);
                path.addRectangle(.43f, -.46f, .08f, .34f);
            }
        }
        custom.addGlyph(code, path, .6f);
    }
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override { return face; }
};
bool pixelsEqual(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
{
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y).getARGB() != b.getPixelAt(x, y).getARGB()) return false;
    return true;
}
void requireRowFont(juce::ListBox& list, FontLook& look, bool macro)
{
    constexpr int width = 440;
    const int height = macro ? 43 : 45;
    juce::Image actual(juce::Image::ARGB, width, height, true), expected(actual.getFormat(), width, height, true);
    juce::Graphics graphics(actual);
    list.getListBoxModel()->paintListBoxItem(0, graphics, width, height, false);
    juce::Graphics reference(expected);
    reference.setColour(look.findColour(juce::PopupMenu::textColourId));
    reference.setFont(juce::Font(look.face).withHeight(14));
    if (macro) {
        reference.drawText(juce::String::fromUTF8(u8"1. 命令"), 8, 2, width - 16, 21, juce::Justification::centredLeft);
        reference.setOpacity(.66f); reference.setFont(juce::Font(look.face).withHeight(11.5f));
        reference.drawText(juce::String::fromUTF8(u8"text=中文"), 8, 23, width - 16, 17, juce::Justification::centredLeft);
        require(pixelsEqual(actual, expected, actual.getBounds()), "macro name and argument rows render the enclosing local typeface");
    } else {
        reference.drawText(juce::String::fromUTF8(u8"命令"), 9, 2, width - 185, 23, juce::Justification::centredLeft);
        reference.setFont(juce::Font(look.face).withHeight(12)); reference.setOpacity(.65f);
        reference.drawText(juce::String::fromUTF8(u8"分类"), 9, 24, width - 18, 17, juce::Justification::centredLeft);
        require(pixelsEqual(actual, expected, actual.getBounds()), "command name and category rows render the enclosing local typeface");
    }
    bool painted = false;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        painted = painted || actual.getPixelAt(x, y).getAlpha() != 0;
    require(painted, "local typeface row rendering has visible glyph pixels");
}
void requireFormFonts(juce::Component& form, const juce::Typeface::Ptr& face)
{
    int labels = 0, editors = 0;
    for (auto* child : form.getChildren()) {
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            require(label->getFont().getTypefacePtr() == face, "rebuilt parameter label uses inherited local typeface");
            ++labels;
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
            require(editor->getFont().getTypefacePtr() == face, "rebuilt parameter text uses inherited local typeface");
            ++editors;
        }
    }
    require(labels == 1 && editors == 1, "font fixture exercises the real parameter label and editor");
}
void requireComboFont(juce::ComboBox& combo, const juce::Typeface::Ptr& face)
{
    auto* label = dynamic_cast<juce::Label*>(combo.getChildComponent(0));
    require(label != nullptr && label->getFont().getTypefacePtr() == face,
            "macro combo label keeps local typeface after JUCE recreates it");
}
void localFontChecks()
{
    FontLook first(false), second(true);
    juce::Component parent;
    CommandRegistry commands;
    require(commands.add({"font.command", u8"命令", u8"分类", "", {},
        {{"text", u8"参数", u8"中文", ParameterType::text, 0, 0, {}}}, true}).ok,
        "Chinese font fixture has a valid command and parameter schema");
    Host host; CommandConfiguration config; CommandDispatcher dispatcher(commands, config, host);
    // Real product order: the panel and its initial form exist before attachment.
    CommandPanel panel(dispatcher, [] { return juce::Result::ok(); }, [] {});
    search(panel, u8"命令");
    auto& query = control<juce::TextEditor>(panel, "command.search");
    require(query.getText() == juce::String::fromUTF8(u8"命令") &&
            control<juce::ListBox>(panel, "command.results").getNumRows() == 1,
            "UTF-8 font fixture search selects its real parameterized command");
    const auto originalFace = query.getFont().getTypefacePtr();
    require(originalFace != first.face, "panel initially has the unchanged default typeface");
    parent.setLookAndFeel(&first);
    parent.addAndMakeVisible(panel);
    require(query.getFont().getTypefacePtr() == first.face, "late attachment rebinds an existing search editor font");
    requireFormFonts(*find(panel, "command.parameters"), first.face);
    requireRowFont(control<juce::ListBox>(panel, "command.results"), first, false);
    {
        // Existing TextEditor runs must change too, not only its insertion font.
        juce::TextEditor reference;
        reference.setLookAndFeel(&first);
        query.copyAllExplicitColoursTo(reference);
        reference.setSize(query.getWidth(), query.getHeight());
        reference.setFont(juce::Font(first.face).withHeight(query.getFont().getHeight()));
        reference.setText(query.getText(), false);
        const auto actual = query.createComponentSnapshot(query.getLocalBounds());
        const auto expected = reference.createComponentSnapshot(reference.getLocalBounds());
        require(pixelsEqual(actual, expected, actual.getBounds()), "late attachment rebinds already populated TextEditor glyph runs");
        reference.setLookAndFeel(nullptr);
    }
    search(panel, "not-a-command");
    search(panel, u8"命令");
    requireFormFonts(*find(panel, "command.parameters"), first.face);
    panel.showPage(CommandPanel::Page::macros);
    click(panel, "macro.new");
    auto& name = control<juce::TextEditor>(panel, "macro.name");
    name.setText(juce::String::fromUTF8(u8"中文宏"), false); name.onTextChange();
    require(name.getFont().getTypefacePtr() == first.face, "new macro text inherits the local typeface");
    auto& choice = control<juce::ComboBox>(panel, "macro.command");
    choice.setSelectedId(1, juce::dontSendNotification); choice.onChange();
    requireFormFonts(*find(panel, "macro.parameters"), first.face);
    requireComboFont(control<juce::ComboBox>(panel, "macro.command"), first.face);
    click(panel, "macro.addStep"); click(panel, "macro.save");
    requireComboFont(control<juce::ComboBox>(panel, "macro.choice"), first.face);
    panel.setSize(616, 351);
    requireComboFont(choice, first.face);
    requireComboFont(control<juce::ComboBox>(panel, "macro.choice"), first.face);
    panel.setSize(720, 590);
    requireRowFont(control<juce::ListBox>(panel, "macro.steps"), first, true);
    parent.setLookAndFeel(&second);
    require(query.getFont().getTypefacePtr() == second.face && name.getFont().getTypefacePtr() == second.face,
            "changing inherited LookAndFeel rebinds populated editors");
    requireFormFonts(*find(panel, "macro.parameters"), second.face);
    requireComboFont(control<juce::ComboBox>(panel, "macro.command"), second.face);
    requireComboFont(control<juce::ComboBox>(panel, "macro.choice"), second.face);
    requireRowFont(control<juce::ListBox>(panel, "macro.steps"), second, true);
    panel.showPage(CommandPanel::Page::search); search(panel, u8"命令");
    requireFormFonts(*find(panel, "command.parameters"), second.face);
    requireRowFont(control<juce::ListBox>(panel, "command.results"), second, false);
    parent.removeChildComponent(&panel);
    require(query.getFont().getTypefacePtr() == originalFace, "detaching the panel restores the default typeface");
    parent.setLookAndFeel(nullptr);
}

void preferenceChecks(const juce::File& output)
{
    auto commands = registry(); Host host; CommandConfiguration config; CommandDispatcher dispatcher(commands, config, host);
    auto file = output.getChildFile("preferences.json"); file.deleteFile(); CommandPreferences preferences(file);
    require(preferences.load(dispatcher).wasOk(), "missing preferences accepted");
    dispatcher.assign("note.pitch", {"T", true}); dispatcher.setArguments("note.pitch", {{"delta", "7"}});
    dispatcher.upsertMacro({"macro.test", "Test", {{"note.pitch", {{"delta", "12"}}}}});
    require(preferences.save(dispatcher).wasOk(), "preferences saved atomically");
    CommandConfiguration fresh; CommandDispatcher restored(commands, fresh, host);
    require(preferences.load(restored).wasOk(), "preferences reload");
    require(restored.resolveKey({"T", true}).invocation.id == "note.pitch" && fresh.macros.size() == 1, "binding and macro persist");
    require(restored.argumentsFor("note.pitch").at("delta") == "7", "parameters persist");
    require(file.replaceWithText("{broken"), "create corrupt fixture");
    require(preferences.load(restored).failed() && file.loadFileAsString() == "{broken", "corrupt original left untouched on load");
    require(preferences.save(restored).wasOk(), "explicit save recovers with backup");
    juce::Array<juce::File> backups; output.findChildFiles(backups, juce::File::findFiles, false, "preferences.invalid-*.json");
    require(backups.size() >= 1 && backups.getLast().loadFileAsString() == "{broken", "invalid original preserved in backup");
    require(preferences.load(restored).wasOk(), "recovered settings reopen");
}
void transformChecks()
{
    TransformOverlay overlay; overlay.setBounds(0, 0, 300, 200); overlay.setVisible(true);
    overlay.setSelectionBounds({30, 40, 200, 100});
    int begins = 0, commits = 0, cancels = 0; ValueTransformBox last;
    overlay.onBegin = [&] { ++begins; return true; };
    overlay.onPreview = [&](const ValueTransformBox& value) { last = value; };
    overlay.onCommit = [&](const ValueTransformBox& value) { ++commits; last = value; };
    overlay.onCancel = [&] { ++cancels; };
    overlay.mouseDown(mouse(overlay, {30, 40}, {30, 40}, false));
    overlay.mouseDrag(mouse(overlay, {30, 50}, {30, 40}, true));
    overlay.mouseDrag(mouse(overlay, {30, 60}, {30, 40}, true));
    require(std::abs(last.highLeft - .8) < 1e-9 && last.highRight == 1 && last.lowLeft == 0, "corner changes one ramp endpoint from original snapshot");
    overlay.mouseUp(mouse(overlay, {30, 60}, {30, 40}, true));
    require(begins == 1 && commits == 1, "one gesture one commit");
    overlay.setSelectionBounds({30, 40, 200, 100});
    overlay.mouseDown(mouse(overlay, {130, 90}, {130, 90}, false));
    overlay.mouseDrag(mouse(overlay, {150, 80}, {130, 90}, true));
    require(last.timeOffset == 0 && std::abs(last.lowLeft - .1) < 1e-9, "velocity box does not move note times");
    require(overlay.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape consumed");
    require(cancels == 1 && commits == 1 && !overlay.isDragging(), "Escape restores without committing");
    overlay.setTimeEditingEnabled(true); overlay.setSelectionBounds({30, 40, 200, 100});
    overlay.mouseDown(mouse(overlay, {230, 90}, {230, 90}, false));
    overlay.mouseDrag(mouse(overlay, {270, 90}, {230, 90}, true));
    overlay.mouseUp(mouse(overlay, {270, 90}, {230, 90}, true));
    require(std::abs(last.timeScale - 1.2) < 1e-9, "CC right edge stretches time");
}
} // namespace
int main(int argc, char** argv)
{
    try {
        juce::ScopedJuceInitialiser_GUI initialiser;
        auto output = argc > 1 ? juce::File(argv[1]) : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("piano-command-ui-checks");
        output.createDirectory(); panelChecks(output); closeAndKeyChecks(); localFontChecks(); preferenceChecks(output); transformChecks();
        std::cout << checks << " command UI, preference and gesture checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
