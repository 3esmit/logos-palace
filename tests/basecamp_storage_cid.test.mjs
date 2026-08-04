import assert from "node:assert/strict";
import test from "node:test";
import {
  canonicalStorageCidSha256,
} from "./basecamp_storage_cid.mjs";

const base32Alphabet = "abcdefghijklmnopqrstuvwxyz234567";
const base58Alphabet =
  "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

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

function encodeBase58(bytes) {
  let value = 0n;
  for (const byte of bytes) value = value * 256n + BigInt(byte);
  let encoded = "";
  while (value > 0n) {
    encoded = base58Alphabet[Number(value % 58n)] + encoded;
    value /= 58n;
  }
  for (const byte of bytes) {
    if (byte !== 0) break;
    encoded = base58Alphabet[0] + encoded;
  }
  return encoded;
}

const digest = "ab".repeat(32);
const cidBytes = Buffer.concat([
  Buffer.from([0x01, 0x55, 0x12, 0x20]),
  Buffer.from(digest, "hex"),
]);

test("accepts canonical base32 and base58btc CIDv1 SHA-256", () => {
  assert.equal(
    canonicalStorageCidSha256(`b${encodeBase32(cidBytes)}`),
    digest,
  );
  assert.equal(
    canonicalStorageCidSha256(`z${encodeBase58(cidBytes)}`),
    digest,
  );
});

test("rejects noncanonical or non-SHA-256 Storage CIDs", () => {
  for (const cid of [
    null,
    "",
    `B${encodeBase32(cidBytes)}`,
    `b${encodeBase32(cidBytes)}a`,
    `b${encodeBase32(Buffer.from([0x01, 0x55, 0x13, 0x20]))}`,
    `b${encodeBase32(Buffer.concat([
      Buffer.from([0x01, 0x55, 0x12, 0x1f]),
      Buffer.alloc(31),
    ]))}`,
    `z1${encodeBase58(cidBytes)}`,
    `b${"a".repeat(128)}`,
  ]) {
    assert.throws(() => canonicalStorageCidSha256(cid));
  }
});
