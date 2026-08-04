#include "palace_action_journal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "palace_sha256.h"

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr const char* kJournalFileName = "action-journal-v2";
constexpr const char* kLegacyJournalFileName = "action-journal-v1";
constexpr const char* kTemporaryFileName = "action-journal-v2.next";
constexpr char kHexDigits[] = "0123456789abcdef";

bool flushFile(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

bool flushDirectory(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

std::string hexEncode(const std::string& value)
{
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(kHexDigits[byte >> 4U]);
        encoded.push_back(kHexDigits[byte & 0x0fU]);
    }
    return encoded;
}

bool hexNibble(char character, unsigned char& value)
{
    if (character >= '0' && character <= '9') {
        value = static_cast<unsigned char>(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<unsigned char>(character - 'a' + 10);
        return true;
    }
    return false;
}

bool hexDecode(const std::string& encoded, std::string& value)
{
    if (encoded.empty() || encoded.size() % 2U != 0U)
        return false;
    value.clear();
    value.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        unsigned char high = 0;
        unsigned char low = 0;
        if (!hexNibble(encoded[index], high) || !hexNibble(encoded[index + 1U], low))
            return false;
        value.push_back(static_cast<char>((high << 4U) | low));
    }
    return true;
}

bool parseStage(const std::string& value, DurableActionStage& stage)
{
    if (value == "0") {
        stage = DurableActionStage::LocalDraft;
    } else if (value == "1") {
        stage = DurableActionStage::Queued;
    } else if (value == "2") {
        stage = DurableActionStage::SubmittedToLez;
    } else if (value == "3") {
        stage = DurableActionStage::Observed;
    } else if (value == "4") {
        stage = DurableActionStage::Finalized;
    } else if (value == "5") {
        stage = DurableActionStage::Rejected;
    } else if (value == "6") {
        stage = DurableActionStage::Expired;
    } else if (value == "7") {
        stage = DurableActionStage::Orphaned;
    } else {
        return false;
    }
    return true;
}

bool isCanonicalTransactionHash(const std::string& value)
{
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

std::string serializeRecord(const ActionJournal& journal)
{
    const std::string state = journal.canonicalState();
    return crypto::sha256Hex(state) + "\n" + state;
}

bool parseRecord(const std::string& record, std::string& state)
{
    const std::size_t newline = record.find('\n');
    if (newline == std::string::npos)
        return false;
    const std::string checksum = record.substr(0, newline);
    state = record.substr(newline + 1U);
    return checksum.size() == 64U && checksum == crypto::sha256Hex(state);
}

} // namespace

bool ActionJournal::createDraft(const std::string& actionId)
{
    if (actionId.empty() || m_actions.find(actionId) != m_actions.end())
        return false;
    m_actions.emplace(actionId, ActionStatus{});
    return true;
}

bool ActionJournal::queue(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage != DurableActionStage::LocalDraft)
        return false;
    found->second.durableStage = DurableActionStage::Queued;
    return true;
}

bool ActionJournal::markSubmittedToLez(const std::string& actionId, const std::string& transactionHash)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage != DurableActionStage::Queued
        || !isCanonicalTransactionHash(transactionHash)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::SubmittedToLez;
    found->second.transactionHash = transactionHash;
    return true;
}

bool ActionJournal::markObserved(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || found->second.durableStage != DurableActionStage::SubmittedToLez) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Observed;
    return true;
}

bool ActionJournal::markFinalized(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || found->second.durableStage != DurableActionStage::Observed) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Finalized;
    return true;
}

bool ActionJournal::markRejected(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::Queued
            && found->second.durableStage != DurableActionStage::SubmittedToLez)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Rejected;
    return true;
}

bool ActionJournal::markExpired(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::Queued
            && found->second.durableStage != DurableActionStage::SubmittedToLez
            && found->second.durableStage != DurableActionStage::Observed)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Expired;
    return true;
}

bool ActionJournal::markOrphaned(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::SubmittedToLez
            && found->second.durableStage != DurableActionStage::Observed)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Orphaned;
    return true;
}

bool ActionJournal::markDeliveryPublished(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage == DurableActionStage::LocalDraft)
        return false;
    found->second.deliveryPublished = true;
    return true;
}

ActionStatus ActionJournal::status(const std::string& actionId) const
{
    const auto found = m_actions.find(actionId);
    return found == m_actions.end() ? ActionStatus{} : found->second;
}

std::string ActionJournal::canonicalState() const
{
    std::ostringstream state;
    state << "version=2\n";
    for (const auto& [actionId, status] : m_actions) {
        state << hexEncode(actionId) << ';'
              << static_cast<unsigned int>(status.durableStage) << ';'
              << (status.deliveryPublished ? '1' : '0') << ';'
              << (status.transactionHash.empty() ? "-" : status.transactionHash) << '\n';
    }
    return state.str();
}

bool ActionJournal::restoreCanonicalState(const std::string& serialized)
{
    static constexpr const char* kVersionOnePrefix = "version=1\n";
    static constexpr const char* kVersionTwoPrefix = "version=2\n";
    const bool legacyVersion = serialized.rfind(kVersionOnePrefix, 0) == 0;
    const bool currentVersion = serialized.rfind(kVersionTwoPrefix, 0) == 0;
    if (!legacyVersion && !currentVersion)
        return false;

    std::map<std::string, ActionStatus> restored;
    const char* prefix = legacyVersion ? kVersionOnePrefix : kVersionTwoPrefix;
    std::istringstream input(serialized.substr(std::char_traits<char>::length(prefix)));
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty())
            return false;
        const std::size_t first = line.find(';');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find(';', first + 1U);
        const std::size_t third = second == std::string::npos ? std::string::npos : line.find(';', second + 1U);
        if (first == std::string::npos || second == std::string::npos
            || (!legacyVersion && third == std::string::npos)
            || (legacyVersion && third != std::string::npos)
            || (!legacyVersion && line.find(';', third + 1U) != std::string::npos)) {
            return false;
        }
        std::string actionId;
        DurableActionStage stage = DurableActionStage::LocalDraft;
        const std::string delivery = line.substr(second + 1U,
                                                 (legacyVersion ? line.size() : third) - second - 1U);
        std::string transactionHash;
        if (!legacyVersion) {
            transactionHash = line.substr(third + 1U);
            if (transactionHash == "-")
                transactionHash.clear();
        }
        if (!hexDecode(line.substr(0, first), actionId)
            || !parseStage(line.substr(first + 1U, second - first - 1U), stage)
            || (delivery != "0" && delivery != "1")
            || (!transactionHash.empty() && !isCanonicalTransactionHash(transactionHash))) {
            return false;
        }
        if (legacyVersion && (stage == DurableActionStage::SubmittedToLez
                              || stage == DurableActionStage::Observed
                              || stage == DurableActionStage::Finalized
                              || stage == DurableActionStage::Expired)) {
            stage = DurableActionStage::Orphaned;
        }
        const bool hashRequired = stage == DurableActionStage::SubmittedToLez
            || stage == DurableActionStage::Observed
            || stage == DurableActionStage::Finalized
            || stage == DurableActionStage::Expired;
        if ((hashRequired && transactionHash.empty())
            || !restored.emplace(actionId,
                                  ActionStatus{stage, delivery == "1", transactionHash}).second) {
            return false;
        }
    }
    m_actions = std::move(restored);
    return true;
}

ActionJournalStore::ActionJournalStore(std::string directory)
    : m_directory(std::move(directory))
{
}

bool ActionJournalStore::save(const ActionJournal& journal) const
{
    if (m_directory.empty())
        return false;
    std::error_code error;
    const fs::path directory(m_directory);
    fs::create_directories(directory, error);
    if (error)
        return false;

    const fs::path temporary = directory / kTemporaryFileName;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << serializeRecord(journal);
        output.flush();
        if (!output.good())
            return false;
    }
    if (!flushFile(temporary))
        return false;

    const fs::path destination = directory / kJournalFileName;
    fs::rename(temporary, destination, error);
    if (error)
        return false;
    return flushDirectory(directory);
}

bool ActionJournalStore::load(ActionJournal& journal) const
{
    if (m_directory.empty())
        return false;
    std::ifstream input(fs::path(m_directory) / kJournalFileName, std::ios::binary);
    if (!input) {
        input.clear();
        input.open(fs::path(m_directory) / kLegacyJournalFileName, std::ios::binary);
    }
    if (!input)
        return false;
    const std::string record((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string state;
    return parseRecord(record, state) && journal.restoreCanonicalState(state);
}

bool ActionJournalStore::exists() const
{
    if (m_directory.empty())
        return false;
    std::error_code error;
    const fs::path directory(m_directory);
    const bool currentExists = fs::exists(directory / kJournalFileName, error);
    if (error)
        return false;
    const bool legacyExists = fs::exists(directory / kLegacyJournalFileName, error);
    return !error && (currentExists || legacyExists);
}

std::string actionStatusName(DurableActionStage stage)
{
    switch (stage) {
    case DurableActionStage::LocalDraft: return "local_draft";
    case DurableActionStage::Queued: return "queued";
    case DurableActionStage::SubmittedToLez: return "submitted_to_lez";
    case DurableActionStage::Observed: return "observed";
    case DurableActionStage::Finalized: return "finalized";
    case DurableActionStage::Rejected: return "rejected";
    case DurableActionStage::Expired: return "expired";
    case DurableActionStage::Orphaned: return "orphaned";
    }
    return "unknown";
}

std::string canonicalActionStatus(const ActionStatus& status)
{
    return "durable=" + actionStatusName(status.durableStage)
        + ";delivery_published=" + (status.deliveryPublished ? "1" : "0");
}

} // namespace palace
