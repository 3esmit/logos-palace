#include "palace_lez_local_history.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace palace {
namespace {

constexpr std::size_t kMaximumPageBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumJsonDepth = 32U;
constexpr std::size_t kMaximumJsonNodes = 200000U;
constexpr std::size_t kMaximumBlocksPerPage = 32U;
constexpr std::size_t kMaximumTransactionsPerPage = 256U;
constexpr std::size_t kMaximumAccountsPerPage = 1024U;
constexpr std::size_t kMaximumInstructionWordsPerPage = 0x4000U;
constexpr std::size_t kMaximumPages = 64U;
constexpr std::size_t kMaximumBlocks = kMaximumPages * kMaximumBlocksPerPage;
constexpr std::size_t kMaximumActions = 256U;

bool isAsciiDigit(const char value) { return value >= '0' && value <= '9'; }

bool decodeUtf8CodePoint(const std::string &value, std::size_t &cursor,
                         std::uint32_t &codePoint) {
  if (cursor >= value.size())
    return false;
  const auto first = static_cast<std::uint8_t>(value[cursor]);
  if (first <= 0x7fU) {
    codePoint = first;
    ++cursor;
    return true;
  }

  std::size_t continuationCount = 0U;
  std::uint32_t minimum = 0U;
  if (first >= 0xc2U && first <= 0xdfU) {
    continuationCount = 1U;
    codePoint = first & 0x1fU;
    minimum = 0x80U;
  } else if (first >= 0xe0U && first <= 0xefU) {
    continuationCount = 2U;
    codePoint = first & 0x0fU;
    minimum = 0x800U;
  } else if (first >= 0xf0U && first <= 0xf4U) {
    continuationCount = 3U;
    codePoint = first & 0x07U;
    minimum = 0x10000U;
  } else {
    return false;
  }
  if (continuationCount > value.size() - cursor - 1U)
    return false;
  for (std::size_t index = 0U; index < continuationCount; ++index) {
    const auto continuation =
        static_cast<std::uint8_t>(value[cursor + index + 1U]);
    if ((continuation & 0xc0U) != 0x80U)
      return false;
    codePoint = (codePoint << 6U) | (continuation & 0x3fU);
  }
  cursor += continuationCount + 1U;
  return codePoint >= minimum && codePoint <= 0x10ffffU &&
         !(codePoint >= 0xd800U && codePoint <= 0xdfffU);
}

bool isValidUtf8(const std::string &value) {
  std::size_t cursor = 0U;
  while (cursor < value.size()) {
    std::uint32_t codePoint = 0U;
    if (!decodeUtf8CodePoint(value, cursor, codePoint))
      return false;
  }
  return true;
}

bool appendUtf8(std::string &output, const std::uint32_t codePoint) {
  if (codePoint <= 0x7fU) {
    output.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  } else if (codePoint <= 0xffffU) {
    if (codePoint >= 0xd800U && codePoint <= 0xdfffU)
      return false;
    output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  } else if (codePoint <= 0x10ffffU) {
    output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  } else {
    return false;
  }
  return true;
}

enum class JsonType : std::uint8_t {
  Null,
  Boolean,
  Number,
  String,
  Array,
  Object,
};

struct JsonValue {
  JsonType type = JsonType::Null;
  bool boolean = false;
  std::string text;
  std::vector<JsonValue> array;
  std::vector<std::pair<std::string, JsonValue>> object;
};

class StrictJsonParser {
public:
  StrictJsonParser(const std::string &input, const std::size_t maximumDepth,
                   const std::size_t maximumNodes)
      : input_(input), maximumDepth_(maximumDepth),
        maximumNodes_(maximumNodes) {}

  bool parse(JsonValue &output, std::string &reason) {
    if (!isValidUtf8(input_)) {
      reason = "invalid-json-utf8";
      return false;
    }
    skipWhitespace();
    if (!parseValue(0U, output)) {
      reason = reason_.empty() ? "invalid-json" : reason_;
      return false;
    }
    skipWhitespace();
    if (cursor_ != input_.size()) {
      reason = "trailing-json-data";
      return false;
    }
    return true;
  }

private:
  bool fail(const char *reason) {
    if (reason_.empty())
      reason_ = reason;
    return false;
  }

  void skipWhitespace() {
    while (cursor_ < input_.size()) {
      const char value = input_[cursor_];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
        break;
      }
      ++cursor_;
    }
  }

  bool parseValue(const std::size_t depth, JsonValue &output) {
    if (depth > maximumDepth_)
      return fail("json-too-deep");
    if (++nodes_ > maximumNodes_)
      return fail("json-too-many-nodes");
    skipWhitespace();
    if (cursor_ >= input_.size())
      return fail("truncated-json");
    switch (input_[cursor_]) {
    case 'n':
      output.type = JsonType::Null;
      return parseLiteral("null");
    case 't':
      output.type = JsonType::Boolean;
      output.boolean = true;
      return parseLiteral("true");
    case 'f':
      output.type = JsonType::Boolean;
      output.boolean = false;
      return parseLiteral("false");
    case '"':
      output.type = JsonType::String;
      return parseString(output.text);
    case '[':
      output.type = JsonType::Array;
      return parseArray(depth, output.array);
    case '{':
      output.type = JsonType::Object;
      return parseObject(depth, output.object);
    default:
      output.type = JsonType::Number;
      return parseNumber(output.text);
    }
  }

  bool parseLiteral(const std::string_view literal) {
    if (literal.size() > input_.size() - cursor_ ||
        input_.compare(cursor_, literal.size(), literal) != 0) {
      return fail("invalid-json-literal");
    }
    cursor_ += literal.size();
    return true;
  }

  bool parseHex4(std::uint32_t &output) {
    if (4U > input_.size() - cursor_)
      return false;
    output = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
      const char character = input_[cursor_++];
      output <<= 4U;
      if (character >= '0' && character <= '9')
        output |= static_cast<std::uint32_t>(character - '0');
      else if (character >= 'a' && character <= 'f')
        output |= static_cast<std::uint32_t>(character - 'a' + 10);
      else if (character >= 'A' && character <= 'F')
        output |= static_cast<std::uint32_t>(character - 'A' + 10);
      else
        return false;
    }
    return true;
  }

  bool parseString(std::string &output) {
    if (input_[cursor_] != '"')
      return fail("expected-json-string");
    ++cursor_;
    output.clear();
    while (cursor_ < input_.size()) {
      const auto character = static_cast<std::uint8_t>(input_[cursor_++]);
      if (character == '"')
        return isValidUtf8(output) || fail("invalid-json-string-utf8");
      if (character < 0x20U)
        return fail("unescaped-json-control");
      if (character != '\\') {
        output.push_back(static_cast<char>(character));
        continue;
      }
      if (cursor_ >= input_.size())
        return fail("truncated-json-escape");
      const char escaped = input_[cursor_++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        output.push_back(escaped);
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u': {
        std::uint32_t first = 0U;
        if (!parseHex4(first))
          return fail("invalid-json-unicode-escape");
        std::uint32_t codePoint = first;
        if (first >= 0xd800U && first <= 0xdbffU) {
          if (6U > input_.size() - cursor_ || input_[cursor_] != '\\' ||
              input_[cursor_ + 1U] != 'u') {
            return fail("invalid-json-surrogate-pair");
          }
          cursor_ += 2U;
          std::uint32_t second = 0U;
          if (!parseHex4(second) || second < 0xdc00U || second > 0xdfffU) {
            return fail("invalid-json-surrogate-pair");
          }
          codePoint =
              0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U);
        } else if (first >= 0xdc00U && first <= 0xdfffU) {
          return fail("invalid-json-surrogate-pair");
        }
        if (!appendUtf8(output, codePoint))
          return fail("invalid-json-code-point");
        break;
      }
      default:
        return fail("invalid-json-escape");
      }
      if (output.size() > input_.size())
        return fail("json-string-too-large");
    }
    return fail("unterminated-json-string");
  }

  bool parseNumber(std::string &output) {
    const std::size_t start = cursor_;
    if (input_[cursor_] == '-')
      ++cursor_;
    if (cursor_ >= input_.size())
      return fail("invalid-json-number");
    if (input_[cursor_] == '0') {
      ++cursor_;
      if (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
        return fail("noncanonical-json-number");
    } else if (input_[cursor_] >= '1' && input_[cursor_] <= '9') {
      while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
        ++cursor_;
    } else {
      return fail("invalid-json-number");
    }
    if (cursor_ < input_.size() && input_[cursor_] == '.') {
      ++cursor_;
      if (cursor_ >= input_.size() || !isAsciiDigit(input_[cursor_]))
        return fail("invalid-json-number");
      while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
        ++cursor_;
    }
    if (cursor_ < input_.size() &&
        (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
      ++cursor_;
      if (cursor_ < input_.size() &&
          (input_[cursor_] == '+' || input_[cursor_] == '-')) {
        ++cursor_;
      }
      if (cursor_ >= input_.size() || !isAsciiDigit(input_[cursor_]))
        return fail("invalid-json-number");
      while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
        ++cursor_;
    }
    output.assign(input_, start, cursor_ - start);
    if (output.size() > 64U)
      return fail("json-number-too-large");
    return true;
  }

  bool parseArray(const std::size_t depth, std::vector<JsonValue> &output) {
    ++cursor_;
    skipWhitespace();
    if (cursor_ < input_.size() && input_[cursor_] == ']') {
      ++cursor_;
      return true;
    }
    while (cursor_ < input_.size()) {
      JsonValue value;
      if (!parseValue(depth + 1U, value))
        return false;
      output.push_back(std::move(value));
      skipWhitespace();
      if (cursor_ >= input_.size())
        return fail("unterminated-json-array");
      if (input_[cursor_] == ']') {
        ++cursor_;
        return true;
      }
      if (input_[cursor_] != ',')
        return fail("invalid-json-array-separator");
      ++cursor_;
      skipWhitespace();
    }
    return fail("unterminated-json-array");
  }

  bool parseObject(const std::size_t depth,
                   std::vector<std::pair<std::string, JsonValue>> &output) {
    ++cursor_;
    skipWhitespace();
    if (cursor_ < input_.size() && input_[cursor_] == '}') {
      ++cursor_;
      return true;
    }
    std::set<std::string> keys;
    while (cursor_ < input_.size()) {
      if (input_[cursor_] != '"')
        return fail("invalid-json-object-key");
      std::string key;
      if (!parseString(key))
        return false;
      if (!keys.insert(key).second)
        return fail("duplicate-json-key");
      skipWhitespace();
      if (cursor_ >= input_.size() || input_[cursor_] != ':')
        return fail("invalid-json-object-separator");
      ++cursor_;
      JsonValue value;
      if (!parseValue(depth + 1U, value))
        return false;
      output.emplace_back(std::move(key), std::move(value));
      skipWhitespace();
      if (cursor_ >= input_.size())
        return fail("unterminated-json-object");
      if (input_[cursor_] == '}') {
        ++cursor_;
        return true;
      }
      if (input_[cursor_] != ',')
        return fail("invalid-json-object-separator");
      ++cursor_;
      skipWhitespace();
    }
    return fail("unterminated-json-object");
  }

  const std::string &input_;
  std::size_t maximumDepth_ = 0U;
  std::size_t maximumNodes_ = 0U;
  std::size_t cursor_ = 0U;
  std::size_t nodes_ = 0U;
  std::string reason_;
};

const JsonValue *member(const JsonValue &object, const std::string_view key) {
  if (object.type != JsonType::Object)
    return nullptr;
  const auto found =
      std::find_if(object.object.begin(), object.object.end(),
                   [&](const auto &entry) { return entry.first == key; });
  return found == object.object.end() ? nullptr : &found->second;
}

bool exactKeys(const JsonValue &object,
               const std::initializer_list<std::string_view> expected) {
  if (object.type != JsonType::Object ||
      object.object.size() != expected.size()) {
    return false;
  }
  return std::all_of(expected.begin(), expected.end(), [&](const auto key) {
    return member(object, key) != nullptr;
  });
}

bool jsonUnsigned64(const JsonValue &value, std::uint64_t &output) {
  if (value.type != JsonType::Number || value.text.empty() ||
      value.text.front() == '-' ||
      value.text.find_first_of(".eE") != std::string::npos) {
    return false;
  }
  const auto parsed = std::from_chars(
      value.text.data(), value.text.data() + value.text.size(), output);
  return parsed.ec == std::errc{} &&
         parsed.ptr == value.text.data() + value.text.size();
}

bool isLowerHex(const std::string &value, const std::size_t exactSize) {
  return value.size() == exactSize &&
         std::all_of(value.begin(), value.end(), [](const char character) {
           return isAsciiDigit(character) ||
                  (character >= 'a' && character <= 'f');
         });
}

bool isNonzeroLowerHex(const std::string &value, const std::size_t exactSize) {
  return isLowerHex(value, exactSize) &&
         std::any_of(value.begin(), value.end(),
                     [](const char character) { return character != '0'; });
}

struct LocalHeaderV1 {
  std::uint64_t blockId = 0U;
  std::string blockHashHex;
  std::string previousBlockHashHex;
};

struct LocalTransactionV1 {
  std::string transactionHashHex;
  std::string programIdHex;
  std::vector<std::string> accountIdsHex;
  std::vector<std::uint32_t> instructionWords;
};

struct LocalBlockV1 {
  LocalHeaderV1 header;
  std::vector<LocalTransactionV1> publicTransactions;
};

struct LocalPageV1 {
  LocalHeaderV1 snapshotTip;
  std::vector<LocalBlockV1> blocks;
  std::optional<std::uint64_t> nextBlockId;
};

bool parseHeader(const JsonValue &value, LocalHeaderV1 &output) {
  if (!exactKeys(value, {"block_id", "block_hash", "previous_block_hash"})) {
    return false;
  }
  const JsonValue *blockId = member(value, "block_id");
  const JsonValue *blockHash = member(value, "block_hash");
  const JsonValue *previousBlockHash = member(value, "previous_block_hash");
  if (!jsonUnsigned64(*blockId, output.blockId) ||
      blockHash->type != JsonType::String ||
      !isNonzeroLowerHex(blockHash->text, 64U) ||
      previousBlockHash->type != JsonType::String ||
      !isLowerHex(previousBlockHash->text, 64U)) {
    return false;
  }
  output.blockHashHex = blockHash->text;
  output.previousBlockHashHex = previousBlockHash->text;
  return true;
}

bool programIdWordsToHex(const JsonValue &value, std::string &output) {
  if (value.type != JsonType::Array || value.array.size() != 8U)
    return false;
  constexpr char kHex[] = "0123456789abcdef";
  output.clear();
  output.reserve(64U);
  for (const JsonValue &wordValue : value.array) {
    std::uint64_t word = 0U;
    if (!jsonUnsigned64(wordValue, word) ||
        word > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    const auto value32 = static_cast<std::uint32_t>(word);
    for (std::size_t byte = 0U; byte < sizeof(value32); ++byte) {
      const auto octet =
          static_cast<std::uint8_t>((value32 >> (byte * 8U)) & 0xffU);
      output.push_back(kHex[octet >> 4U]);
      output.push_back(kHex[octet & 0x0fU]);
    }
  }
  return true;
}

bool parseTransaction(const JsonValue &value, LocalTransactionV1 &output) {
  if (!exactKeys(value, {"transaction_hash", "program_id", "account_ids",
                         "instruction_data"})) {
    return false;
  }
  const JsonValue *transactionHash = member(value, "transaction_hash");
  const JsonValue *programId = member(value, "program_id");
  const JsonValue *accountIds = member(value, "account_ids");
  const JsonValue *instructionData = member(value, "instruction_data");
  if (transactionHash->type != JsonType::String ||
      !isNonzeroLowerHex(transactionHash->text, 64U) ||
      !programIdWordsToHex(*programId, output.programIdHex) ||
      accountIds->type != JsonType::Array || accountIds->array.empty() ||
      accountIds->array.size() > kMaximumAccountsPerPage ||
      instructionData->type != JsonType::Array ||
      instructionData->array.empty() ||
      instructionData->array.size() > kMaximumInstructionWordsPerPage) {
    return false;
  }

  std::set<std::string> distinctAccounts;
  output.accountIdsHex.clear();
  output.accountIdsHex.reserve(accountIds->array.size());
  for (const JsonValue &accountId : accountIds->array) {
    if (accountId.type != JsonType::String)
      return false;
    PalaceLezBytes32 decoded{};
    if (!PalaceLezCodec::parseAccountIdBase58(accountId.text, decoded)) {
      return false;
    }
    const std::string accountIdHex = PalaceLezCodec::bytes32Hex(decoded);
    if (!distinctAccounts.insert(accountIdHex).second)
      return false;
    output.accountIdsHex.push_back(accountIdHex);
  }

  output.instructionWords.clear();
  output.instructionWords.reserve(instructionData->array.size());
  for (const JsonValue &wordValue : instructionData->array) {
    std::uint64_t word = 0U;
    if (!jsonUnsigned64(wordValue, word) ||
        word > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    output.instructionWords.push_back(static_cast<std::uint32_t>(word));
  }
  output.transactionHashHex = transactionHash->text;
  return true;
}

bool parseBlock(const JsonValue &value, LocalBlockV1 &output) {
  if (!exactKeys(value, {"header", "public_transactions"}))
    return false;
  const JsonValue *header = member(value, "header");
  const JsonValue *transactions = member(value, "public_transactions");
  if (!parseHeader(*header, output.header) ||
      transactions->type != JsonType::Array ||
      transactions->array.size() > kMaximumTransactionsPerPage) {
    return false;
  }
  output.publicTransactions.clear();
  output.publicTransactions.reserve(transactions->array.size());
  for (const JsonValue &transaction : transactions->array) {
    LocalTransactionV1 parsed;
    if (!parseTransaction(transaction, parsed))
      return false;
    output.publicTransactions.push_back(std::move(parsed));
  }
  return true;
}

const JsonValue *localHistoryPageDocument(const JsonValue &document) {
  if (exactKeys(document, {"snapshot_tip", "blocks", "next_block_id"}))
    return &document;
  if (!exactKeys(document, {"jsonrpc", "id", "result"}))
    return nullptr;
  const JsonValue *result = member(document, "result");
  if (result == nullptr ||
      !exactKeys(*result, {"snapshot_tip", "blocks", "next_block_id"})) {
    return nullptr;
  }
  return result;
}

bool parsePage(const std::string &body, LocalPageV1 &output) {
  if (body.size() > kMaximumPageBytes)
    return false;
  JsonValue document;
  std::string ignoredReason;
  StrictJsonParser parser(body, kMaximumJsonDepth, kMaximumJsonNodes);
  if (!parser.parse(document, ignoredReason)) {
    return false;
  }
  const JsonValue *pageDocument = localHistoryPageDocument(document);
  if (pageDocument == nullptr)
    return false;
  const JsonValue *snapshotTip = member(*pageDocument, "snapshot_tip");
  const JsonValue *blocks = member(*pageDocument, "blocks");
  const JsonValue *nextBlockId = member(*pageDocument, "next_block_id");
  if (!parseHeader(*snapshotTip, output.snapshotTip) ||
      blocks->type != JsonType::Array ||
      blocks->array.size() > kMaximumBlocksPerPage) {
    return false;
  }
  if (nextBlockId->type == JsonType::Null) {
    output.nextBlockId.reset();
  } else {
    std::uint64_t parsedNext = 0U;
    if (!jsonUnsigned64(*nextBlockId, parsedNext))
      return false;
    output.nextBlockId = parsedNext;
  }

  output.blocks.clear();
  output.blocks.reserve(blocks->array.size());
  for (const JsonValue &block : blocks->array) {
    LocalBlockV1 parsed;
    if (!parseBlock(block, parsed))
      return false;
    output.blocks.push_back(std::move(parsed));
  }
  return true;
}

std::string encodeTipJson(const LocalHeaderV1 &tip) {
  return "{\"block_id\":" + std::to_string(tip.blockId) + ",\"block_hash\":\"" +
         tip.blockHashHex + "\",\"previous_block_hash\":\"" +
         tip.previousBlockHashHex + "\"}";
}

std::uint64_t actionId(const PalaceLezInstructionV3 &instruction) {
  return std::visit(
      [](const auto &value) -> std::uint64_t {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, PalaceLezInitializeV3>)
          return 0U;
        else
          return value.orderedActionId;
      },
      instruction.payload);
}

bool isInitialize(const PalaceLezInstructionV3 &instruction) {
  return std::holds_alternative<PalaceLezInitializeV3>(instruction.payload);
}

std::optional<std::string>
instructionPalaceIdHex(const PalaceLezInstructionV3 &instruction) {
  return std::visit(
      [](const auto &value) -> std::optional<std::string> {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, PalaceLezInitializeV3>) {
          return PalaceLezCodec::bytes32Hex(value.palaceId);
        } else {
          return std::nullopt;
        }
      },
      instruction.payload);
}

bool verifyPalaceTransaction(
    const LocalTransactionV1 &transaction,
    const PalaceLezLocalCommittedHistoryExpectationV1 &expectation,
    PalaceLezLocalCommittedActionV1 &output) {
  if (transaction.accountIdsHex.size() < 2U)
    return false;
  const PalaceLezWireInstruction decoded =
      PalaceLezCodec::decodeInstruction(transaction.instructionWords);
  if (!decoded.accepted)
    return false;
  const PalaceLezTransactionPlanV3 plan = PalaceLezCodec::buildTransaction(
      expectation.programIdHex, transaction.accountIdsHex[1],
      decoded.instruction);
  if (!plan.accepted || plan.rootAccountIdHex != expectation.rootAccountIdHex ||
      plan.accountIdsHex != transaction.accountIdsHex ||
      plan.instructionWords != transaction.instructionWords) {
    return false;
  }
  output.transactionHash = transaction.transactionHashHex;
  output.orderedActionId = actionId(decoded.instruction);
  output.instruction = decoded.instruction;
  output.accountIdsHex = transaction.accountIdsHex;
  return true;
}

} // namespace

PalaceLezLocalCommittedHistoryUpdateV1
PalaceLezLocalCommittedHistorySession::start(
    const PalaceLezLocalCommittedHistoryExpectationV1 &expectation) {
  expectation_ = {};
  outcome_ = PalaceLezLocalCommittedHistoryOutcome::Pending;
  reason_ = "not-started";
  started_ = false;
  finished_ = false;
  readyRequest_.reset();
  outstandingRequest_.reset();
  pagesScanned_ = 0U;
  blocksScanned_ = 0U;
  lastBlockId_ = 0U;
  lastBlockHashHex_.clear();
  seenBlockIds_.clear();
  seenBlockHashes_.clear();
  seenTransactionHashes_.clear();
  actions_.clear();
  targetInitializeFound_ = false;
  collectingTargetStream_ = false;
  snapshotTipJson_.clear();
  snapshotTipBlockId_ = 0U;
  snapshotTipBlockHashHex_.clear();
  snapshotTipPreviousBlockHashHex_.clear();
  result_.reset();

  PalaceLezBytes32 programId{};
  PalaceLezBytes32 rootAccountId{};
  if (!PalaceLezCodec::parseBytes32Hex(expectation.programIdHex, programId) ||
      !PalaceLezCodec::parseBytes32Hex(expectation.rootAccountIdHex,
                                       rootAccountId) ||
      !isNonzeroLowerHex(expectation.palaceIdHex, 64U) ||
      PalaceLezCodec::deriveRootPda(expectation.programIdHex) !=
          expectation.rootAccountIdHex) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "invalid-program-root-or-palace");
  }

  expectation_ = expectation;
  started_ = true;
  readyRequest_ = PalaceLezLocalCommittedHistoryRequestV1{};
  return setPending("awaiting-local-committed-history-page");
}

std::optional<PalaceLezLocalCommittedHistoryRequestV1>
PalaceLezLocalCommittedHistorySession::takeNextRequest() {
  if (!started_ || finished_ || outstandingRequest_.has_value() ||
      !readyRequest_.has_value()) {
    return std::nullopt;
  }
  outstandingRequest_ = std::move(readyRequest_);
  readyRequest_.reset();
  return outstandingRequest_;
}

PalaceLezLocalCommittedHistoryUpdateV1
PalaceLezLocalCommittedHistorySession::acceptPage(const std::string &pageJson) {
  if (!started_)
    return {false, false, outcome_, "local-committed-history-not-started"};
  if (finished_)
    return {false, false, outcome_, "local-committed-history-finished"};
  if (!outstandingRequest_.has_value()) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "local-committed-page-without-request");
  }

  const PalaceLezLocalCommittedHistoryRequestV1 request = *outstandingRequest_;
  outstandingRequest_.reset();
  LocalPageV1 page;
  if (!parsePage(pageJson, page)) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Degraded,
                       "invalid-local-committed-page");
  }
  if (snapshotTipJson_.empty()) {
    snapshotTipBlockId_ = page.snapshotTip.blockId;
    snapshotTipBlockHashHex_ = page.snapshotTip.blockHashHex;
    snapshotTipPreviousBlockHashHex_ = page.snapshotTip.previousBlockHashHex;
    snapshotTipJson_ = encodeTipJson(page.snapshotTip);
  } else if (page.snapshotTip.blockId != snapshotTipBlockId_ ||
             page.snapshotTip.blockHashHex != snapshotTipBlockHashHex_ ||
             page.snapshotTip.previousBlockHashHex !=
                 snapshotTipPreviousBlockHashHex_ ||
             request.expectedTipJson != snapshotTipJson_) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "local-committed-snapshot-mismatch");
  }
  if (page.blocks.size() > kMaximumBlocks - blocksScanned_)
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Degraded,
                       "local-committed-block-window-exhausted");

  std::size_t pageTransactionCount = 0U;
  std::size_t pageAccountCount = 0U;
  std::size_t pageInstructionWordCount = 0U;
  for (std::size_t index = 0U; index < page.blocks.size(); ++index) {
    const LocalBlockV1 &block = page.blocks[index];
    if (block.header.blockId > snapshotTipBlockId_)
      return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                         "local-committed-block-exceeds-snapshot");
    if (index == 0U) {
      if (request.startBlockId != 0U &&
          block.header.blockId != request.startBlockId) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "local-committed-cursor-mismatch");
      }
      if (!lastBlockHashHex_.empty() &&
          (lastBlockId_ == std::numeric_limits<std::uint64_t>::max() ||
           block.header.blockId != lastBlockId_ + 1U ||
           block.header.previousBlockHashHex != lastBlockHashHex_)) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "local-committed-page-chain-mismatch");
      }
    } else {
      const LocalBlockV1 &previous = page.blocks[index - 1U];
      if (previous.header.blockId ==
              std::numeric_limits<std::uint64_t>::max() ||
          block.header.blockId != previous.header.blockId + 1U ||
          block.header.previousBlockHashHex != previous.header.blockHashHex) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "local-committed-page-chain-mismatch");
      }
    }
    if (!seenBlockIds_.insert(block.header.blockId).second ||
        !seenBlockHashes_.insert(block.header.blockHashHex).second) {
      return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                         "duplicate-local-committed-block");
    }
    for (const LocalTransactionV1 &transaction : block.publicTransactions) {
      if (++pageTransactionCount > kMaximumTransactionsPerPage ||
          (pageAccountCount += transaction.accountIdsHex.size()) >
              kMaximumAccountsPerPage ||
          (pageInstructionWordCount += transaction.instructionWords.size()) >
              kMaximumInstructionWordsPerPage) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Degraded,
                           "local-committed-page-bounds-exceeded");
      }
      if (!seenTransactionHashes_.insert(transaction.transactionHashHex)
               .second) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "duplicate-local-committed-transaction");
      }
      if (transaction.programIdHex != expectation_.programIdHex ||
          transaction.accountIdsHex.empty() ||
          transaction.accountIdsHex.front() != expectation_.rootAccountIdHex) {
        continue;
      }
      if (actions_.size() >= kMaximumActions)
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Degraded,
                           "local-committed-action-window-exhausted");
      PalaceLezLocalCommittedActionV1 action;
      if (!verifyPalaceTransaction(transaction, expectation_, action)) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "local-committed-transaction-mismatch");
      }
      if (isInitialize(action.instruction)) {
        const std::optional<std::string> palaceIdHex =
            instructionPalaceIdHex(action.instruction);
        if (!palaceIdHex.has_value()) {
          return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                             "local-committed-initialize-palace-missing");
        }
        collectingTargetStream_ = false;
        if (*palaceIdHex != expectation_.palaceIdHex)
          continue;
        actions_.clear();
        targetInitializeFound_ = true;
        collectingTargetStream_ = true;
      } else if (!collectingTargetStream_) {
        continue;
      }
      actions_.push_back(std::move(action));
    }
  }

  ++pagesScanned_;
  blocksScanned_ += page.blocks.size();
  if (pagesScanned_ > kMaximumPages)
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Degraded,
                       "local-committed-page-window-exhausted");
  if (!page.blocks.empty()) {
    const LocalHeaderV1 &last = page.blocks.back().header;
    lastBlockId_ = last.blockId;
    lastBlockHashHex_ = last.blockHashHex;
  }

  if (page.nextBlockId.has_value()) {
    if (page.blocks.empty() ||
        page.blocks.back().header.blockId ==
            std::numeric_limits<std::uint64_t>::max() ||
        *page.nextBlockId != page.blocks.back().header.blockId + 1U ||
        *page.nextBlockId > snapshotTipBlockId_) {
      return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                         "local-committed-next-cursor-mismatch");
    }
    readyRequest_ = PalaceLezLocalCommittedHistoryRequestV1{
        *page.nextBlockId,
        snapshotTipJson_,
    };
    return setPending("awaiting-local-committed-history-page");
  }

  if (!page.blocks.empty() &&
      (page.blocks.back().header.blockId != snapshotTipBlockId_ ||
       page.blocks.back().header.blockHashHex != snapshotTipBlockHashHex_ ||
       page.blocks.back().header.previousBlockHashHex !=
           snapshotTipPreviousBlockHashHex_)) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "local-committed-snapshot-tip-mismatch");
  }
  if (page.blocks.empty() && !actions_.empty()) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "local-committed-empty-page-before-snapshot");
  }
  return completeRebuild();
}

PalaceLezLocalCommittedHistoryOutcome
PalaceLezLocalCommittedHistorySession::outcome() const {
  return outcome_;
}

const std::string &PalaceLezLocalCommittedHistorySession::reason() const {
  return reason_;
}

bool PalaceLezLocalCommittedHistorySession::waitingForPage() const {
  return outstandingRequest_.has_value();
}

std::size_t PalaceLezLocalCommittedHistorySession::pagesScanned() const {
  return pagesScanned_;
}

std::size_t PalaceLezLocalCommittedHistorySession::blocksScanned() const {
  return blocksScanned_;
}

std::optional<PalaceLezLocalCommittedHistoryResultV1>
PalaceLezLocalCommittedHistorySession::rebuildResult() const {
  if (outcome_ != PalaceLezLocalCommittedHistoryOutcome::Rebuilt)
    return std::nullopt;
  return result_;
}

PalaceLezLocalCommittedHistoryUpdateV1
PalaceLezLocalCommittedHistorySession::setTerminal(
    const PalaceLezLocalCommittedHistoryOutcome outcome, std::string reason) {
  const bool changed = outcome_ != outcome || reason_ != reason;
  outcome_ = outcome;
  reason_ = std::move(reason);
  finished_ = true;
  readyRequest_.reset();
  outstandingRequest_.reset();
  if (outcome != PalaceLezLocalCommittedHistoryOutcome::Rebuilt)
    result_.reset();
  return {false, changed, outcome_, reason_};
}

PalaceLezLocalCommittedHistoryUpdateV1
PalaceLezLocalCommittedHistorySession::setPending(std::string reason) {
  const bool changed =
      outcome_ != PalaceLezLocalCommittedHistoryOutcome::Pending ||
      reason_ != reason;
  outcome_ = PalaceLezLocalCommittedHistoryOutcome::Pending;
  reason_ = std::move(reason);
  return {true, changed, outcome_, reason_};
}

PalaceLezLocalCommittedHistoryUpdateV1
PalaceLezLocalCommittedHistorySession::completeRebuild() {
  if (!targetInitializeFound_ || actions_.empty() ||
      !isInitialize(actions_.front().instruction) ||
      actions_.front().orderedActionId != 0U) {
    return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                       "local-committed-initialize-not-found");
  }

  PalaceLezLocalCommittedHistoryResultV1 rebuilt;
  rebuilt.actions = actions_;
  std::set<std::string> accountIds;
  std::uint64_t expectedActionId = 0U;
  for (std::size_t index = 0U; index < rebuilt.actions.size(); ++index) {
    const PalaceLezLocalCommittedActionV1 &action = rebuilt.actions[index];
    if ((index != 0U && isInitialize(action.instruction)) ||
        action.orderedActionId != expectedActionId) {
      return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                         "local-committed-action-order-mismatch");
    }
    for (const std::string &accountId : action.accountIdsHex) {
      if (accountIds.insert(accountId).second)
        rebuilt.uniqueAccountIdsHex.push_back(accountId);
    }
    if (index + 1U < rebuilt.actions.size()) {
      if (expectedActionId == std::numeric_limits<std::uint64_t>::max()) {
        return setTerminal(PalaceLezLocalCommittedHistoryOutcome::Rejected,
                           "local-committed-action-order-mismatch");
      }
      ++expectedActionId;
    }
  }
  rebuilt.latestLocalCommittedBlockId = snapshotTipBlockId_;
  rebuilt.latestLocalCommittedBlockHashHex = snapshotTipBlockHashHex_;
  result_ = std::move(rebuilt);
  outcome_ = PalaceLezLocalCommittedHistoryOutcome::Rebuilt;
  reason_ = "local-committed-history-rebuilt";
  finished_ = true;
  readyRequest_.reset();
  outstandingRequest_.reset();
  return {true, true, outcome_, reason_};
}

} // namespace palace
