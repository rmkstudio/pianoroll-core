#pragma once
#include "Interaction.h"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pianoroll {

// All command/configuration APIs belong to the UI thread. Nothing here is called
// by PlaybackEngine::render, a timer, or a worker thread.
struct KeyChord {
    std::string key; // ASCII character, Space, Enter, Escape, Up, F1 ... F24, etc.
    bool command = false, shift = false, alt = false;
    bool operator==(const KeyChord&) const noexcept;
    bool operator<(const KeyChord&) const noexcept;
};
KeyChord normalizeKeyChord(KeyChord);
bool validKeyChord(const KeyChord&) noexcept;
std::string keyChordText(const KeyChord&);
KeyChord keyChord(const ShortcutInput&);

using CommandArguments = std::map<std::string, std::string>;
// Same finite, full-string, classic-locale parser used by registry validation.
// Integer reads additionally require an integral value representable by int.
std::optional<double> parseCommandNumber(const std::string&);
std::optional<int> parseCommandInteger(const std::string&);
struct CommandInvocation {
    std::string id;
    CommandArguments arguments;
};
enum class ParameterType { integer, number, text, choice };
struct CommandParameter {
    std::string id, name, defaultValue;
    ParameterType type = ParameterType::integer;
    double minimum = -127, maximum = 127;
    std::vector<std::string> choices;
};
struct CommandSpec {
    std::string id, name, category, keywords;
    std::vector<KeyChord> defaults;
    std::vector<CommandParameter> parameters;
    bool macroAllowed = false;
};
struct CommandResult {
    bool ok = true;
    std::string reason;
    static CommandResult failure(std::string reason) { return {false, std::move(reason)}; }
};
class CommandRegistry {
public:
    CommandResult add(CommandSpec);
    const CommandSpec* find(const std::string& id) const noexcept;
    const std::vector<CommandSpec>& commands() const noexcept { return commands_; }
    // ASCII case-insensitive token match; UTF-8 Chinese keywords match exactly.
    // Registry keeps search text ready; only a user search performs this scan.
    std::vector<const CommandSpec*> search(const std::string& query, std::size_t limit = 100) const;
    // Fills defaults and canonicalises integer arguments to decimal ("1e2" -> "100").
    CommandResult validate(CommandInvocation&) const;
private:
    std::vector<CommandSpec> commands_;
    std::vector<std::string> searchText_;
};

struct Macro {
    std::string id; // stable user ID beginning "macro."
    std::string name;
    std::vector<CommandInvocation> steps;
};
struct BindingOverride {
    std::string commandId;
    std::vector<KeyChord> keys; // empty explicitly removes every default binding
};
struct CommandConfiguration {
    int schema = 1;
    std::vector<BindingOverride> bindings;
    std::vector<Macro> macros;
    std::map<std::string, CommandArguments> arguments; // remembered per-command UI settings
};
constexpr std::size_t maximumMacroSteps = 32;
constexpr std::size_t maximumMacros = 64;
constexpr std::size_t maximumBindings = 512;

struct KeyResolution {
    bool consumed = false;
    CommandInvocation invocation;
};
struct BindingConflict {
    KeyChord key;
    std::string commandId, name;
};

// The adapter owns an isolated working document for macros. preflightMacro must
// validate the ordered actions against a disposable copy, so "select all, move"
// succeeds even when the live selection starts empty. begin/apply/commit must
// keep partial edits off the document, clipboard, preview and undo stack.
// commitMacro publishes once and adds one undo entry; rollbackMacro discards.
// No async actions, file operations, undo/redo, or nested macros are allowed.
class CommandHost {
public:
    virtual ~CommandHost() = default;
    virtual CommandResult checkCommand(const CommandInvocation&) const = 0;
    virtual CommandResult executeCommand(const CommandInvocation&) = 0;
    virtual CommandResult preflightMacro(const Macro&) const = 0;
    virtual CommandResult beginMacro(const std::string& name) = 0;
    virtual CommandResult applyMacroStep(const CommandInvocation&) = 0;
    virtual CommandResult commitMacro() = 0;
    virtual void rollbackMacro() noexcept = 0;
};

class CommandDispatcher {
public:
    CommandDispatcher(const CommandRegistry&, CommandConfiguration&, CommandHost&);
    const CommandRegistry& registry() const noexcept { return registry_; }
    const CommandConfiguration& configuration() const noexcept { return configuration_; }
    std::vector<KeyChord> bindingsFor(const std::string& commandId) const;
    std::vector<BindingConflict> conflicts(const KeyChord&, const std::string& exceptId = {}) const;
    CommandResult assign(const std::string& commandId, KeyChord, bool replaceConflict = false);
    void unbind(const std::string& commandId);
    CommandResult resetBinding(const std::string& commandId, bool replaceConflict = false);
    void resetAllBindings();
    // Call once after loading persistent settings. Invalid settings are rejected
    // atomically, leaving the previous configuration intact.
    CommandResult replaceConfiguration(CommandConfiguration);
    CommandResult setArguments(const std::string& commandId, CommandArguments);
    CommandArguments argumentsFor(const std::string& commandId) const;
    CommandResult upsertMacro(Macro);
    void removeMacro(const std::string& id);
    const Macro* findMacro(const std::string& id) const noexcept;
    CommandResult validateMacro(Macro&) const; // structural; host does state checks
    KeyResolution resolveKey(KeyChord, bool textInputFocused = false) const;
    CommandResult check(CommandInvocation) const;
    CommandResult execute(CommandInvocation);
private:
    CommandResult validateConfiguration(CommandConfiguration&) const;
    bool known(const std::string&) const noexcept;
    void setBindings(const std::string&, std::vector<KeyChord>);
    const CommandRegistry& registry_;
    CommandConfiguration& configuration_;
    CommandHost& host_;
    bool executing_ = false;
};

// Existing piano-roll actions only. Adapters may omit unsupported actions before
// adding them to their registry, and add product-specific actions independently.
std::vector<CommandSpec> standardPianoRollCommands();
const char* commandId(EditorCommand) noexcept;
EditorCommand editorCommandForId(const std::string&) noexcept;

} // namespace pianoroll
