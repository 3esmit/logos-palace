#include "palace_vm_engine.h"

#include "palace_sha256.h"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace palace {
namespace {

struct Command {
    enum class Kind { Say, Set, GoToRoom };

    Kind kind;
    std::string first;
    std::string second;
};

enum class EventKind { Enter, Leave, Select };

struct Event {
    EventKind kind;
    std::string spotId;
};

std::string trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r");
    return value.substr(begin, end - begin + 1);
}

bool isIdentifier(const std::string& value)
{
    if (value.empty() || value.size() > 64)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_' || character == '-';
    });
}

bool hasRoom(const std::vector<std::string>& rooms, const std::string& roomId)
{
    return std::find(rooms.begin(), rooms.end(), roomId) != rooms.end();
}

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

std::string serializeList(const std::vector<std::string>& values)
{
    std::string result;
    for (const std::string& value : values)
        result += lengthEncoded(value);
    return result;
}

std::string canonicalWithoutHashes(const VmReceipt& receipt)
{
    return std::string("accepted=") + (receipt.accepted ? "1" : "0")
        + ";local=" + serializeList(receipt.localEffects)
        + ";shared=" + serializeList(receipt.sharedIntents)
        + ";rejected=" + serializeList(receipt.rejectedEffects)
        + ";state=" + canonicalState(receipt.resultingState);
}

void seal(VmReceipt& receipt, const VmContext& context)
{
    receipt.stateRoot = crypto::sha256Hex(canonicalState(receipt.resultingState));
    receipt.executionReceipt = crypto::sha256Hex(
        "profile=" + lengthEncoded(context.profileId)
        + ";bundle=" + lengthEncoded(context.scriptBundleCid)
        + ";epoch=" + lengthEncoded(context.roomEpoch)
        + ";trigger=" + lengthEncoded(context.trigger)
        + ";state_root=" + receipt.stateRoot
        + ";effects=" + canonicalWithoutHashes(receipt));
}

VmReceipt reject(const std::string& reason, const VmContext& context)
{
    VmReceipt receipt;
    receipt.rejectedEffects.push_back(reason);
    receipt.resultingState = context.priorState;
    seal(receipt, context);
    return receipt;
}

bool matches(const Event& event, const std::string& trigger)
{
    switch (event.kind) {
    case EventKind::Enter: return trigger == "ENTER";
    case EventKind::Leave: return trigger == "LEAVE";
    case EventKind::Select: return trigger == "SELECT:" + event.spotId;
    }
    return false;
}

} // namespace

std::string VmReceipt::canonical() const
{
    return canonicalWithoutHashes(*this)
        + ";state_root=" + stateRoot
        + ";receipt=" + executionReceipt;
}

VmReceipt PalaceVmEngine::execute(const std::string& script, const VmContext& context) const
{
    if (context.profileId != "classic-mvp-v1")
        return reject("unsupported-profile", context);
    if (context.instructionBudget == 0)
        return reject("instruction-budget-exhausted", context);
    if (script.size() > context.maxScriptBytes)
        return reject("script-size-exceeded", context);
    if (context.priorState.size() > context.maxStateEntries)
        return reject("state-entry-budget-exceeded", context);

    std::istringstream input(script);
    std::vector<Command> commands;
    Event event{EventKind::Select, {}};
    bool hasEvent = false;
    std::string line;
    std::size_t sourceInstructions = 0;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty())
            continue;
        ++sourceInstructions;
        if (sourceInstructions > context.instructionBudget)
            return reject("instruction-budget-exhausted", context);

        if (!hasEvent) {
            static constexpr const char* kSelectPrefix = "ON SELECT ";
            if (line == "ON ENTER") {
                event = {EventKind::Enter, {}};
                hasEvent = true;
                continue;
            }
            if (line == "ON LEAVE") {
                event = {EventKind::Leave, {}};
                hasEvent = true;
                continue;
            }
            if (line.rfind(kSelectPrefix, 0) != 0)
                return reject("expected-on-select", context);
            event = {EventKind::Select,
                     trim(line.substr(std::char_traits<char>::length(kSelectPrefix)))};
            if (!isIdentifier(event.spotId))
                return reject("invalid-spot-id", context);
            hasEvent = true;
            continue;
        }

        if (line.rfind("SAY ", 0) == 0) {
            const std::string message = trim(line.substr(4));
            if (message.empty() || message.size() > 256)
                return reject("invalid-speech", context);
            commands.push_back({Command::Kind::Say, message, {}});
            continue;
        }

        if (line.rfind("SET ", 0) == 0) {
            std::istringstream setInput(line.substr(4));
            std::string key;
            std::string value;
            std::string trailing;
            if (!(setInput >> key >> value) || (setInput >> trailing) || !isIdentifier(key))
                return reject("invalid-state-transition", context);
            std::int64_t parsedValue = 0;
            const auto conversion = std::from_chars(
                value.data(), value.data() + value.size(), parsedValue);
            if (conversion.ec != std::errc() || conversion.ptr != value.data() + value.size())
                return reject("invalid-state-value", context);
            commands.push_back({Command::Kind::Set, key, value});
            continue;
        }

        if (line.rfind("GOTOROOM ", 0) == 0) {
            const std::string target = trim(line.substr(9));
            if (!isIdentifier(target))
                return reject("invalid-room-id", context);
            commands.push_back({Command::Kind::GoToRoom, target, {}});
            continue;
        }

        return reject("unsupported-instruction", context);
    }

    if (!hasEvent)
        return reject("missing-on-select", context);

    VmReceipt receipt;
    receipt.accepted = true;
    receipt.resultingState = context.priorState;
    if (!matches(event, context.trigger)) {
        seal(receipt, context);
        return receipt;
    }

    for (const Command& command : commands) {
        switch (command.kind) {
        case Command::Kind::Say:
            receipt.localEffects.push_back("say:" + command.first);
            break;
        case Command::Kind::Set: {
            if (!context.canMutateSharedState) {
                receipt.rejectedEffects.push_back("capability-denied:set:" + command.first);
                break;
            }
            std::int64_t value = 0;
            std::from_chars(command.second.data(),
                            command.second.data() + command.second.size(), value);
            if (receipt.resultingState.find(command.first) == receipt.resultingState.end()
                && receipt.resultingState.size() >= context.maxStateEntries) {
                return reject("state-entry-budget-exceeded", context);
            }
            receipt.resultingState[command.first] = value;
            receipt.sharedIntents.push_back("set:" + command.first + "=" + command.second);
            break;
        }
        case Command::Kind::GoToRoom:
            if (context.roomLocked) {
                receipt.rejectedEffects.push_back("room-locked:navigate:" + command.first);
            } else if (!hasRoom(context.allowedRooms, command.first)) {
                receipt.rejectedEffects.push_back("unknown-room:navigate:" + command.first);
            } else {
                receipt.localEffects.push_back("navigate:" + command.first);
            }
            break;
        }
    }
    const std::size_t effectCount = receipt.localEffects.size()
        + receipt.sharedIntents.size() + receipt.rejectedEffects.size();
    if (effectCount > context.maxEffects)
        return reject("effect-budget-exhausted", context);
    if (canonicalWithoutHashes(receipt).size() > context.maxOutputBytes)
        return reject("output-budget-exhausted", context);
    seal(receipt, context);
    return receipt;
}

std::map<std::string, std::int64_t> parseCanonicalState(const std::string& value)
{
    std::map<std::string, std::int64_t> state;
    std::istringstream input(value);
    std::string entry;
    while (std::getline(input, entry, ';')) {
        if (entry.empty())
            continue;
        const std::size_t separator = entry.find('=');
        if (separator == std::string::npos)
            return {};
        const std::string key = entry.substr(0, separator);
        const std::string rawValue = entry.substr(separator + 1);
        if (!isIdentifier(key) || rawValue.empty())
            return {};
        std::int64_t parsedValue = 0;
        const auto conversion = std::from_chars(
            rawValue.data(), rawValue.data() + rawValue.size(), parsedValue);
        if (conversion.ec != std::errc() || conversion.ptr != rawValue.data() + rawValue.size())
            return {};
        state.emplace(key, parsedValue);
    }
    return state;
}

std::vector<std::string> parseCanonicalRooms(const std::string& value)
{
    std::vector<std::string> rooms;
    std::istringstream input(value);
    std::string room;
    while (std::getline(input, room, ',')) {
        if (!isIdentifier(room))
            return {};
        rooms.push_back(room);
    }
    return rooms;
}

std::string canonicalState(const std::map<std::string, std::int64_t>& state)
{
    std::string result;
    for (const auto& [key, value] : state) {
        if (!result.empty())
            result += ';';
        result += key + "=" + std::to_string(value);
    }
    return result;
}

} // namespace palace
