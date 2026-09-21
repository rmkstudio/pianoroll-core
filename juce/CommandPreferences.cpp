#include <PianoRollCore/CommandUI.h>

namespace pianoroll {
namespace {
juce::var object() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& target, const char* key, const juce::var& value)
{ target.getDynamicObject()->setProperty(key, value); }
juce::var argumentsToJson(const CommandArguments& arguments)
{
    auto result = object();
    for (const auto& item : arguments)
        result.getDynamicObject()->setProperty(juce::Identifier(item.first), juce::String(item.second));
    return result;
}
bool stringField(const juce::var& source, const char* name, std::string& result)
{
    const auto value = source[name];
    if (!value.isString() || value.toString().length() > 4096) return false;
    result = value.toString().toStdString();
    return true;
}
bool readArguments(const juce::var& source, CommandArguments& result)
{
    auto* data = source.getDynamicObject();
    if (data == nullptr || data->getProperties().size() > 64) return false;
    for (const auto& item : data->getProperties()) {
        if (!item.value.isString() || item.value.toString().length() > 4096) return false;
        result.emplace(item.name.toString().toStdString(), item.value.toString().toStdString());
    }
    return true;
}
juce::Result decode(const juce::var& root, CommandConfiguration& config)
{
    const auto invalid = [] { return juce::Result::fail("Invalid command settings. Original preserved."); };
    if (root.getDynamicObject() == nullptr || !root["schema"].isInt()
        || static_cast<int>(root["schema"]) != 1) return invalid();
    const auto* bindings = root["bindings"].getArray();
    const auto* macros = root["macros"].getArray();
    if (bindings == nullptr || macros == nullptr || bindings->size() > static_cast<int>(maximumBindings)
        || macros->size() > static_cast<int>(maximumMacros)) return invalid();
    for (const auto& row : *bindings) {
        BindingOverride binding;
        const auto* keys = row["keys"].getArray();
        if (!stringField(row, "command", binding.commandId) || keys == nullptr || keys->size() > 16)
            return invalid();
        for (const auto& key : *keys) {
            KeyChord chord;
            if (!stringField(key, "key", chord.key) || !key["command"].isBool()
                || !key["shift"].isBool() || !key["alt"].isBool()) return invalid();
            chord.command = key["command"]; chord.shift = key["shift"]; chord.alt = key["alt"];
            binding.keys.push_back(std::move(chord));
        }
        config.bindings.push_back(std::move(binding));
    }
    for (const auto& row : *macros) {
        Macro macro;
        const auto* steps = row["steps"].getArray();
        if (!stringField(row, "id", macro.id) || !stringField(row, "name", macro.name)
            || steps == nullptr || steps->size() > static_cast<int>(maximumMacroSteps)) return invalid();
        for (const auto& step : *steps) {
            CommandInvocation invocation;
            if (!stringField(step, "command", invocation.id)
                || !readArguments(step["arguments"], invocation.arguments)) return invalid();
            macro.steps.push_back(std::move(invocation));
        }
        config.macros.push_back(std::move(macro));
    }
    if (root.hasProperty("arguments")) {
        const auto* remembered = root["arguments"].getDynamicObject();
        if (remembered == nullptr || remembered->getProperties().size() > 512) return invalid();
        for (const auto& row : remembered->getProperties()) {
            CommandArguments arguments;
            if (!readArguments(row.value, arguments)) return invalid();
            config.arguments.emplace(row.name.toString().toStdString(), std::move(arguments));
        }
    }
    return juce::Result::ok();
}
juce::var encode(const CommandConfiguration& config)
{
    auto root = object(); put(root, "schema", config.schema);
    juce::Array<juce::var> bindings, macros;
    for (const auto& binding : config.bindings) {
        auto row = object(); put(row, "command", juce::String(binding.commandId));
        juce::Array<juce::var> keys;
        for (const auto& key : binding.keys) {
            auto chord = object(); put(chord, "key", juce::String(key.key));
            put(chord, "command", key.command); put(chord, "shift", key.shift); put(chord, "alt", key.alt);
            keys.add(chord);
        }
        put(row, "keys", keys); bindings.add(row);
    }
    for (const auto& macro : config.macros) {
        auto row = object(); put(row, "id", juce::String(macro.id)); put(row, "name", juce::String(macro.name));
        juce::Array<juce::var> steps;
        for (const auto& step : macro.steps) {
            auto item = object(); put(item, "command", juce::String(step.id));
            put(item, "arguments", argumentsToJson(step.arguments)); steps.add(item);
        }
        put(row, "steps", steps); macros.add(row);
    }
    auto remembered = object();
    for (const auto& row : config.arguments)
        remembered.getDynamicObject()->setProperty(juce::Identifier(row.first), argumentsToJson(row.second));
    put(root, "bindings", bindings); put(root, "macros", macros); put(root, "arguments", remembered);
    return root;
}
} // namespace

CommandPreferences::CommandPreferences(juce::File file) : file_(std::move(file)) {}
juce::File CommandPreferences::defaultFile(const juce::String& productId)
{
    auto safeName = juce::File::createLegalFileName(productId).substring(0, 100);
    if (safeName.isEmpty()) safeName = "PianoRoll";
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("rmkstudio").getChildFile(safeName).getChildFile("piano-commands.json");
}
juce::Result CommandPreferences::load(CommandDispatcher& dispatcher)
{
    invalidOriginal_ = false; warning_.clear(); lastContents_.clear();
    if (!file_.exists()) return juce::Result::ok();
    if (!file_.existsAsFile() || file_.getSize() > 1024 * 1024) {
        invalidOriginal_ = true; warning_ = "Command settings could not be read. Original preserved.";
        return juce::Result::fail(warning_);
    }
    juce::var parsed;
    lastContents_ = file_.loadFileAsString();
    auto parsedResult = juce::JSON::parse(lastContents_, parsed);
    CommandConfiguration next;
    auto result = parsedResult.wasOk() ? decode(parsed, next) : parsedResult;
    if (result.wasOk()) {
        auto accepted = dispatcher.replaceConfiguration(std::move(next));
        if (!accepted.ok) result = juce::Result::fail(juce::String(accepted.reason));
    }
    if (result.failed()) {
        invalidOriginal_ = true;
        warning_ = "Command settings are invalid. Using session settings; original preserved.";
        return juce::Result::fail(warning_);
    }
    return juce::Result::ok();
}
juce::Result CommandPreferences::save(const CommandDispatcher& dispatcher)
{
    const auto text = juce::JSON::toString(encode(dispatcher.configuration()), false);
    if (text.getNumBytesAsUTF8() > 1024 * 1024)
        return juce::Result::fail("Command settings exceed 1 MB. Reduce macro or text settings before saving.");
    auto result = file_.getParentDirectory().createDirectory();
    if (result.failed()) return juce::Result::fail("Could not create the settings folder.");
    if (file_.exists() && (invalidOriginal_ || file_.loadFileAsString() != lastContents_)) {
        // Another editor or external tool may have replaced the file after load.
        // Preserve that version too rather than silently discarding its bytes.
        auto backup = file_.getSiblingFile(file_.getFileNameWithoutExtension() + (invalidOriginal_ ? ".invalid-" : ".previous-")
            + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".json").getNonexistentSibling();
        if (!file_.existsAsFile() || !file_.copyFileTo(backup))
            return juce::Result::fail("Could not preserve invalid settings. Changes remain in this session.");
        invalidOriginal_ = false;
    }
    juce::TemporaryFile temporary(file_);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return juce::Result::fail("Could not write command settings. Changes remain in this session.");
    if (!stream->writeText(text, false, false, "\n"))
        return juce::Result::fail("Could not finish writing command settings.");
    stream->flush(); result = stream->getStatus(); stream.reset();
    if (result.failed() || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not replace command settings. Previous file preserved.");
    warning_.clear(); lastContents_ = text;
    return juce::Result::ok();
}
} // namespace pianoroll
