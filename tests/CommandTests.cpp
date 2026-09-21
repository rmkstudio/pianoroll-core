#include <PianoRollCore/Commands.h>
#include <PianoRollCore/MusicTools.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
using namespace pianoroll;
struct State { int pitch = 60, clipboard = -1; bool selected = false; };
struct Host : CommandHost {
    State live, staged; int undo = 0, calls = 0, commits = 0, rollbacks = 0;
    bool unavailable = false, failBegin = false, failCommit = false, failApply = false, throwApply = false;
    CommandDispatcher* dispatcher = nullptr;
    CommandResult checkCommand(const CommandInvocation&) const override {
        return unavailable ? CommandResult::failure("Busy") : CommandResult{};
    }
    static CommandResult apply(State& state, const CommandInvocation& invocation) {
        if (invocation.id == "edit.selectAll") state.selected = true;
        else if (invocation.id == "edit.deselect") state.selected = false;
        else if (invocation.id == "notes.transpose") {
            if (!state.selected) return CommandResult::failure("Select notes");
            const int next = state.pitch + std::stoi(invocation.arguments.at("semitones"));
            if (next < 0 || next > 127) return CommandResult::failure("Pitch out of range");
            state.pitch = next;
        } else if (invocation.id == "edit.copy") {
            if (!state.selected) return CommandResult::failure("Select notes");
            state.clipboard = state.pitch;
        }
        return {};
    }
    CommandResult executeCommand(const CommandInvocation& invocation) override {
        ++calls;
        if (invocation.id == "edit.cancel" && dispatcher)
            return dispatcher->execute({"edit.cancel", {}});
        return apply(live, invocation);
    }
    CommandResult preflightMacro(const Macro& macro) const override {
        if (unavailable) return CommandResult::failure("Busy");
        auto copy = live;
        for (const auto& step : macro.steps) { auto r = apply(copy, step); if (!r.ok) return r; }
        return {};
    }
    CommandResult beginMacro(const std::string&) override {
        staged = live;
        return failBegin ? CommandResult::failure("Begin failed") : CommandResult{};
    }
    CommandResult applyMacroStep(const CommandInvocation& invocation) override {
        if (throwApply) throw std::runtime_error("step");
        if (failApply && invocation.id == "notes.transpose") return CommandResult::failure("Step failed");
        return apply(staged, invocation);
    }
    CommandResult commitMacro() override {
        if (failCommit) return CommandResult::failure("Commit failed");
        live = staged; ++undo; ++commits; return {};
    }
    void rollbackMacro() noexcept override { staged = live; ++rollbacks; }
};
void commandTests() {
    CommandRegistry registry;
    for (auto command : standardPianoRollCommands()) require(registry.add(std::move(command)).ok, "standard registry must be internally valid");
    require(!registry.add(standardPianoRollCommands().front()).ok, "duplicate ID rejected");
    require(!registry.add({"bad key", "Bad", {}, {}, {}, {}, false}).ok, "invalid ID rejected");
    require(!registry.add({"bad.default", "Bad", {}, {}, {{"Q"}}, {}, false}).ok, "conflicting default rejected");
    require(registry.search("量化").size() == 2, "Chinese search");
    require(registry.search("QUANTIZE HALF").size() == 1, "case insensitive multi-token search");
    require(registry.search("不存在").empty(), "unmatched search empty");
    require(registry.search("", 3).size() == 3, "search limit");
    require(registry.search("", 0).empty(), "zero search limit");
    require(normalizeKeyChord({" return ", true}) == KeyChord{"Enter", true}, "key normalization");
    require(normalizeKeyChord({"q"}).key == "Q", "ASCII character case");
    require(validKeyChord({"F24"}) && !validKeyChord({"F25"}) && !validKeyChord({"F01"}), "bounded function keys");
    require(keyChordText({"q", true, true, true}) == "Ctrl+Alt+Shift+Q", "canonical chord display");
    require(keyChord({ShortcutKey::up, 0, {true, false, false}, false}) == KeyChord{"Up", true}, "legacy key input adapter");
    require(editorCommandForId(commandId(EditorCommand::copy)) == EditorCommand::copy, "stable legacy mapping");

    CommandConfiguration config; Host host; CommandDispatcher dispatcher(registry, config, host);
    require(dispatcher.resolveKey({"K", true}).invocation.id == "commands.search", "Ctrl K default");
    require(dispatcher.resolveKey({"Q"}).invocation.id == "notes.quantize", "Q default");
    require(dispatcher.resolveKey({"Q", false, false, true}).invocation.id == "notes.quantizeHalf", "Alt Q default");
    require(dispatcher.resolveKey({"Q", false, true}).invocation.id == "notes.restoreTiming", "Shift Q default");
    require(dispatcher.resolveKey({"M"}).invocation.id == "notes.mirror", "M default");
    require(!dispatcher.resolveKey({"Q"}, true).consumed, "text editor retains Q");
    require(!dispatcher.resolveKey({"K", true}, true).consumed, "text editor retains Ctrl K");
    require(!dispatcher.resolveKey({"M", true}).consumed, "modifiers exact");
    host.unavailable = true;
    const auto unavailable = dispatcher.resolveKey({"Q"});
    require(unavailable.consumed && !dispatcher.execute(unavailable.invocation).ok && host.calls == 0,
            "recognized unavailable key consumed without executing");
    host.unavailable = false;
    require(!dispatcher.assign("notes.mirror", {"Q"}).ok, "collision is explicit");
    require(dispatcher.assign("notes.mirror", {"Q"}, true).ok, "explicit key takeover");
    require(dispatcher.resolveKey({"Q"}).invocation.id == "notes.mirror", "takeover resolves new command");
    require(dispatcher.bindingsFor("notes.quantize").empty(), "old owner removed");
    require(!dispatcher.resetBinding("notes.quantize").ok, "reset cannot silently steal key");
    require(dispatcher.resetBinding("notes.quantize", true).ok, "explicit reset takeover");
    require(dispatcher.resolveKey({"Q"}).invocation.id == "notes.quantize", "restored default");
    dispatcher.unbind("notes.mirror");
    require(!dispatcher.resolveKey({"M"}).consumed, "unbind suppresses default path");
    require(dispatcher.assign("notes.mirror", {"F8"}).ok, "function key assignment");
    require(dispatcher.resolveKey({"F8"}).invocation.id == "notes.mirror", "custom key executes");
    dispatcher.resetAllBindings();
    require(dispatcher.resolveKey({"M"}).consumed && !dispatcher.resolveKey({"F8"}).consumed, "restore all defaults");
    require(!dispatcher.assign("missing", {"F8"}).ok, "unknown binding target");
    auto invalid = config; invalid.schema = 2;
    require(!dispatcher.replaceConfiguration(invalid).ok && config.schema == 1, "schema reject atomic");
    invalid = config; invalid.bindings = {{"notes.mirror", {{"Q"}}}};
    require(!dispatcher.replaceConfiguration(invalid).ok && config.bindings.empty(), "persisted conflicts rejected atomically");
    invalid = config; invalid.bindings = {{"notes.mirror", {{"F8"}, {"F8"}}}};
    require(!dispatcher.replaceConfiguration(invalid).ok, "duplicate shortcut within command rejected");
    require(dispatcher.setArguments("notes.transpose", {{"semitones", "5"}}).ok, "remember command arguments");
    require(dispatcher.argumentsFor("notes.transpose").at("semitones") == "5", "argument recall");
    require(!dispatcher.setArguments("notes.transpose", {{"semitones", "NaN"}}).ok, "NaN parameter rejected");
    require(!dispatcher.setArguments("notes.transpose", {{"semitones", "128"}}).ok, "out of range parameter rejected");
    require(!dispatcher.setArguments("notes.transpose", {{"semitones", "1.5"}}).ok, "noninteger rejected");
    require(!dispatcher.setArguments("notes.transpose", {{"typo", "1"}}).ok, "unknown parameter rejected");
    require(dispatcher.argumentsFor("notes.transpose").at("semitones") == "5", "failed argument update unchanged");
    require(dispatcher.setArguments("notes.quantize", {{"grid", "0.5"}}).ok, "partial settings fill defaults");
    require(dispatcher.resolveKey({"Q"}).invocation.arguments.at("grid") == "0.5", "key uses remembered parameters");
    host.live.selected = true;
    require(dispatcher.execute({"notes.transpose", {}}).ok && host.live.pitch == 65, "execute uses remembered params");
    require(dispatcher.execute({"notes.transpose", {{"semitones", "-5"}}}).ok && host.live.pitch == 60, "explicit runtime params override remembered");

    Macro macro{"macro.stack", "Example", {{"edit.selectAll", {}}, {"notes.transpose", {}}, {"edit.copy", {}}}};
    require(dispatcher.upsertMacro(macro).ok, "macro saves declared defaults");
    require(dispatcher.findMacro(macro.id)->steps[1].arguments.at("semitones") == "12", "macro params independent of remembered command args");
    host.live.selected = false;
    require(dispatcher.check({macro.id, {}}).ok, "ordered preflight allows select before move/copy");
    require(dispatcher.execute({macro.id, {}}).ok, "macro executes");
    require(host.live.pitch == 72 && host.live.clipboard == 72 && host.undo == 1 && host.commits == 1, "macro updates once with one undo");
    require(dispatcher.assign(macro.id, {"F5"}).ok && dispatcher.resolveKey({"F5"}).invocation.id == macro.id, "macro shortcut");
    require(!dispatcher.execute({macro.id, {{"bad", "1"}}}).ok, "macro runtime parameters rejected");
    Macro nested{"macro.nested", "Nested", {{macro.id, {}}}};
    require(!dispatcher.upsertMacro(nested).ok, "nested macros rejected");
    for (const std::string forbidden : {"edit.undo", "edit.redo", "transport.playPause", "commands.search"}) {
        Macro m{"macro.bad", "Bad", {{forbidden, {}}}};
        require(!dispatcher.upsertMacro(m).ok, "nontransactional command forbidden in macro");
    }
    Macro tooLong = macro; tooLong.steps.resize(33, {"edit.selectAll", {}});
    require(!dispatcher.upsertMacro(tooLong).ok, "macro step bound");
    Macro empty{"macro.empty", "Empty", {}};
    require(!dispatcher.upsertMacro(empty).ok, "empty macro rejected");
    const auto originalPitch = host.live.pitch, originalClipboard = host.live.clipboard;
    Macro rangeFailure{"macro.range", "Range failure", {{"edit.selectAll", {}}, {"notes.transpose", {{"semitones", "127"}}}}};
    require(dispatcher.upsertMacro(rangeFailure).ok, "valid params but state invalid allowed to save");
    require(!dispatcher.execute({rangeFailure.id, {}}).ok && host.undo == 1 && host.live.pitch == originalPitch, "preflight failure publishes nothing");
    host.failApply = true;
    require(!dispatcher.execute({macro.id, {}}).ok && host.live.pitch == originalPitch && host.live.clipboard == originalClipboard && host.undo == 1,
            "mid-macro failure rolls back notes clipboard and undo");
    host.failApply = false; host.failCommit = true;
    require(!dispatcher.execute({macro.id, {}}).ok && host.live.pitch == originalPitch && host.undo == 1, "commit failure rollback");
    host.failCommit = false; host.throwApply = true;
    require(!dispatcher.execute({macro.id, {}}).ok && host.live.pitch == originalPitch && host.undo == 1, "exception rollback");
    host.throwApply = false; host.failBegin = true;
    require(!dispatcher.execute({macro.id, {}}).ok && host.live.pitch == originalPitch, "partly initialized transaction rollback");
    host.failBegin = false;
    require(host.rollbacks == 4, "every failed started transaction rolls back exactly once");
    host.dispatcher = &dispatcher;
    require(!dispatcher.execute({"edit.cancel", {}}).ok, "reentrant dispatch rejected");
    host.dispatcher = nullptr;
    dispatcher.removeMacro(macro.id);
    require(!dispatcher.findMacro(macro.id) && !dispatcher.resolveKey({"F5"}).consumed, "macro removal also removes binding");
}
void numericArgumentTests() {
    require(parseCommandNumber("1e2") == 100.0 && parseCommandInteger("1e2") == 100,
            "numeric readers agree on scientific notation");
    require(parseCommandInteger("12.0") == 12 && parseCommandInteger("-0") == 0,
            "integer-valued decimal and negative zero accepted");
    for (const auto* text : {"", "12tail", "1,5", "nan", "inf", "1e999"})
        require(!parseCommandNumber(text), "invalid numbers have no parsed value");
    require(!parseCommandInteger("1.5"), "integer reader rejects fractions");
    require(parseCommandInteger(std::to_string(std::numeric_limits<int>::min())) == std::numeric_limits<int>::min() &&
            parseCommandInteger(std::to_string(std::numeric_limits<int>::max())) == std::numeric_limits<int>::max(),
            "integer reader preserves representable endpoints");
    require(!parseCommandInteger(std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1)) &&
            !parseCommandInteger(std::to_string(static_cast<long long>(std::numeric_limits<int>::min()) - 1)),
            "integer reader rejects unrepresentable values");

    struct CommaDecimal : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    struct LocaleRestore { std::locale previous; ~LocaleRestore() { std::locale::global(previous); } } restore;
    std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    require(parseCommandNumber("1.5") == 1.5 && !parseCommandNumber("1,5"), "parsing independent of process locale");

    CommandRegistry registry;
    for (auto spec : standardPianoRollCommands()) registry.add(std::move(spec));
    CommandInvocation invocation{"notes.transpose", {{"semitones", "1e1"}}};
    require(registry.validate(invocation).ok && invocation.arguments.at("semitones") == "10",
            "registry delivers canonical decimal integers");
    CommandConfiguration config; Host host; host.live.selected = true;
    CommandDispatcher dispatcher(registry, config, host);
    // Host intentionally keeps stoi: existing adapters must also receive 10, not 1.
    require(dispatcher.execute({"notes.transpose", {{"semitones", "1e1"}}}).ok && host.live.pitch == 70,
            "legacy integer host receives the validated value");
    require(dispatcher.setArguments("notes.transpose", {{"semitones", "-1e1"}}).ok &&
            dispatcher.argumentsFor("notes.transpose").at("semitones") == "-10",
            "remembered arguments use canonical integers");
    require(dispatcher.execute({"notes.transpose", {}}).ok && host.live.pitch == 60,
            "remembered execution retains numeric meaning");
    Macro macro{"macro.numeric", "Numeric", {{"notes.transpose", {{"semitones", "1.2e1"}}}}};
    require(dispatcher.upsertMacro(macro).ok && dispatcher.execute({macro.id, {}}).ok && host.live.pitch == 72 && host.undo == 1,
            "macro validation and legacy execution agree");
    config.arguments["notes.transpose"] = {{"semitones", "+02.0"}};
    require(dispatcher.replaceConfiguration(config).ok && dispatcher.argumentsFor("notes.transpose").at("semitones") == "2",
            "existing configuration normalizes on load");
}
void musicTests() {
    ScaleDefinition scale;
    require(scale.contains(60) && scale.contains(64) && !scale.contains(61), "C major membership");
    scale.root = 2;
    require(scale.contains(61) && !scale.contains(60), "scale root transpose");
    scale.root = std::numeric_limits<int>::min();
    require(!scale.contains(-1), "invalid pitch rejected safely");
    const auto used = makePitchLayout(PitchVisibility::used, {72, 60, 60, -1, 128});
    require(used.pitches == std::vector<int>({60, 72}), "used pitch rows ordered and unique");
    require(used.rowForPitch(72) == 1 && used.rowForPitch(61) == -1, "folded pitch lookup");
    require(used.pitchForRow(-1) == 60 && used.pitchForRow(200) == 72, "folded hit test clamp");
    require(makePitchLayout(PitchVisibility::all, {}).pitches.size() == 128, "all pitch view");
    require(makePitchLayout(PitchVisibility::used, {}).pitches.size() == 25, "empty view permits note entry");
    const auto scaleRows = makePitchLayout(PitchVisibility::scale, {}, {});
    require(scaleRows.rowForPitch(60) >= 0 && scaleRows.rowForPitch(61) == -1, "scale fold membership");
    ArticulationMap articulations{{{"Legato", 24, -1}, {"Staccato", 24, 2}}};
    require(articulations.valid(), "explicit articulation mapping valid");
    require(articulations.find(24, 2)->name == "Staccato", "channel-specific articulation wins");
    require(articulations.find(24, 1)->name == "Legato" && !articulations.protects(25, 1), "unmapped low notes not inferred");
    articulations.entries.push_back({"Duplicate", 24, 2});
    require(!articulations.valid(), "duplicate articulation mapping rejected");
    StepInput input;
    require(!input.noteOn(60), "disarmed input does nothing");
    input.setEnabled(true); require(input.setLength(.25) && input.setPosition(4), "step setup");
    require(input.noteOn(60, 71, 1) && input.noteOn(64, 99, 1), "chord accumulates input velocity");
    require(!input.noteOn(60, 71, 1), "key repeats ignored");
    require(!input.setLength(.5) && !input.setPosition(0), "pending chord cannot move underneath held keys");
    require(input.noteOff(60, 1).empty() && input.position() == 4, "partial chord release no publish");
    auto chord = input.noteOff(64, 1);
    require(chord.notes.size() == 2 && chord.start == 4 && chord.length == .25 && input.position() == 4.25, "all releases publish once and advance");
    require(chord.notes[0].velocity == 71 && chord.notes[1].velocity == 99, "input dynamics preserved");
    require(input.noteOff(64, 1).empty() && input.position() == 4.25, "duplicate note off cannot advance");
    require(input.rest() && input.position() == 4.5 && input.back() && input.position() == 4.25, "rest and back");
    input.noteOn(67); input.setEnabled(false); input.setEnabled(true);
    require(input.noteOff(67).empty(), "disarm discards held chord");
    require(!input.setLength(0) && !input.setLength(std::numeric_limits<double>::quiet_NaN()), "invalid step length rejected");
    input.setPosition(0); require(input.back() && input.position() == 0, "back clamps at song start");
}
void quantizePreferenceTests() {
    CommandRegistry registry;
    for (auto spec : standardPianoRollCommands()) {
        const auto added = registry.add(std::move(spec));
        if (!added.ok) throw std::runtime_error(added.reason);
    }
    CommandConfiguration config; Host host; CommandDispatcher dispatcher(registry, config, host);
    require(dispatcher.setArguments("notes.quantize", {{"grid", ".5"}, {"division", "triplet"}, {"strength", ".25"}}).ok,
            "remember current Q grid and strength");
    const auto half = dispatcher.resolveKey({"Q", false, false, true}).invocation;
    require(half.arguments.at("grid") == ".5" && half.arguments.at("division") == "triplet" &&
            std::stod(half.arguments.at("strength")) == .5, "Alt Q uses Q template with half strength");
    require(dispatcher.setArguments("notes.quantizeHalf", {{"grid", "1"}, {"division", "quintuplet"}}).ok,
            "half quantize dialog updates the shared template");
    const auto full = dispatcher.argumentsFor("notes.quantize");
    require(full.at("grid") == "1" && full.at("division") == "quintuplet" && full.at("strength") == ".25" &&
            !config.arguments.count("notes.quantizeHalf"), "half settings preserve Q strength without a hidden second template");
    Macro macro{"macro.half", "Saved half quantize", {{"notes.quantizeHalf", {{"grid", ".125"}}}}};
    require(dispatcher.upsertMacro(macro).ok && dispatcher.findMacro(macro.id)->steps[0].arguments.at("grid") == ".125",
            "macro retains its explicitly saved half quantize grid");
    require(!dispatcher.setArguments("notes.quantizeHalf", {{"strength", "1"}}).ok,
            "half quantize cannot silently become full quantize");
}
}
int main(int argc, char**) {
    try {
        if (argc == 1) { commandTests(); numericArgumentTests(); musicTests(); }
        quantizePreferenceTests();
        std::cout << checks << " command/music checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "Check " << checks << " failed: " << e.what() << '\n'; return 1; }
}
