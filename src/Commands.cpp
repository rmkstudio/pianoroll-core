#include <PianoRollCore/Commands.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>

namespace pianoroll {
namespace {
std::string lower(std::string s) {
    for (auto& c : s) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? std::string{} : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
bool validId(const std::string& s) {
    return !s.empty() && s.size() <= 128 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}
CommandResult fail(const std::string& s) { return CommandResult::failure(s); }
bool forbiddenInMacro(const std::string& id) {
    return id == "edit.undo" || id == "edit.redo" || id.compare(0, 6, "macro.") == 0 ||
           id.compare(0, 5, "file.") == 0 || id.compare(0, 10, "transport.") == 0 ||
           id.compare(0, 9, "commands.") == 0;
}
CommandArguments withRemembered(const CommandConfiguration& config, CommandInvocation& invocation) {
    CommandArguments args;
    const auto found = config.arguments.find(invocation.id == "notes.quantizeHalf" ? "notes.quantize" : invocation.id);
    if (found != config.arguments.end()) args = found->second;
    if (invocation.id == "notes.quantizeHalf") {
        args["strength"] = ".5";
        args["endStrength"] = ".5";
    }
    for (const auto& item : invocation.arguments) args[item.first] = item.second;
    return args;
}
}

bool KeyChord::operator==(const KeyChord& o) const noexcept {
    return key == o.key && command == o.command && shift == o.shift && alt == o.alt;
}
bool KeyChord::operator<(const KeyChord& o) const noexcept {
    return std::tie(key, command, shift, alt) < std::tie(o.key, o.command, o.shift, o.alt);
}
KeyChord normalizeKeyChord(KeyChord key) {
    key.key = trim(key.key);
    const auto l = lower(key.key);
    const std::pair<const char*, const char*> names[] = {
        {"space", "Space"}, {"enter", "Enter"}, {"return", "Enter"}, {"escape", "Escape"},
        {"esc", "Escape"}, {"home", "Home"}, {"end", "End"}, {"backspace", "Backspace"},
        {"delete", "Delete"}, {"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
        {"pageup", "PageUp"}, {"pagedown", "PageDown"}, {"tab", "Tab"}, {"insert", "Insert"}
    };
    for (const auto& name : names) if (l == name.first) { key.key = name.second; return key; }
    if (key.key.size() == 1) key.key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key.key[0])));
    else if (l.size() >= 2 && l[0] == 'f') key.key[0] = 'F';
    return key;
}
bool validKeyChord(const KeyChord& c) noexcept {
    const auto& s = c.key;
    if (s.size() == 1) return static_cast<unsigned char>(s[0]) >= 33 && static_cast<unsigned char>(s[0]) <= 126;
    if (s == "Space" || s == "Enter" || s == "Escape" || s == "Home" || s == "End" ||
        s == "Backspace" || s == "Delete" || s == "Up" || s == "Down" || s == "Left" || s == "Right" ||
        s == "PageUp" || s == "PageDown" || s == "Tab" || s == "Insert") return true;
    if (s.size() < 2 || s.size() > 3 || s[0] != 'F' || s[1] < '1' || s[1] > '9') return false;
    int n = s[1] - '0';
    if (s.size() == 3) { if (s[2] < '0' || s[2] > '9') return false; n = n * 10 + s[2] - '0'; }
    return n <= 24;
}
std::string keyChordText(const KeyChord& input) {
    const auto c = normalizeKeyChord(input);
    return (c.command ? std::string("Ctrl+") : std::string{}) + (c.alt ? "Alt+" : "") +
           (c.shift ? "Shift+" : "") + c.key;
}
KeyChord keyChord(const ShortcutInput& i) {
    KeyChord result{"", i.modifiers.command, i.modifiers.shift, i.modifiers.alt};
    switch (i.key) {
        case ShortcutKey::character: result.key = std::string(1, i.character); break;
        case ShortcutKey::space: result.key = "Space"; break;
        case ShortcutKey::enter: result.key = "Enter"; break;
        case ShortcutKey::home: result.key = "Home"; break;
        case ShortcutKey::escape: result.key = "Escape"; break;
        case ShortcutKey::backspace: result.key = "Backspace"; break;
        case ShortcutKey::deleteKey: result.key = "Delete"; break;
        case ShortcutKey::up: result.key = "Up"; break;
        case ShortcutKey::down: result.key = "Down"; break;
        case ShortcutKey::left: result.key = "Left"; break;
        case ShortcutKey::right: result.key = "Right"; break;
        default: break;
    }
    return normalizeKeyChord(std::move(result));
}

std::optional<double> parseCommandNumber(const std::string& text) {
    std::istringstream parser(text);
    parser.imbue(std::locale::classic());
    double value = 0;
    parser >> value;
    if (parser.fail() || !parser.eof() || !std::isfinite(value)) return std::nullopt;
    return value;
}
std::optional<int> parseCommandInteger(const std::string& text) {
    const auto value = parseCommandNumber(text);
    if (!value || std::floor(*value) != *value ||
        *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(*value);
}

CommandResult CommandRegistry::add(CommandSpec spec) {
    if (!validId(spec.id) || spec.id.compare(0, 6, "macro.") == 0 || spec.name.empty() || spec.name.size() > 256)
        return fail("Invalid command ID or name");
    if (find(spec.id)) return fail("Duplicate command ID: " + spec.id);
    std::set<std::string> params;
    for (const auto& p : spec.parameters)
        if (!validId(p.id) || !params.insert(p.id).second || !std::isfinite(p.minimum) ||
            !std::isfinite(p.maximum) || p.minimum > p.maximum) return fail("Invalid parameter schema: " + spec.id);
    std::set<KeyChord> keys;
    for (auto& k : spec.defaults) {
        k = normalizeKeyChord(k);
        if (!validKeyChord(k) || !keys.insert(k).second) return fail("Invalid or duplicate default key: " + spec.id);
        for (const auto& old : commands_)
            if (std::find(old.defaults.begin(), old.defaults.end(), k) != old.defaults.end())
                return fail("Default key conflict with " + old.name);
    }
    searchText_.push_back(lower(spec.name + " " + spec.id + " " + spec.category + " " + spec.keywords));
    commands_.push_back(std::move(spec));
    CommandInvocation defaults{commands_.back().id, {}};
    auto result = validate(defaults);
    if (!result.ok) { commands_.pop_back(); searchText_.pop_back(); }
    return result;
}
const CommandSpec* CommandRegistry::find(const std::string& id) const noexcept {
    const auto it = std::find_if(commands_.begin(), commands_.end(), [&](const CommandSpec& c) { return c.id == id; });
    return it == commands_.end() ? nullptr : &*it;
}
std::vector<const CommandSpec*> CommandRegistry::search(const std::string& query, std::size_t limit) const {
    std::vector<const CommandSpec*> out;
    std::istringstream stream(lower(query)); std::vector<std::string> tokens; std::string token;
    while (stream >> token) tokens.push_back(token);
    for (std::size_t i = 0; i < commands_.size() && out.size() < limit; ++i) {
        if (std::all_of(tokens.begin(), tokens.end(), [&](const std::string& t) { return searchText_[i].find(t) != std::string::npos; }))
            out.push_back(&commands_[i]);
    }
    return out;
}
CommandResult CommandRegistry::validate(CommandInvocation& invocation) const {
    const auto* spec = find(invocation.id);
    if (!spec) return fail("Unknown command: " + invocation.id);
    if (invocation.arguments.size() > spec->parameters.size()) return fail("Unknown command parameter");
    for (const auto& arg : invocation.arguments)
        if (std::none_of(spec->parameters.begin(), spec->parameters.end(), [&](const CommandParameter& p) { return p.id == arg.first; }))
            return fail("Unknown parameter: " + arg.first);
    for (const auto& p : spec->parameters) {
        auto it = invocation.arguments.emplace(p.id, p.defaultValue).first;
        const auto& value = it->second;
        if (value.size() > 4096) return fail("Parameter is too long: " + p.name);
        if (p.type == ParameterType::choice) {
            if (std::find(p.choices.begin(), p.choices.end(), value) == p.choices.end()) return fail("Invalid choice: " + p.name);
        } else if (p.type != ParameterType::text) {
            const auto number = parseCommandNumber(value);
            if (!number || *number < p.minimum || *number > p.maximum ||
                (p.type == ParameterType::integer && std::floor(*number) != *number)) return fail("Invalid value: " + p.name);
            if (p.type == ParameterType::integer) {
                std::ostringstream decimal;
                decimal.imbue(std::locale::classic());
                decimal << std::fixed << std::setprecision(0) << (*number == 0 ? 0.0 : *number);
                it->second = decimal.str();
            }
        }
    }
    return {};
}

CommandDispatcher::CommandDispatcher(const CommandRegistry& r, CommandConfiguration& c, CommandHost& h)
    : registry_(r), configuration_(c), host_(h) {}
const Macro* CommandDispatcher::findMacro(const std::string& id) const noexcept {
    const auto& all = configuration_.macros;
    const auto it = std::find_if(all.begin(), all.end(), [&](const Macro& m) { return m.id == id; });
    return it == all.end() ? nullptr : &*it;
}
bool CommandDispatcher::known(const std::string& id) const noexcept { return registry_.find(id) || findMacro(id); }
std::vector<KeyChord> CommandDispatcher::bindingsFor(const std::string& id) const {
    for (const auto& b : configuration_.bindings) if (b.commandId == id) return b.keys;
    const auto* c = registry_.find(id); return c ? c->defaults : std::vector<KeyChord>{};
}
std::vector<BindingConflict> CommandDispatcher::conflicts(const KeyChord& input, const std::string& except) const {
    const auto key = normalizeKeyChord(input); std::vector<BindingConflict> out;
    const auto visit = [&](const std::string& id, const std::string& name) {
        if (id == except) return;
        for (const auto& bound : bindingsFor(id)) if (bound == key) { out.push_back({key, id, name}); break; }
    };
    for (const auto& c : registry_.commands()) visit(c.id, c.name);
    for (const auto& m : configuration_.macros) visit(m.id, m.name);
    return out;
}
void CommandDispatcher::setBindings(const std::string& id, std::vector<KeyChord> keys) {
    for (auto& b : configuration_.bindings) if (b.commandId == id) { b.keys = std::move(keys); return; }
    configuration_.bindings.push_back({id, std::move(keys)});
}
CommandResult CommandDispatcher::assign(const std::string& id, KeyChord key, bool replaceConflict) {
    key = normalizeKeyChord(std::move(key));
    if (!known(id)) return fail("Unknown command: " + id);
    if (!validKeyChord(key)) return fail("Unsupported key");
    const auto collision = conflicts(key, id);
    if (!replaceConflict && !collision.empty()) return fail("Key is assigned to " + collision.front().name);
    auto replacement = configuration_;
    for (const auto& c : collision) {
        auto keys = bindingsFor(c.commandId);
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        auto found = std::find_if(replacement.bindings.begin(), replacement.bindings.end(), [&](const BindingOverride& b) { return b.commandId == c.commandId; });
        if (found == replacement.bindings.end()) replacement.bindings.push_back({c.commandId, std::move(keys)});
        else found->keys = std::move(keys);
    }
    auto keys = bindingsFor(id);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    auto found = std::find_if(replacement.bindings.begin(), replacement.bindings.end(), [&](const BindingOverride& b) { return b.commandId == id; });
    if (found == replacement.bindings.end()) replacement.bindings.push_back({id, std::move(keys)});
    else found->keys = std::move(keys);
    return replaceConfiguration(std::move(replacement));
}
void CommandDispatcher::unbind(const std::string& id) { if (known(id)) setBindings(id, {}); }
CommandResult CommandDispatcher::resetBinding(const std::string& id, bool replaceConflict) {
    if (!known(id)) return fail("Unknown command: " + id);
    const auto old = configuration_;
    unbind(id);
    if (const auto* spec = registry_.find(id)) for (const auto& key : spec->defaults) {
        const auto r = assign(id, key, replaceConflict);
        if (!r.ok) { configuration_ = old; return r; }
    }
    configuration_.bindings.erase(std::remove_if(configuration_.bindings.begin(), configuration_.bindings.end(), [&](const BindingOverride& b) { return b.commandId == id; }), configuration_.bindings.end());
    return {};
}
void CommandDispatcher::resetAllBindings() { configuration_.bindings.clear(); }
CommandResult CommandDispatcher::validateMacro(Macro& m) const {
    m.name = trim(m.name);
    if (!validId(m.id) || m.id.compare(0, 6, "macro.") != 0 || m.id.size() <= 6 || m.name.empty() || m.name.size() > 256)
        return fail("A macro needs a valid ID and name");
    if (m.steps.empty() || m.steps.size() > maximumMacroSteps) return fail("A macro must contain 1 to 32 steps");
    for (auto& step : m.steps) {
        const auto* spec = registry_.find(step.id);
        if (!spec || !spec->macroAllowed || forbiddenInMacro(step.id)) return fail("Command cannot be used in a macro: " + step.id);
        const auto result = registry_.validate(step); if (!result.ok) return result;
    }
    return {};
}
CommandResult CommandDispatcher::validateConfiguration(CommandConfiguration& config) const {
    if (config.schema != 1 || config.macros.size() > maximumMacros || config.bindings.size() > maximumBindings || config.arguments.size() > registry_.commands().size())
        return fail("Unsupported or oversized command settings");
    std::set<std::string> ids;
    for (auto& macro : config.macros) {
        auto result = validateMacro(macro); if (!result.ok) return result;
        if (!ids.insert(macro.id).second) return fail("Duplicate macro ID");
    }
    std::map<std::string, std::vector<KeyChord>> effective;
    for (const auto& c : registry_.commands()) effective[c.id] = c.defaults;
    for (const auto& m : config.macros) effective[m.id] = {};
    ids.clear();
    for (auto& b : config.bindings) {
        if (!effective.count(b.commandId) || !ids.insert(b.commandId).second || b.keys.size() > 16) return fail("Unknown or duplicate binding target");
        std::set<KeyChord> perCommand;
        for (auto& k : b.keys) {
            k = normalizeKeyChord(k);
            if (!validKeyChord(k) || !perCommand.insert(k).second) return fail("Invalid or duplicate shortcut");
        }
        effective[b.commandId] = b.keys;
    }
    std::map<KeyChord, std::string> used;
    for (const auto& item : effective) for (const auto& key : item.second)
        if (!used.emplace(key, item.first).second) return fail("Shortcut conflict: " + keyChordText(key));
    for (auto& item : config.arguments) {
        CommandInvocation invocation{item.first, item.second}; const auto result = registry_.validate(invocation);
        if (!result.ok) return result;
        item.second = std::move(invocation.arguments);
    }
    // Half-quantize has one shared grid/options preference with Q. Old in-memory
    // drafts may have a separate entry: normalize it without overriding Q.
    auto half = config.arguments.find("notes.quantizeHalf");
    if (half != config.arguments.end()) {
        if (!config.arguments.count("notes.quantize")) {
            auto args = half->second; args.erase("strength"); args.erase("endStrength");
            CommandInvocation full{"notes.quantize", std::move(args)};
            const auto valid = registry_.validate(full); if (!valid.ok) return valid;
            config.arguments[full.id] = std::move(full.arguments);
        }
        config.arguments.erase(half);
    }
    return {};
}
CommandResult CommandDispatcher::replaceConfiguration(CommandConfiguration config) {
    auto result = validateConfiguration(config); if (result.ok) configuration_ = std::move(config); return result;
}
CommandResult CommandDispatcher::setArguments(const std::string& id, CommandArguments args) {
    CommandInvocation invocation{id, std::move(args)}; auto result = registry_.validate(invocation);
    if (result.ok) {
        if (id == "notes.quantizeHalf") {
            auto full = argumentsFor("notes.quantize");
            for (const auto& argument : invocation.arguments)
                if (argument.first != "strength" && argument.first != "endStrength") full[argument.first] = argument.second;
            configuration_.arguments["notes.quantize"] = std::move(full);
            configuration_.arguments.erase(id);
        } else configuration_.arguments[id] = std::move(invocation.arguments);
    }
    return result;
}
CommandArguments CommandDispatcher::argumentsFor(const std::string& id) const {
    CommandInvocation invocation{id, {}}; invocation.arguments = withRemembered(configuration_, invocation);
    registry_.validate(invocation); return invocation.arguments;
}
CommandResult CommandDispatcher::upsertMacro(Macro macro) {
    auto result = validateMacro(macro); if (!result.ok) return result;
    for (auto& m : configuration_.macros) if (m.id == macro.id) { m = std::move(macro); return {}; }
    if (configuration_.macros.size() >= maximumMacros) return fail("Too many macros (maximum 64)");
    configuration_.macros.push_back(std::move(macro)); return {};
}
void CommandDispatcher::removeMacro(const std::string& id) {
    configuration_.macros.erase(std::remove_if(configuration_.macros.begin(), configuration_.macros.end(), [&](const Macro& m) { return m.id == id; }), configuration_.macros.end());
    configuration_.bindings.erase(std::remove_if(configuration_.bindings.begin(), configuration_.bindings.end(), [&](const BindingOverride& b) { return b.commandId == id; }), configuration_.bindings.end());
}
KeyResolution CommandDispatcher::resolveKey(KeyChord input, bool textFocused) const {
    if (textFocused) return {};
    input = normalizeKeyChord(std::move(input)); if (!validKeyChord(input)) return {};
    for (const auto& c : registry_.commands()) for (const auto& k : bindingsFor(c.id)) if (k == input)
        return {true, {c.id, argumentsFor(c.id)}};
    for (const auto& m : configuration_.macros) for (const auto& k : bindingsFor(m.id)) if (k == input)
        return {true, {m.id, {}}};
    return {};
}
CommandResult CommandDispatcher::check(CommandInvocation invocation) const {
    if (executing_) return fail("A command is already running");
    try {
        if (const auto* macro = findMacro(invocation.id)) {
            if (!invocation.arguments.empty()) return fail("Macros do not accept runtime parameters");
            auto copy = *macro; const auto valid = validateMacro(copy);
            return valid.ok ? host_.preflightMacro(copy) : valid;
        }
        invocation.arguments = withRemembered(configuration_, invocation);
        const auto valid = registry_.validate(invocation); return valid.ok ? host_.checkCommand(invocation) : valid;
    } catch (...) { return fail("Command availability check failed"); }
}
CommandResult CommandDispatcher::execute(CommandInvocation invocation) {
    if (executing_) return fail("A command is already running");
    struct Guard { bool& flag; Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(executing_);
    bool macroStarted = false;
    try {
        if (const auto* macro = findMacro(invocation.id)) {
            if (!invocation.arguments.empty()) return fail("Macros do not accept runtime parameters");
            const auto name = macro->name; auto copy = *macro;
            auto result = validateMacro(copy); if (!result.ok) return result;
            result = host_.preflightMacro(copy); if (!result.ok) return result;
            // Set before begin so a partially initialized host also gets rollback.
            macroStarted = true; result = host_.beginMacro(name);
            if (result.ok) for (const auto& step : copy.steps) {
                result = host_.applyMacroStep(step); if (!result.ok) break;
            }
            if (result.ok) result = host_.commitMacro();
            if (!result.ok) host_.rollbackMacro();
            return result;
        }
        invocation.arguments = withRemembered(configuration_, invocation);
        auto result = registry_.validate(invocation); if (!result.ok) return result;
        result = host_.checkCommand(invocation); return result.ok ? host_.executeCommand(invocation) : result;
    } catch (...) {
        if (macroStarted) host_.rollbackMacro();
        return fail("Command failed; macro changes were discarded");
    }
}

const char* commandId(EditorCommand c) noexcept {
    switch (c) {
        case EditorCommand::escape: return "edit.cancel"; case EditorCommand::undo: return "edit.undo";
        case EditorCommand::redo: return "edit.redo"; case EditorCommand::deselect: return "edit.deselect";
        case EditorCommand::selectAll: return "edit.selectAll"; case EditorCommand::copy: return "edit.copy";
        case EditorCommand::cut: return "edit.cut"; case EditorCommand::paste: return "edit.paste";
        case EditorCommand::erase: return "edit.delete"; case EditorCommand::up: return "notes.up";
        case EditorCommand::down: return "notes.down"; case EditorCommand::left: return "notes.left";
        case EditorCommand::right: return "notes.right"; case EditorCommand::playPause: return "transport.playPause";
        case EditorCommand::home: return "transport.home"; case EditorCommand::pencil: return "tool.pencil";
        case EditorCommand::pointer: return "tool.pointer"; default: return "";
    }
}
EditorCommand editorCommandForId(const std::string& id) noexcept {
    for (int i = static_cast<int>(EditorCommand::escape); i <= static_cast<int>(EditorCommand::pointer); ++i)
        if (id == commandId(static_cast<EditorCommand>(i))) return static_cast<EditorCommand>(i);
    return EditorCommand::none;
}
std::vector<CommandSpec> standardPianoRollCommands() {
    std::vector<CommandSpec> out;
    const auto add = [&](const char* id, const char* name, const char* category, const char* keywords,
                         std::vector<KeyChord> keys = {}, bool macro = false, std::vector<CommandParameter> params = {}) {
        out.push_back({id, name, category, keywords, std::move(keys), std::move(params), macro});
    };
    add("commands.search", "Search commands / 搜索命令", "Commands", "find search 查找 搜索", {{"K", true}});
    add("commands.shortcuts", "Keyboard shortcuts / 快捷键", "Commands", "keys settings 快捷键 设置");
    add("commands.macros", "Macros / 宏", "Commands", "sequence batch 宏 批量");
    add("edit.cancel", "Cancel / 取消", "Edit", "escape 取消 退出", {{"Escape"}});
    add("edit.undo", "Undo / 撤销", "Edit", "undo 撤销", {{"Z", true}});
    add("edit.redo", "Redo / 重做", "Edit", "redo 重做", {{"Z", true, true}, {"Y", true}});
    add("edit.selectAll", "Select all / 全选", "Selection", "select all 选择 全选", {{"A", true}}, true);
    add("edit.deselect", "Deselect / 取消选择", "Selection", "clear selection 取消选择", {{"D", true}}, true);
    add("edit.copy", "Copy / 复制", "Edit", "copy clipboard 复制", {{"C", true}}, true);
    add("edit.cut", "Cut / 剪切", "Edit", "cut clipboard 剪切", {{"X", true}}, true);
    add("edit.paste", "Paste / 粘贴", "Edit", "paste clipboard 粘贴", {{"V", true}}, true);
    add("edit.delete", "Delete notes / 删除音符", "Edit", "delete erase 删除 音符", {{"Delete"}, {"Backspace"}}, true);
    add("notes.up", "Semitone up / 升半音", "Notes", "transpose pitch 升高 半音", {{"Up"}}, true);
    add("notes.down", "Semitone down / 降半音", "Notes", "transpose pitch 降低 半音", {{"Down"}}, true);
    add("notes.octaveUp", "Octave up / 升八度", "Notes", "transpose pitch octave 升高 八度", {{"Up", true}}, true);
    add("notes.octaveDown", "Octave down / 降八度", "Notes", "transpose pitch octave 降低 八度", {{"Down", true}}, true);
    add("notes.left", "Nudge left / 左移一格", "Notes", "move grid nudge 左移 网格", {{"Left"}}, true);
    add("notes.right", "Nudge right / 右移一格", "Notes", "move grid nudge 右移 网格", {{"Right"}}, true);
    add("notes.fineLeft", "Nudge left finely / 微量左移", "Notes", "move tick nudge 微调 左移", {{"Left", false, false, true}}, true);
    add("notes.fineRight", "Nudge right finely / 微量右移", "Notes", "move tick nudge 微调 右移", {{"Right", false, false, true}}, true);
    add("transport.playPause", "Play / Pause / 播放暂停", "Playback", "play pause transport 播放 暂停", {{"Space"}, {"Enter"}});
    add("transport.stop", "Stop / 停止", "Playback", "stop transport 停止");
    add("transport.home", "Return to start / 回到开头", "Playback", "home start 回到 开头", {{"Home"}});
    add("transport.clearLoop", "Clear loop / 清除循环", "Playback", "loop clear 循环 清除");
    add("transport.solo", "Toggle solo / 切换独奏", "Playback", "solo track voice 独奏 声部 轨道");
    add("view.follow", "Follow playhead / 跟随播放", "View", "follow cursor 跟随 播放");
    add("view.fit", "Fit notes / 适应音符", "View", "fit zoom notes 缩放 全览 适应");
    add("view.fitSelection", "Fit selection / 适应选区", "View", "fit zoom selection 缩放 选区");
    add("tool.pencil", "Draw tool / 画笔", "Tools", "pencil draw paint 画笔 绘制", {{"1"}});
    add("tool.pointer", "Select tool / 选择工具", "Tools", "pointer select 选择 工具", {{"2"}});
    const auto number = [](const char* id, const char* label, const char* value, double min, double max) {
        return CommandParameter{id, label, value, ParameterType::number, min, max, {}};
    };
    const auto integer = [](const char* id, const char* label, const char* value, double min, double max) {
        return CommandParameter{id, label, value, ParameterType::integer, min, max, {}};
    };
    const auto choice = [](const char* id, const char* label, const char* value, std::vector<std::string> choices) {
        return CommandParameter{id, label, value, ParameterType::choice, 0, 0, std::move(choices)};
    };
    const auto text = [](const char* id, const char* label, const char* value) {
        return CommandParameter{id, label, value, ParameterType::text, 0, 0, {}};
    };
    const auto boolean = [&](const char* id, const char* label, bool value) {
        return choice(id, label, value ? "true" : "false", {"false", "true"});
    };
    const std::vector<CommandParameter> grid{
        number("grid", "Grid (beats) / 网格（拍）", ".25", 1.0 / 16384, 128),
        choice("division", "Division / 连音", "straight", {"straight", "triplet", "quintuplet"}),
        number("swing", "Swing / 摇摆", "0", 0, 1)};
    auto quantize = grid;
    const std::vector<CommandParameter> quantizeExtra{
        boolean("starts", "Note starts / 音头", true), boolean("ends", "Note ends / 音尾", false),
        number("strength", "Start strength / 音头强度", "1", 0, 1),
        number("endStrength", "End strength / 音尾强度", "1", 0, 1),
        number("captureRange", "Capture distance (-1: all) / 距离", "-1", -1, 1.0e9),
        number("rangeStart", "Range start (-1: selection) / 起点", "-1", -1, 1.0e9),
        number("rangeEnd", "Range end (-1: selection) / 终点", "-1", -1, 1.0e9)};
    quantize.insert(quantize.end(), quantizeExtra.begin(), quantizeExtra.end());
    add("notes.quantize", "Quantize / 量化", "Notes", "quantize rhythm grid 量化 节奏 对齐", {{"Q"}}, true, quantize);
    auto halfQuantize = quantize;
    for (auto& p : halfQuantize) if (p.id == "strength" || p.id == "endStrength") {
        p.defaultValue = ".5"; p.minimum = .5; p.maximum = .5;
    }
    add("notes.quantizeHalf", "Quantize 50% / 半量化", "Notes", "quantize half 量化 一半", {{"Q", false, false, true}}, true, halfQuantize);
    add("notes.restoreTiming", "Restore timing / 恢复原始时机", "Notes", "restore original timing 恢复 原始 时机", {{"Q", false, true}}, true);
    add("notes.mirror", "Mirror notes / 镜像", "Notes", "mirror invert pitch retrograde 镜像 倒影 逆行", {{"M"}}, true, {
        choice("axis", "Axis / 方向", "pitch", {"pitch", "time", "both"}),
        choice("pivot", "Pivot / 镜像轴", "middle", {"first", "middle", "last", "custom"}),
        number("customPitch", "Custom pitch / 指定音高", "60", 0, 127),
        number("customTime", "Custom time (beats) / 指定位置", "0", 0, 1.0e9)});
    add("notes.reverse", "Reverse phrase / 逆行", "Notes", "reverse phrase time 逆行 时间 倒放", {}, true);
    add("notes.length", "Set note length / 统一时值", "Notes", "length duration 时值 长度", {}, true, {
        number("length", "Length (beats) / 长度（拍）", ".25", 1.0 / 16384, 1.0e9)});
    add("notes.duplicate", "Duplicate in place / 原位复制", "Notes", "duplicate clone in place 原位 复制 叠加", {}, true);
    add("notes.linkedResize", "Linked note edges / 联动音符边界", "Notes", "linked adjacent resize boundary 联动 相邻 边界", {}, false, {
        boolean("enabled", "Link adjacent edges / 联动相邻边界", true)});
    add("notes.alignEnds", "Align note ends / 对齐音尾", "Notes", "align end 尾音 结尾 对齐", {}, true, {
        number("target", "End (-1: latest selected) / 终点", "-1", -1, 1.0e9)});
    add("notes.scaleTime", "Scale phrase time / 乐句伸缩", "Notes", "scale stretch time duration 乐句 伸缩 压缩", {}, true, {
        number("factor", "Time factor / 时值倍率", ".5", .001, 1000),
        number("anchor", "Anchor (-1: selection start) / 起点", "-1", -1, 1.0e9)});
    add("notes.transpose", "Transpose / 移调", "Notes", "transpose octave pitch 移调 八度", {}, true, {
        integer("semitones", "Semitones / 半音数", "12", -127, 127)});
    add("notes.velocity", "Adjust velocity / 调整力度", "Notes", "velocity dynamics 力度 强弱", {}, true, {
        integer("delta", "Velocity change / 力度增减", "-10", -126, 126)});
    add("notes.select", "Select by condition / 条件选音", "Selection", "filter highest lowest pitch velocity 条件 最高 最低 音高 力度 时值 选择", {}, true, {
        choice("kind", "Condition / 条件", "highest", {"highest", "lowest", "pitch", "velocity", "length", "everyNth", "beat"}),
        integer("min", "Minimum / 最小", "0", 0, 127), integer("max", "Maximum / 最大", "127", 0, 127),
        number("minLength", "Minimum length (beats) / 最短时值", "0", 0, 1.0e9),
        number("maxLength", "Maximum length (beats) / 最长时值", "1000000000", 0, 1.0e9),
        integer("every", "Every N notes / 每几个音", "2", 1, 200000), integer("offset", "Offset / 偏移", "0", 0, 200000),
        number("period", "Beat period / 拍位周期", "1", 1.0 / 16384, 1.0e9),
        number("position", "Beat position / 拍位", "0", 0, 1.0e9),
        number("tolerance", "Tolerance (beats) / 容差", ".01", 0, 1.0e9),
        boolean("withinSelection", "Within selection / 仅选区内", false)});
    add("notes.split", "Split at grid / 按网格切音", "Notes", "split repeat grid 切分 网格 重复音", {}, true, grid);
    add("notes.repeat", "Repeat phrase / 重复乐句", "Notes", "repeat phrase fill ostinato 重复 乐句 填充", {{"D"}}, true, {
        number("end", "End (-1: loop or repeat count) / 终点", "-1", -1, 1.0e9),
        number("period", "Period (0: phrase length) / 周期", "0", 0, 1.0e9),
        integer("count", "Copies / 副本数", "1", 1, 128)});
    add("notes.strum", "Strum / 扫弦", "Notes", "strum chord spread onset 扫弦 和弦 错开", {}, true, {
        number("spacing", "Note spacing (beats) / 间隔", ".025", 0, 128),
        boolean("descending", "High to low / 从高到低", false), boolean("preserveEnds", "Keep note ends / 保持音尾", false)});
    const std::vector<CommandParameter> transform{
        number("offset", "Value offset / 整体抬升", "0", -127, 127),
        number("scale", "Value scale / 起伏倍率", "1", 0, 100),
        number("rampStart", "Start offset / 起点抬升", "0", -127, 127),
        number("rampEnd", "End offset / 终点抬升", "0", -127, 127),
        number("timeScale", "Time scale / 时间倍率", "1", .001, 1000)};
    add("velocity.transform", "Shape velocity / 力度塑形", "Dynamics", "velocity transform ramp crescendo 力度 塑形 渐强 渐弱", {}, true, transform);
    add("velocity.transformBox", "Velocity transform box / 力度变换框", "Dynamics", "velocity box handles 力度 变换框", {}, false);
    auto automation = transform;
    automation.insert(automation.end(), {
        choice("source", "Source (current track/channel) / 当前轨道通道", "current", {"current"}),
        text("controllers", "CC numbers / CC 编号", "selected"),
        number("start", "Start (-1: selected phrase) / 起点", "-1", -1, 1.0e9),
        number("end", "End (-1: selected phrase) / 终点", "-1", -1, 1.0e9)});
    add("automation.transform", "Shape CC / CC 塑形", "Dynamics", "cc controller automation transform CC 控制 自动化 塑形", {}, true, automation);
    add("automation.transformBox", "CC transform box / CC 变换框", "Dynamics", "cc automation box handles CC 变换框", {}, false);
    add("automation.follow.configure", "CC follows notes / CC 跟随音符", "Dynamics", "cc follow copy move CC 跟随 复制 移动", {}, false, {
        boolean("enabled", "Follow notes / 跟随音符", false),
        choice("source", "Source (current track/channel) / 当前轨道通道", "current", {"current"}),
        text("controllers", "CC numbers (comma separated) / CC 编号", "")});
    add("view.pitchVisibility", "Pitch visibility / 音高折叠", "View", "fold used pitch scale 音高 折叠 已用", {}, false, {
        choice("mode", "Visible pitches / 显示音高", "all", {"all", "used", "scale"})});
    add("view.scale", "Scale guide / 音阶提示", "View", "scale key root major minor 音阶 调性 提示", {}, false, {
        integer("root", "Root pitch class / 主音", "0", 0, 11),
        choice("scale", "Scale / 音阶", "major", {"major", "minor", "pentatonic", "chromatic", "custom"}),
        text("intervals", "Custom semitones / 自定义半音", "0,2,4,5,7,9,11"),
        boolean("highlight", "Highlight / 高亮", true)});
    add("articulation.configure", "Articulation mapping / 演奏法标记", "Music", "articulation keyswitch protect 演奏法 切换音 保护", {}, false, {
        choice("action", "Action / 操作", "add", {"add", "remove", "clear"}),
        integer("pitch", "Key-switch MIDI pitch / 切换音高", "24", 0, 127),
        text("name", "Articulation / 演奏法名称", "Legato"),
        choice("channel", "Channel (all: current track) / 通道", "all",
               {"all", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"})});
    add("step.configure", "Step input / 步进输入", "Music", "step input record notes 步进 输入 录入", {}, false, {
        boolean("enabled", "Enable / 开启", true), number("length", "Step length (beats) / 步长", ".25", 1.0 / 16384, 128),
        integer("velocity", "Velocity / 力度", "100", 1, 127),
        choice("velocityMode", "Velocity mode / 力度模式", "input", {"input", "fixed"}),
        number("gate", "Note length (%) / 发声长度", "90", 1, 100)});
    add("step.toggle", "Arm step input / 切换步进输入", "Music", "step arm toggle 步进 开关");
    add("step.rest", "Step rest / 步进休止", "Music", "step rest 步进 休止");
    add("step.back", "Step back / 步进后退", "Music", "step back 步进 后退");
    add("step.note", "Enter step note / 输入步进音符", "Music", "step enter note 步进 音符 输入", {}, false, {
        integer("pitch", "MIDI pitch / 音高", "60", 0, 127), integer("velocity", "Velocity / 力度", "100", 1, 127)});
    return out;
}
} // namespace pianoroll
