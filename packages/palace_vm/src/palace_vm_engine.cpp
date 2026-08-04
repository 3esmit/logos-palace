#include "palace_vm_engine.h"

#include "palace_sha256.h"

#include <algorithm>
#include <charconv>
#include <set>
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
        + ";deferred=" + serializeList(receipt.deferredEffects)
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
        + ";phase="
        + (context.orderedFinalized ? "finalized" : "provisional")
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

bool isCanonicalUnsignedDecimal(const std::string& value)
{
    if (value.empty() || value.size() > 20U
        || (value.size() > 1U && value.front() == '0')) {
        return false;
    }
    std::uint64_t parsed = 0U;
    const auto conversion = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return conversion.ec == std::errc()
        && conversion.ptr == value.data() + value.size();
}

bool isCanonicalTrigger(const std::string& trigger)
{
    static constexpr const char* kSelectPrefix = "SELECT:";
    if (trigger == "ENTER" || trigger == "LEAVE")
        return true;
    if (trigger.rfind(kSelectPrefix, 0U) != 0U)
        return false;
    return isIdentifier(
        trigger.substr(std::char_traits<char>::length(kSelectPrefix)));
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
    if (!context.inputError.empty())
        return reject(context.inputError, context);
    if (context.profileId != "classic-mvp-v1")
        return reject("unsupported-profile", context);
    if (!isIdentifier(context.scriptBundleCid)
        || !isCanonicalUnsignedDecimal(context.roomEpoch)
        || !isCanonicalTrigger(context.trigger)) {
        return reject("invalid-turn-context", context);
    }
    if (context.instructionBudget == 0)
        return reject("instruction-budget-exhausted", context);
    if (script.size() > context.maxScriptBytes)
        return reject("script-size-exceeded", context);
    if (context.priorState.size() > context.maxStateEntries)
        return reject("state-entry-budget-exceeded", context);
    if (context.allowedRooms.size() > 64U)
        return reject("room-entry-budget-exceeded", context);
    if (!std::all_of(
            context.priorState.begin(),
            context.priorState.end(),
            [](const auto& entry) { return isIdentifier(entry.first); })) {
        return reject("invalid-state-key", context);
    }
    std::set<std::string> uniqueRooms;
    for (const std::string& room : context.allowedRooms) {
        if (!isIdentifier(room) || !uniqueRooms.insert(room).second)
            return reject("invalid-room-set", context);
    }

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

    const bool hasSharedStateCommand = std::any_of(
        commands.begin(), commands.end(), [](const Command& command) {
            return command.kind == Command::Kind::Set;
        });
    if (hasSharedStateCommand && !context.canMutateSharedState)
        return reject("capability-denied:shared-state", context);

    for (const Command& command : commands) {
        switch (command.kind) {
        case Command::Kind::Say:
            receipt.localEffects.push_back("say:" + command.first);
            break;
        case Command::Kind::Set: {
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
            } else if (!context.orderedFinalized) {
                receipt.deferredEffects.push_back(
                    "await-finality:navigate:" + command.first);
            } else {
                receipt.localEffects.push_back("navigate:" + command.first);
            }
            break;
        }
    }
    const std::size_t effectCount = receipt.localEffects.size()
        + receipt.sharedIntents.size() + receipt.deferredEffects.size()
        + receipt.rejectedEffects.size();
    if (effectCount > context.maxEffects)
        return reject("effect-budget-exhausted", context);
    if (canonicalWithoutHashes(receipt).size() > context.maxOutputBytes)
        return reject("output-budget-exhausted", context);
    seal(receipt, context);
    return receipt;
}

CanonicalStateParseResult parseCanonicalState(
    const std::string& value,
    std::size_t maxBytes,
    std::size_t maxEntries)
{
    CanonicalStateParseResult result;
    if (value.size() > maxBytes) {
        result.reason = "state-size-exceeded";
        return result;
    }
    std::map<std::string, std::int64_t> state;
    std::istringstream input(value);
    std::string entry;
    while (std::getline(input, entry, ';')) {
        if (entry.empty()) {
            result.reason = "invalid-state-encoding";
            return result;
        }
        if (state.size() >= maxEntries) {
            result.reason = "state-entry-budget-exceeded";
            return result;
        }
        const std::size_t separator = entry.find('=');
        if (separator == std::string::npos
            || entry.find('=', separator + 1U) != std::string::npos) {
            result.reason = "invalid-state-encoding";
            return result;
        }
        const std::string key = entry.substr(0, separator);
        const std::string rawValue = entry.substr(separator + 1);
        if (!isIdentifier(key) || rawValue.empty()) {
            result.reason = "invalid-state-encoding";
            return result;
        }
        std::int64_t parsedValue = 0;
        const auto conversion = std::from_chars(
            rawValue.data(), rawValue.data() + rawValue.size(), parsedValue);
        if (conversion.ec != std::errc()
            || conversion.ptr != rawValue.data() + rawValue.size()) {
            result.reason = "invalid-state-encoding";
            return result;
        }
        if (!state.emplace(key, parsedValue).second) {
            result.reason = "duplicate-state-key";
            return result;
        }
    }
    if (canonicalState(state) != value) {
        result.reason = "noncanonical-state-encoding";
        return result;
    }
    result.accepted = true;
    result.value = std::move(state);
    return result;
}

CanonicalRoomsParseResult parseCanonicalRooms(
    const std::string& value,
    std::size_t maxBytes,
    std::size_t maxRooms)
{
    CanonicalRoomsParseResult result;
    if (value.size() > maxBytes) {
        result.reason = "room-set-size-exceeded";
        return result;
    }
    std::vector<std::string> rooms;
    std::set<std::string> uniqueRooms;
    std::istringstream input(value);
    std::string room;
    while (std::getline(input, room, ',')) {
        if (rooms.size() >= maxRooms) {
            result.reason = "room-entry-budget-exceeded";
            return result;
        }
        if (!isIdentifier(room)) {
            result.reason = "invalid-room-set";
            return result;
        }
        if (!uniqueRooms.insert(room).second) {
            result.reason = "duplicate-room";
            return result;
        }
        rooms.push_back(room);
    }
    if (canonicalRooms(rooms) != value) {
        result.reason = "noncanonical-room-set";
        return result;
    }
    result.accepted = true;
    result.value = std::move(rooms);
    return result;
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

std::string canonicalRooms(const std::vector<std::string>& rooms)
{
    std::string result;
    for (const std::string& room : rooms) {
        if (!result.empty())
            result += ',';
        result += room;
    }
    return result;
}

} // namespace palace
