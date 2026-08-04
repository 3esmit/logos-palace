const base32Alphabet = "abcdefghijklmnopqrstuvwxyz234567";
const base58Alphabet =
  "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

function decodeBase32(value) {
  let accumulator = 0;
  let bitCount = 0;
  const bytes = [];
  for (const character of value) {
    const digit = base32Alphabet.indexOf(character);
    if (digit < 0) throw new Error("CID contains non-base32 character");
    accumulator = (accumulator << 5) | digit;
    bitCount += 5;
    while (bitCount >= 8) {
      bitCount -= 8;
      bytes.push((accumulator >> bitCount) & 0xff);
      accumulator &= bitCount === 0 ? 0 : (1 << bitCount) - 1;
    }
  }
  if (bitCount > 0 && accumulator !== 0) {
    throw new Error("CID has non-canonical base32 tail bits");
  }
  return Buffer.from(bytes);
}

function encodeBase32(bytes) {
  let accumulator = 0;
  let bitCount = 0;
  let encoded = "";
  for (const byte of bytes) {
    accumulator = (accumulator << 8) | byte;
    bitCount += 8;
    while (bitCount >= 5) {
      bitCount -= 5;
      encoded += base32Alphabet[(accumulator >> bitCount) & 0x1f];
      accumulator &= bitCount === 0 ? 0 : (1 << bitCount) - 1;
    }
  }
  if (bitCount > 0) {
    encoded += base32Alphabet[
      (accumulator << (5 - bitCount)) & 0x1f
    ];
  }
  return encoded;
}

function decodeBase58(value) {
  if (!value) throw new Error("CID base58 payload is empty");
  let decoded = 0n;
  for (const character of value) {
    const digit = base58Alphabet.indexOf(character);
    if (digit < 0) throw new Error("CID contains non-base58 character");
    decoded = decoded * 58n + BigInt(digit);
  }
  let hex = decoded.toString(16);
  if (hex.length % 2 !== 0) hex = `0${hex}`;
  const payload =
    decoded === 0n ? Buffer.alloc(0) : Buffer.from(hex, "hex");
  let leadingZeros = 0;
  while (
    leadingZeros < value.length
    && value[leadingZeros] === base58Alphabet[0]
  ) {
    leadingZeros += 1;
  }
  return Buffer.concat([Buffer.alloc(leadingZeros), payload]);
}

function encodeBase58(bytes) {
  let value = 0n;
  for (const byte of bytes) value = value * 256n + BigInt(byte);
  let encoded = "";
  while (value > 0n) {
    const digit = Number(value % 58n);
    encoded = base58Alphabet[digit] + encoded;
    value /= 58n;
  }
  let leadingZeros = 0;
  while (leadingZeros < bytes.length && bytes[leadingZeros] === 0) {
    encoded = base58Alphabet[0] + encoded;
    leadingZeros += 1;
  }
  return encoded;
}

function readVarint(bytes, cursor) {
  let value = 0;
  let multiplier = 1;
  const start = cursor.offset;
  while (cursor.offset < bytes.length) {
    const byte = bytes[cursor.offset++];
    value += (byte & 0x7f) * multiplier;
    if (!Number.isSafeInteger(value)) throw new Error("CID varint overflow");
    if ((byte & 0x80) === 0) {
      if (cursor.offset - start > 1 && byte === 0) {
        throw new Error("CID varint is not canonical");
      }
      return value;
    }
    multiplier *= 128;
  }
  throw new Error("CID varint is truncated");
}

export function canonicalStorageCidSha256(cid) {
  let bytes;
  if (typeof cid !== "string") {
    throw new Error("CID is not a string");
  }
  if (cid.length < 10 || cid.length > 128) {
    throw new Error("CID length is outside the accepted boundary");
  }
  if (/^b[a-z2-7]+$/.test(cid)) {
    bytes = decodeBase32(cid.slice(1));
    if (`b${encodeBase32(bytes)}` !== cid) {
      throw new Error(`CID is not canonical base32: ${cid}`);
    }
  } else if (/^z[1-9A-HJ-NP-Za-km-z]+$/.test(cid)) {
    bytes = decodeBase58(cid.slice(1));
    if (`z${encodeBase58(bytes)}` !== cid) {
      throw new Error(`CID is not canonical base58btc: ${cid}`);
    }
  } else {
    throw new Error(`CID is not canonical CIDv1 base32/base58btc: ${cid}`);
  }
  const cursor = { offset: 0 };
  const version = readVarint(bytes, cursor);
  const codec = readVarint(bytes, cursor);
  const multihash = readVarint(bytes, cursor);
  const digestLength = readVarint(bytes, cursor);
  if (
    version !== 1
    || codec === 0
    || multihash !== 0x12
    || digestLength !== 32
    || cursor.offset + digestLength !== bytes.length
  ) {
    throw new Error(`CID has unexpected envelope: ${cid}`);
  }
  return bytes.subarray(cursor.offset).toString("hex");
}
