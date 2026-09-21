#include <PianoRollCore/CommandUI.h>
#include <algorithm>

namespace pianoroll {
KeyChord chordFromKeyPress(const juce::KeyPress& key)
{
    KeyChord result;
    const auto code = key.getKeyCode();
    const std::pair<int, const char*> special[] = {
        {juce::KeyPress::spaceKey, "Space"}, {juce::KeyPress::returnKey, "Enter"},
        {juce::KeyPress::escapeKey, "Escape"}, {juce::KeyPress::backspaceKey, "Backspace"},
        {juce::KeyPress::deleteKey, "Delete"}, {juce::KeyPress::insertKey, "Insert"},
        {juce::KeyPress::homeKey, "Home"},
        {juce::KeyPress::endKey, "End"}, {juce::KeyPress::pageUpKey, "PageUp"},
        {juce::KeyPress::pageDownKey, "PageDown"}, {juce::KeyPress::tabKey, "Tab"},
        {juce::KeyPress::upKey, "Up"}, {juce::KeyPress::downKey, "Down"},
        {juce::KeyPress::leftKey, "Left"}, {juce::KeyPress::rightKey, "Right"}};
    for (const auto& item : special) if (code == item.first) result.key = item.second;
    if (result.key.empty() && code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F24Key)
        result.key = "F" + std::to_string(code - juce::KeyPress::F1Key + 1);
    if (result.key.empty() && code > 32 && code < 127) result.key.assign(1, static_cast<char>(code));
    const auto modifiers = key.getModifiers();
    result.command = modifiers.isCtrlDown() || modifiers.isCommandDown();
    result.shift = modifiers.isShiftDown(); result.alt = modifiers.isAltDown();
    return normalizeKeyChord(std::move(result));
}
bool textInputHasFocus(const juce::Component* origin)
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (origin != nullptr && focused != origin && !origin->isParentOf(focused)) return false;
    for (auto* component = focused; component != nullptr; component = component->getParentComponent())
        if (dynamic_cast<juce::TextEditor*>(component) != nullptr) return true;
    return false;
}
namespace {
class ParameterForm final : public juce::Component {
public:
    std::function<void()> onSubmit;
    std::function<void()> onEscape;
    void set(const CommandSpec* spec, const CommandArguments& values)
    {
        fields.clear();
        if (spec != nullptr) for (const auto& parameter : spec->parameters) {
            auto field = std::make_unique<Field>(); field->parameter = parameter;
            field->label.setText(juce::String(parameter.name), juce::dontSendNotification);
            field->label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(field->label);
            const auto value = values.find(parameter.id);
            const auto text = value == values.end() ? parameter.defaultValue : value->second;
            if (parameter.type == ParameterType::choice) {
                for (std::size_t i = 0; i < parameter.choices.size(); ++i)
                    field->choice.addItem(juce::String(parameter.choices[i]), static_cast<int>(i) + 1);
                const auto found = std::find(parameter.choices.begin(), parameter.choices.end(), text);
                if (found != parameter.choices.end()) field->choice.setSelectedId(static_cast<int>(found - parameter.choices.begin()) + 1);
                field->choice.setComponentID("parameter." + juce::String(parameter.id));
                addAndMakeVisible(field->choice);
            } else {
                field->editor.setText(juce::String(text), false);
                field->editor.setSelectAllWhenFocused(true);
                field->editor.onReturnKey = [this] { if (onSubmit) onSubmit(); };
                field->editor.onEscapeKey = [this] { if (onEscape) onEscape(); };
                field->editor.setComponentID("parameter." + juce::String(parameter.id));
                if (parameter.type != ParameterType::text)
                    field->editor.setTooltip(juce::String(parameter.minimum) + " ... " + juce::String(parameter.maximum));
                addAndMakeVisible(field->editor);
            }
            fields.push_back(std::move(field));
        }
        setSize(getWidth(), contentHeight()); resized();
    }
    CommandArguments values() const
    {
        CommandArguments result;
        for (const auto& field : fields)
            result.emplace(field->parameter.id, (field->parameter.type == ParameterType::choice
                ? field->choice.getText() : field->editor.getText()).toStdString());
        return result;
    }
    int columns() const { return getWidth() < 460 ? 1 : 2; }
    int contentHeight() const { return fields.empty() ? 0 : static_cast<int>((fields.size() + columns() - 1) / columns()) * 57; }
    void setContentWidth(int width)
    {
        const auto count = width < 460 ? 1 : 2;
        setSize(width, fields.empty() ? 0 : static_cast<int>((fields.size() + count - 1) / count) * 57);
    }
    void resized() override
    {
        const auto count = columns();
        const int width = juce::jmax(20, (getWidth() - (count - 1) * 12) / count);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            auto area = juce::Rectangle<int>(static_cast<int>(i % count) * (width + 12), static_cast<int>(i / count) * 57, width, 53);
            fields[i]->label.setBounds(area.removeFromTop(22));
            fields[i]->editor.setBounds(area); fields[i]->choice.setBounds(area);
        }
    }
private:
    struct Field { CommandParameter parameter; juce::Label label; juce::TextEditor editor; juce::ComboBox choice; };
    std::vector<std::unique_ptr<Field>> fields;
};
juce::String bindingLabel(const CommandDispatcher& dispatcher, const std::string& id)
{
    juce::StringArray labels;
    for (const auto& key : dispatcher.bindingsFor(id)) labels.add(juce::String(keyChordText(key)));
    return labels.joinIntoString(", ");
}
} // namespace

struct CommandPanel::Impl final : juce::ListBoxModel, juce::KeyListener {
    struct Entry { std::string id, name, category; bool macro = false; };
    struct StepsModel final : juce::ListBoxModel {
        explicit StepsModel(Impl& owner) : ui(owner) {}
        int getNumRows() override { return static_cast<int>(ui.draft.steps.size()); }
        void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
        {
            if (row < 0 || row >= getNumRows()) return;
            ui.paintRowBackground(graphics, width, height, selected);
            const auto& step = ui.draft.steps[static_cast<std::size_t>(row)];
            const auto* spec = ui.dispatcher.registry().find(step.id);
            graphics.setFont(juce::Font(14.0f));
            graphics.drawText(juce::String(row + 1) + ". " + juce::String(spec ? spec->name : step.id), 8, 2, width - 16, 21, juce::Justification::centredLeft);
            juce::StringArray parameters;
            for (const auto& parameter : step.arguments) parameters.add(juce::String(parameter.first) + "=" + juce::String(parameter.second));
            graphics.setOpacity(.66f); graphics.setFont(juce::Font(11.5f));
            graphics.drawText(parameters.joinIntoString(", "), 8, 23, width - 16, 17, juce::Justification::centredLeft);
        }
        void selectedRowsChanged(int row) override { ui.selectStep(row); }
        Impl& ui;
    };
    CommandPanel& panel;
    CommandDispatcher& dispatcher;
    std::function<juce::Result()> save;
    std::function<void()> close;
    Page page = Page::search;
    juce::TextButton searchTab{"Commands"}, shortcutTab{"Shortcuts"}, macroTab{"Macros"}, closeButton{"Close"};
    juce::TextEditor query;
    juce::ListBox results{"Command list", this};
    juce::TextButton run{"Run"}, capture{"Record shortcut"}, assign{"Assign"}, cancelKey{"Cancel"},
        clearKey{"Clear"}, resetKey{"Default"}, resetAll{"Reset all"};
    juce::Label currentKey, status;
    juce::Viewport parametersView;
    ParameterForm parameters;
    juce::ComboBox macroChoice, stepChoice;
    juce::TextEditor macroName;
    juce::TextButton newMacro{"New"}, deleteMacro{"Delete"}, saveMacro{"Save"}, runMacro{"Run"},
        bindMacro{"Shortcut"}, discardMacro{"Discard"}, addStep{"Add step"}, updateStep{"Update step"},
        removeStep{"Remove"}, upStep{"Up"}, downStep{"Down"};
    StepsModel stepsModel{*this};
    juce::ListBox steps{"Macro steps", &stepsModel};
    juce::Viewport stepParametersView;
    ParameterForm stepParameters;
    std::vector<Entry> entries;
    std::vector<std::string> stepCommands, macroIds;
    Macro draft;
    bool capturing = false, haveKey = false, macroDirty = false, loadingMacro = false;
    KeyChord candidate;
    int selectedMacro = 0;

    Impl(CommandPanel& owner, CommandDispatcher& commands, std::function<juce::Result()> saveCallback, std::function<void()> closeCallback)
        : panel(owner), dispatcher(commands), save(std::move(saveCallback)), close(std::move(closeCallback))
    {
        for (auto* button : {&searchTab, &shortcutTab, &macroTab, &closeButton, &run, &capture, &assign,
                            &cancelKey, &clearKey, &resetKey, &resetAll, &newMacro, &deleteMacro, &saveMacro,
                            &runMacro, &bindMacro, &discardMacro, &addStep, &updateStep, &removeStep, &upStep, &downStep}) {
            panel.addAndMakeVisible(button); button->setWantsKeyboardFocus(false);
        }
        for (auto* component : std::initializer_list<juce::Component*>{&query, &results, &currentKey, &status,
                &parametersView, &macroChoice, &stepChoice, &macroName, &steps, &stepParametersView})
            panel.addAndMakeVisible(component);
        parametersView.setViewedComponent(&parameters, false);
        stepParametersView.setViewedComponent(&stepParameters, false);
        parameters.setComponentID("command.parameters"); stepParameters.setComponentID("macro.parameters");
        results.setRowHeight(45); steps.setRowHeight(43);
        results.setOutlineThickness(1); steps.setOutlineThickness(1);
        query.setTextToShowWhenEmpty("Search commands or macros...", juce::Colours::grey);
        query.setComponentID("command.search"); results.setComponentID("command.results");
        run.setComponentID("command.run"); capture.setComponentID("shortcut.capture");
        assign.setComponentID("shortcut.assign"); cancelKey.setComponentID("shortcut.cancel");
        clearKey.setComponentID("shortcut.clear"); resetKey.setComponentID("shortcut.default");
        resetAll.setComponentID("shortcut.resetAll");
        macroChoice.setComponentID("macro.choice"); macroName.setComponentID("macro.name");
        newMacro.setComponentID("macro.new"); deleteMacro.setComponentID("macro.delete");
        saveMacro.setComponentID("macro.save"); runMacro.setComponentID("macro.run");
        bindMacro.setComponentID("macro.shortcut"); discardMacro.setComponentID("macro.discard");
        stepChoice.setComponentID("macro.command"); addStep.setComponentID("macro.addStep");
        updateStep.setComponentID("macro.updateStep"); removeStep.setComponentID("macro.removeStep");
        upStep.setComponentID("macro.up"); downStep.setComponentID("macro.down"); steps.setComponentID("macro.steps");
        status.setComponentID("command.status"); status.setJustificationType(juce::Justification::centredLeft);
        searchTab.onClick = [this] { show(Page::search); };
        shortcutTab.onClick = [this] { show(Page::shortcuts); };
        macroTab.onClick = [this] { show(Page::macros); };
        closeButton.onClick = [this] { dismiss(); };
        query.onTextChange = [this] { refreshResults(); };
        query.addKeyListener(this); results.addKeyListener(this);
        run.onClick = [this] { executeSelected(); };
        parameters.onSubmit = [this] { executeSelected(); };
        parameters.onEscape = stepParameters.onEscape = [this] { dismiss(); };
        capture.onClick = [this] {
            if (current() == nullptr) return;
            capturing = true; haveKey = false; capture.setButtonText("Press a key...");
            status.setText("Press a shortcut. Escape cancels.", juce::dontSendNotification);
            panel.grabKeyboardFocus(); showKeyControls();
        };
        cancelKey.onClick = [this] { cancelCapture(); };
        assign.onClick = [this] { assignKey(); };
        clearKey.onClick = [this] {
            if (const auto* entry = current()) { dispatcher.unbind(entry->id); persist("Shortcut cleared."); refreshSelected(); results.repaint(); }
        };
        resetKey.onClick = [this] {
            if (const auto* entry = current()) {
                const auto result = dispatcher.resetBinding(entry->id);
                if (!result.ok) { message(result); return; }
                persist("Default shortcut restored."); refreshSelected(); results.repaint();
            }
        };
        resetAll.onClick = [this] { dispatcher.resetAllBindings(); persist("Default shortcuts restored."); refreshSelected(); results.repaint(); };
        for (const auto& command : dispatcher.registry().commands()) if (command.macroAllowed) {
            stepCommands.push_back(command.id); stepChoice.addItem(juce::String(command.name), static_cast<int>(stepCommands.size()));
        }
        stepChoice.onChange = [this] { loadStepParameters(); };
        macroChoice.onChange = [this] {
            if (loadingMacro) return;
            if (macroDirty) {
                macroChoice.setSelectedId(selectedMacro, juce::dontSendNotification);
                status.setText("Save or discard this macro first.", juce::dontSendNotification); return;
            }
            loadMacro(macroChoice.getSelectedId());
        };
        macroName.setTextToShowWhenEmpty("Macro name", juce::Colours::grey);
        macroName.onTextChange = [this] { if (!loadingMacro) macroDirty = true; };
        macroName.onEscapeKey = [this] { dismiss(); };
        newMacro.onClick = [this] {
            if (macroDirty) { status.setText("Save or discard this macro first.", juce::dontSendNotification); return; }
            draft = {}; draft.id = "macro." + juce::Uuid().toString().toStdString();
            selectedMacro = 0; loadingMacro = true; macroChoice.setSelectedId(0, juce::dontSendNotification);
            macroName.clear(); loadingMacro = false; macroDirty = true;
            steps.updateContent(); steps.deselectAllRows(); macroName.grabKeyboardFocus();
            status.setText("Name the macro, then add steps.", juce::dontSendNotification); updateMacroButtons();
        };
        saveMacro.onClick = [this] { storeMacro(); };
        discardMacro.onClick = [this] { macroDirty = false; loadMacro(selectedMacro); status.setText("Draft discarded.", juce::dontSendNotification); };
        deleteMacro.onClick = [this] {
            if (draft.id.empty()) return;
            dispatcher.removeMacro(draft.id); macroDirty = false; persist("Macro deleted.");
            refreshMacros(); loadMacro(0); refreshResults();
        };
        runMacro.onClick = [this] {
            if (macroDirty) { status.setText("Save this macro before running it.", juce::dontSendNotification); return; }
            message(dispatcher.execute({draft.id, {}}), "Macro completed.");
        };
        bindMacro.onClick = [this] {
            if (macroDirty) { status.setText("Save this macro before assigning a shortcut.", juce::dontSendNotification); return; }
            const auto id = draft.id; query.setText({}, false); show(Page::shortcuts);
            for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].id == id) results.selectRow(static_cast<int>(i));
        };
        addStep.onClick = [this] { editStep(false); }; updateStep.onClick = [this] { editStep(true); };
        removeStep.onClick = [this] {
            const auto row = steps.getSelectedRow();
            if (row >= 0 && row < static_cast<int>(draft.steps.size())) {
                draft.steps.erase(draft.steps.begin() + row); macroDirty = true; steps.updateContent();
                steps.selectRow(juce::jmin(row, static_cast<int>(draft.steps.size()) - 1)); updateMacroButtons();
            }
        };
        upStep.onClick = [this] { moveStep(-1); }; downStep.onClick = [this] { moveStep(1); };
        refreshMacros(); if (!macroIds.empty()) loadMacro(1);
        if (!stepCommands.empty()) stepChoice.setSelectedId(1);
        show(Page::search);
    }
    ~Impl() override
    {
        query.removeKeyListener(this); results.removeKeyListener(this);
        results.setModel(nullptr); steps.setModel(nullptr);
        parametersView.setViewedComponent(nullptr); stepParametersView.setViewedComponent(nullptr);
    }
    void message(const CommandResult& result, const juce::String& success = {})
    { status.setText(result.ok ? success : juce::String(result.reason), juce::dontSendNotification); }
    bool persist(const juce::String& success)
    {
        auto result = save ? save() : juce::Result::ok();
        status.setText(result.wasOk() ? success : result.getErrorMessage(), juce::dontSendNotification);
        return result.wasOk();
    }
    void dismiss()
    {
        if (macroDirty) { show(Page::macros); status.setText("Save or discard this macro before closing.", juce::dontSendNotification); return; }
        auto callback = close; if (callback) callback();
    }
    const Entry* current() const
    {
        const auto row = results.getSelectedRow();
        return row >= 0 && row < static_cast<int>(entries.size()) ? &entries[static_cast<std::size_t>(row)] : nullptr;
    }
    void refreshResults()
    {
        const auto previous = current() ? current()->id : std::string();
        entries.clear();
        const auto text = query.getText().trim();
        for (auto* command : dispatcher.registry().search(text.toStdString(), 512))
            entries.push_back({command->id, command->name, command->category, false});
        for (const auto& macro : dispatcher.configuration().macros)
            if (text.isEmpty() || juce::String(macro.name).containsIgnoreCase(text) || juce::String(macro.id).containsIgnoreCase(text))
                entries.push_back({macro.id, macro.name, "Macro", true});
        results.updateContent();
        int selected = entries.empty() ? -1 : 0;
        for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].id == previous) selected = static_cast<int>(i);
        results.selectRow(selected); refreshSelected();
        if (entries.empty()) status.setText("No matching command. Try another name.", juce::dontSendNotification);
    }
    void refreshSelected()
    {
        cancelCapture(false);
        const auto* entry = current();
        run.setEnabled(entry != nullptr); capture.setEnabled(entry != nullptr);
        clearKey.setEnabled(entry != nullptr); resetKey.setEnabled(entry != nullptr && !entry->macro);
        if (entry != nullptr) {
            currentKey.setText("Current: " + (bindingLabel(dispatcher, entry->id).isEmpty() ? juce::String("Unassigned") : bindingLabel(dispatcher, entry->id)), juce::dontSendNotification);
            parameters.set(dispatcher.registry().find(entry->id), dispatcher.argumentsFor(entry->id));
            const auto availability = dispatcher.check({entry->id, {}});
            status.setText(availability.ok ? juce::String() : juce::String(availability.reason), juce::dontSendNotification);
        } else { parameters.set(nullptr, {}); currentKey.setText({}, juce::dontSendNotification); }
        panel.resized(); updateColours();
    }
    void executeSelected()
    {
        const auto* entry = current(); if (entry == nullptr) return;
        const auto id = entry->id; const bool isMacro = entry->macro;
        const auto arguments = parameters.values();
        if (!isMacro) {
            auto result = dispatcher.setArguments(id, arguments);
            if (!result.ok) { message(result); return; }
        }
        const auto result = dispatcher.execute({id, isMacro ? CommandArguments{} : arguments});
        if (!result.ok) { message(result); return; }
        if (!isMacro && !arguments.empty() && !persist("Command completed.")) return;
        dismiss();
    }
    void show(Page target)
    {
        cancelCapture(false); page = target;
        const bool search = page == Page::search, shortcuts = page == Page::shortcuts, macros = page == Page::macros;
        searchTab.setToggleState(search, juce::dontSendNotification);
        shortcutTab.setToggleState(shortcuts, juce::dontSendNotification);
        macroTab.setToggleState(macros, juce::dontSendNotification);
        for (auto* component : std::initializer_list<juce::Component*>{&query, &results}) component->setVisible(!macros);
        for (auto* component : std::initializer_list<juce::Component*>{&run, &parametersView}) component->setVisible(search);
        for (auto* component : std::initializer_list<juce::Component*>{&capture, &assign, &cancelKey, &clearKey, &resetKey, &resetAll, &currentKey}) component->setVisible(shortcuts);
        for (auto* component : std::initializer_list<juce::Component*>{&macroChoice, &macroName, &newMacro, &deleteMacro, &saveMacro, &runMacro, &bindMacro, &discardMacro,
                &stepChoice, &addStep, &updateStep, &removeStep, &upStep, &downStep, &steps, &stepParametersView}) component->setVisible(macros);
        if (!macros) refreshResults(); else updateMacroButtons();
        showKeyControls(); panel.resized();
    }
    int getNumRows() override { return static_cast<int>(entries.size()); }
    void paintRowBackground(juce::Graphics& graphics, int width, int height, bool selected)
    {
        auto& look = panel.getLookAndFeel();
        if (selected) { graphics.setColour(look.findColour(juce::PopupMenu::highlightedBackgroundColourId)); graphics.fillRect(0, 0, width, height); }
        graphics.setColour(look.findColour(selected ? juce::PopupMenu::highlightedTextColourId : juce::PopupMenu::textColourId));
    }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& entry = entries[static_cast<std::size_t>(row)];
        paintRowBackground(graphics, width, height, selected);
        const auto keys = bindingLabel(dispatcher, entry.id);
        graphics.setFont(juce::Font(14.0f));
        graphics.drawText(juce::String(entry.name), 9, 2, width - 185, 23, juce::Justification::centredLeft);
        graphics.setFont(juce::Font(12.0f));
        graphics.drawText(keys, juce::jmax(10, width - 177), 3, 166, 22, juce::Justification::centredRight);
        graphics.setOpacity(.65f);
        graphics.drawText(juce::String(entry.category), 9, 24, width - 18, 17, juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int) override { refreshSelected(); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { if (page == Page::search) executeSelected(); }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return handleKey(key); }
    bool handleKey(const juce::KeyPress& key)
    {
        if (capturing) {
            if (key.getKeyCode() == juce::KeyPress::escapeKey) { cancelCapture(); return true; }
            candidate = chordFromKeyPress(key);
            if (!validKeyChord(candidate)) { status.setText("Choose a key with optional Ctrl, Shift or Alt.", juce::dontSendNotification); return true; }
            capturing = false; haveKey = true; capture.setButtonText("Record shortcut");
            const auto* entry = current();
            const auto conflicts = dispatcher.conflicts(candidate, entry ? entry->id : std::string());
            juce::StringArray names; for (const auto& conflict : conflicts) names.add(juce::String(conflict.name));
            assign.setButtonText(conflicts.empty() ? "Assign" : "Replace");
            status.setText(juce::String(keyChordText(candidate)) + (conflicts.empty() ? " is available." : " is used by: " + names.joinIntoString(", ") + ". Replace or cancel."), juce::dontSendNotification);
            showKeyControls(); return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) {
            if (haveKey) cancelCapture(); else dismiss(); return true;
        }
        if (page != Page::macros && (key.getKeyCode() == juce::KeyPress::downKey || key.getKeyCode() == juce::KeyPress::upKey)) {
            if (textInputHasFocus(&panel) && juce::Component::getCurrentlyFocusedComponent() != &query) return false;
            results.selectRow(juce::jlimit(0, juce::jmax(0, getNumRows() - 1), results.getSelectedRow() + (key.getKeyCode() == juce::KeyPress::downKey ? 1 : -1))); return true;
        }
        if (key.getKeyCode() == juce::KeyPress::returnKey && page == Page::search) { executeSelected(); return true; }
        return false;
    }
    void showKeyControls()
    {
        assign.setVisible(page == Page::shortcuts && haveKey);
        cancelKey.setVisible(page == Page::shortcuts && (capturing || haveKey));
        capture.setEnabled(current() != nullptr && !capturing);
    }
    void cancelCapture(bool clearMessage = true)
    {
        capturing = haveKey = false; capture.setButtonText("Record shortcut");
        if (clearMessage) status.setText({}, juce::dontSendNotification);
        showKeyControls();
    }
    void assignKey()
    {
        const auto* entry = current(); if (entry == nullptr || !haveKey) return;
        const auto result = dispatcher.assign(entry->id, candidate, true);
        if (!result.ok) { message(result); return; }
        cancelCapture(false); refreshSelected(); persist("Shortcut saved."); results.repaint();
    }
    void refreshMacros()
    {
        macroChoice.clear(juce::dontSendNotification); macroIds.clear();
        for (const auto& macro : dispatcher.configuration().macros) {
            macroIds.push_back(macro.id); macroChoice.addItem(juce::String(macro.name), static_cast<int>(macroIds.size()));
        }
        macroChoice.setTextWhenNothingSelected("Choose a macro");
    }
    void loadMacro(int selected)
    {
        loadingMacro = true; draft = {}; selectedMacro = selected;
        if (selected > 0 && selected <= static_cast<int>(macroIds.size()))
            if (const auto* macro = dispatcher.findMacro(macroIds[static_cast<std::size_t>(selected - 1)])) draft = *macro;
        macroChoice.setSelectedId(selected, juce::dontSendNotification);
        macroName.setText(juce::String(draft.name), false); loadingMacro = false; macroDirty = false;
        steps.updateContent(); steps.selectRow(draft.steps.empty() ? -1 : 0); updateMacroButtons();
    }
    void updateMacroButtons()
    {
        const bool exists = !draft.id.empty();
        macroName.setEnabled(exists); saveMacro.setEnabled(exists); discardMacro.setEnabled(exists);
        deleteMacro.setEnabled(exists && dispatcher.findMacro(draft.id) != nullptr);
        runMacro.setEnabled(exists && !draft.steps.empty()); bindMacro.setEnabled(exists);
        addStep.setEnabled(exists && draft.steps.size() < maximumMacroSteps && !stepCommands.empty());
        const auto row = steps.getSelectedRow(); const bool selected = row >= 0 && row < static_cast<int>(draft.steps.size());
        updateStep.setEnabled(selected); removeStep.setEnabled(selected);
        upStep.setEnabled(selected && row > 0); downStep.setEnabled(selected && row + 1 < static_cast<int>(draft.steps.size()));
    }
    void loadStepParameters(const CommandArguments& values = {})
    {
        const auto id = stepChoice.getSelectedId();
        const auto* spec = id > 0 && id <= static_cast<int>(stepCommands.size()) ? dispatcher.registry().find(stepCommands[static_cast<std::size_t>(id - 1)]) : nullptr;
        stepParameters.set(spec, values); panel.resized(); updateColours();
    }
    void selectStep(int row)
    {
        if (row >= 0 && row < static_cast<int>(draft.steps.size())) {
            const auto& step = draft.steps[static_cast<std::size_t>(row)];
            const auto found = std::find(stepCommands.begin(), stepCommands.end(), step.id);
            stepChoice.setSelectedId(found == stepCommands.end() ? 0 : static_cast<int>(found - stepCommands.begin()) + 1, juce::dontSendNotification);
            loadStepParameters(step.arguments);
        }
        updateMacroButtons();
    }
    void editStep(bool replacing)
    {
        const int id = stepChoice.getSelectedId(), row = steps.getSelectedRow();
        if (draft.id.empty() || id <= 0 || id > static_cast<int>(stepCommands.size())) return;
        CommandInvocation step{stepCommands[static_cast<std::size_t>(id - 1)], stepParameters.values()};
        auto result = dispatcher.registry().validate(step); if (!result.ok) { message(result); return; }
        if (replacing) {
            if (row < 0 || row >= static_cast<int>(draft.steps.size())) return;
            draft.steps[static_cast<std::size_t>(row)] = std::move(step);
        } else {
            if (draft.steps.size() >= maximumMacroSteps) { status.setText("A macro can contain up to 32 steps.", juce::dontSendNotification); return; }
            draft.steps.push_back(std::move(step));
        }
        macroDirty = true; steps.updateContent(); steps.selectRow(replacing ? row : static_cast<int>(draft.steps.size()) - 1);
        status.setText("Step saved to draft. Save the macro when ready.", juce::dontSendNotification); updateMacroButtons();
    }
    void moveStep(int delta)
    {
        const auto row = steps.getSelectedRow(), target = row + delta;
        if (row < 0 || target < 0 || target >= static_cast<int>(draft.steps.size())) return;
        std::swap(draft.steps[static_cast<std::size_t>(row)], draft.steps[static_cast<std::size_t>(target)]);
        macroDirty = true; steps.updateContent(); steps.selectRow(target); updateMacroButtons();
    }
    void storeMacro()
    {
        draft.name = macroName.getText().trim().toStdString();
        const auto result = dispatcher.upsertMacro(draft); if (!result.ok) { message(result); return; }
        macroDirty = false; const auto id = draft.id; refreshMacros();
        const auto found = std::find(macroIds.begin(), macroIds.end(), id);
        loadMacro(found == macroIds.end() ? 0 : static_cast<int>(found - macroIds.begin()) + 1);
        persist("Macro saved.");
    }
    void layout()
    {
        auto area = panel.getLocalBounds().reduced(14);
        auto tabs = area.removeFromTop(31);
        searchTab.setBounds(tabs.removeFromLeft(110)); tabs.removeFromLeft(6);
        shortcutTab.setBounds(tabs.removeFromLeft(110)); tabs.removeFromLeft(6);
        macroTab.setBounds(tabs.removeFromLeft(100)); closeButton.setBounds(tabs.removeFromRight(74));
        area.removeFromTop(10); status.setBounds(area.removeFromBottom(39)); area.removeFromBottom(5);
        if (page == Page::macros) {
            auto top = area.removeFromTop(29); newMacro.setBounds(top.removeFromRight(64)); top.removeFromRight(6);
            deleteMacro.setBounds(top.removeFromRight(66)); top.removeFromRight(8); macroChoice.setBounds(top);
            area.removeFromTop(7); macroName.setBounds(area.removeFromTop(29)); area.removeFromTop(9);
            auto footer = area.removeFromBottom(30);
            saveMacro.setBounds(footer.removeFromLeft(72)); footer.removeFromLeft(6); runMacro.setBounds(footer.removeFromLeft(72));
            footer.removeFromLeft(6); bindMacro.setBounds(footer.removeFromLeft(90)); discardMacro.setBounds(footer.removeFromRight(84));
            area.removeFromBottom(8);
            auto left = area.removeFromLeft(juce::jmax(170, area.getWidth() * 44 / 100)); area.removeFromLeft(12);
            auto ordering = left.removeFromBottom(29); upStep.setBounds(ordering.removeFromLeft(47)); ordering.removeFromLeft(4);
            downStep.setBounds(ordering.removeFromLeft(57)); removeStep.setBounds(ordering.removeFromRight(73));
            left.removeFromBottom(6); steps.setBounds(left);
            stepChoice.setBounds(area.removeFromTop(29)); area.removeFromTop(8);
            auto buttons = area.removeFromBottom(29); addStep.setBounds(buttons.removeFromLeft(92)); buttons.removeFromLeft(5); updateStep.setBounds(buttons);
            area.removeFromBottom(7); stepParametersView.setBounds(area);
            stepParameters.setContentWidth(juce::jmax(1, area.getWidth() - stepParametersView.getScrollBarThickness()));
        } else {
            query.setBounds(area.removeFromTop(31)); area.removeFromTop(8);
            if (page == Page::search) {
                auto buttons = area.removeFromBottom(30); run.setBounds(buttons.removeFromRight(94)); area.removeFromBottom(7);
                const int parameterHeight = juce::jmin(parameters.contentHeight(), juce::jmax(0, area.getHeight() / 2));
                parametersView.setBounds(area.removeFromBottom(parameterHeight));
                parameters.setContentWidth(juce::jmax(1, parametersView.getWidth() - parametersView.getScrollBarThickness()));
                if (parameterHeight > 0) area.removeFromBottom(8);
            } else {
                const bool narrow = area.getWidth() < 650;
                auto buttons = area.removeFromBottom(30);
                auto defaults = narrow ? buttons : juce::Rectangle<int>();
                if (narrow) { area.removeFromBottom(5); buttons = area.removeFromBottom(30); }
                capture.setBounds(buttons.removeFromLeft(150)); buttons.removeFromLeft(6);
                assign.setBounds(buttons.removeFromLeft(78)); buttons.removeFromLeft(5); cancelKey.setBounds(buttons.removeFromLeft(70));
                if (!narrow) defaults = buttons;
                resetAll.setBounds(defaults.removeFromRight(86)); defaults.removeFromRight(5);
                resetKey.setBounds(defaults.removeFromRight(73)); defaults.removeFromRight(5); clearKey.setBounds(defaults.removeFromRight(57));
                area.removeFromBottom(5); currentKey.setBounds(area.removeFromBottom(28)); area.removeFromBottom(6);
            }
            results.setBounds(area);
        }
    }
    void updateColours()
    {
        auto& look = panel.getLookAndFeel();
        const auto background = look.findColour(juce::PopupMenu::backgroundColourId);
        const auto foreground = look.findColour(juce::PopupMenu::textColourId);
        for (auto* list : {&results, &steps}) {
            list->setColour(juce::ListBox::backgroundColourId, background);
            list->setColour(juce::ListBox::textColourId, foreground);
            list->setColour(juce::ListBox::outlineColourId, foreground.withAlpha(.18f));
        }
        status.setColour(juce::Label::textColourId, foreground); currentKey.setColour(juce::Label::textColourId, foreground);
        // Scoped colours inherit each product's skin instead of JUCE's default
        // dark TextEditor palette leaking into a light plugin.
        std::function<void(juce::Component&)> apply = [&](juce::Component& component) {
            if (auto* editor = dynamic_cast<juce::TextEditor*>(&component)) {
                editor->setColour(juce::TextEditor::backgroundColourId, background);
                editor->setColour(juce::TextEditor::textColourId, foreground);
                editor->applyColourToAllText(foreground, true);
                editor->setColour(juce::TextEditor::outlineColourId, foreground.withAlpha(.2f));
            }
            if (auto* label = dynamic_cast<juce::Label*>(&component)) label->setColour(juce::Label::textColourId, foreground);
            if (auto* combo = dynamic_cast<juce::ComboBox*>(&component)) {
                combo->setColour(juce::ComboBox::backgroundColourId, background);
                combo->setColour(juce::ComboBox::textColourId, foreground);
                combo->setColour(juce::ComboBox::arrowColourId, foreground.withAlpha(.75f));
                combo->setColour(juce::ComboBox::outlineColourId, foreground.withAlpha(.2f));
            }
            for (auto* child : component.getChildren()) apply(*child);
        };
        apply(panel);
    }
};

CommandPanel::CommandPanel(CommandDispatcher& dispatcher, std::function<juce::Result()> save,
                           std::function<void()> close)
{
    setOpaque(true); setWantsKeyboardFocus(true); setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    impl_ = std::make_unique<Impl>(*this, dispatcher, std::move(save), std::move(close));
    setSize(720, 590); lookAndFeelChanged();
}
CommandPanel::~CommandPanel() = default;
void CommandPanel::showPage(Page page) { impl_->show(page); impl_->updateColours(); }
void CommandPanel::setSearchText(const juce::String& text) { impl_->query.setText(text); }
void CommandPanel::setNotice(const juce::String& notice) { impl_->status.setText(notice, juce::dontSendNotification); }
void CommandPanel::focusSearch() { if (impl_->page != Page::macros) impl_->query.grabKeyboardFocus(); }
void CommandPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(getLookAndFeel().findColour(juce::PopupMenu::backgroundColourId));
    graphics.setColour(getLookAndFeel().findColour(juce::PopupMenu::textColourId).withAlpha(.2f));
    graphics.drawRect(getLocalBounds(), 1);
}
void CommandPanel::resized() { if (impl_) impl_->layout(); }
void CommandPanel::lookAndFeelChanged() { if (impl_) impl_->updateColours(); }
bool CommandPanel::keyPressed(const juce::KeyPress& key) { return impl_->handleKey(key); }
} // namespace pianoroll
