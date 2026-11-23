/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Helpers for tests that read and write cert_storage's database file directly,
// in the format of rkv's "safe mode" backend.

/* exported PRUNED_MARKER, blobValue, boolValue, bytes, hex, i64Value,
   openDatabase, readDatabase, u64Value, writeDatabase */

const profile = do_get_profile();
const dbDirectory = PathUtils.join(profile.path, "security_state");
const dbPath = PathUtils.join(dbDirectory, "data.safe.bin");

// rkv value type tags (see rkv's value.rs).
const TYPE_BOOL = 1;
const TYPE_U64 = 2;
const TYPE_I64 = 3;
const TYPE_BLOB = 9;

function bytes(...parts) {
  let encoder = new TextEncoder();
  let arrays = parts.map(part =>
    typeof part == "string" ? encoder.encode(part) : new Uint8Array(part)
  );
  let result = new Uint8Array(arrays.reduce((n, a) => n + a.length, 0));
  let offset = 0;
  for (let a of arrays) {
    result.set(a, offset);
    offset += a.length;
  }
  return result;
}

function u64(n) {
  let result = new Uint8Array(8);
  new DataView(result.buffer).setBigUint64(0, BigInt(n), true);
  return result;
}

function boolValue(b) {
  return bytes([TYPE_BOOL, b ? 1 : 0]);
}

function u64Value(n) {
  return bytes([TYPE_U64], u64(n));
}

function i64Value(n) {
  let result = new Uint8Array(9);
  result[0] = TYPE_I64;
  new DataView(result.buffer).setBigInt64(1, BigInt(n), true);
  return result;
}

function blobValue(array) {
  return bytes([TYPE_BLOB], u64(array.length), array);
}

function hex(array) {
  return Array.from(array, b => b.toString(16).padStart(2, "0")).join("");
}

// Serializes entries (an array of [key, value] pairs of Uint8Arrays) as a
// safe mode database containing the single store "cert_storage". The format
// is bincode: a map from optional store names to stores, each of which is a
// u32 of flags and a map from keys to values. Lengths are u64s.
function serializeDatabase(entries) {
  let parts = [u64(1), [1], u64("cert_storage".length), "cert_storage"];
  parts.push([0, 0, 0, 0], u64(entries.length));
  for (let [key, value] of entries) {
    parts.push(u64(key.length), key, u64(value.length), value);
  }
  return bytes(...parts);
}

// Parses a database written by serializeDatabase or by cert_storage, and
// returns a map from hex encoded keys to hex encoded values.
function parseDatabase(data) {
  let view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let offset = 0;
  let readU64 = () => {
    let n = Number(view.getBigUint64(offset, true));
    offset += 8;
    return n;
  };
  let readBytes = () => {
    let length = readU64();
    let result = data.subarray(offset, offset + length);
    offset += length;
    return result;
  };
  Assert.equal(readU64(), 1, "database should have one store");
  Assert.equal(data[offset++], 1, "store should have a name");
  Assert.equal(
    new TextDecoder().decode(readBytes()),
    "cert_storage",
    "store should be cert_storage"
  );
  offset += 4; // flags
  let entries = new Map();
  let count = readU64();
  for (let i = 0; i < count; i++) {
    let key = readBytes();
    entries.set(hex(key), hex(readBytes()));
  }
  Assert.equal(offset, data.length, "database should have no trailing bytes");
  return entries;
}

async function readDatabase() {
  return parseDatabase(await IOUtils.read(dbPath));
}

// Writes entries (an array of [key, value] pairs of Uint8Arrays) as the
// database that cert_storage will open.
async function writeDatabase(entries) {
  await IOUtils.makeDirectory(dbDirectory, { ignoreExisting: true });
  await IOUtils.write(dbPath, serializeDatabase(entries));
}

// Opens the database (cert_storage opens it on first use), and returns the
// nsICertStorage service.
async function openDatabase() {
  let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );
  await new Promise(resolve => {
    certStorage.hasPriorData(Ci.nsICertStorage.DATA_TYPE_REVOCATION, rv => {
      Assert.equal(rv, Cr.NS_OK, "hasPriorData should succeed");
      resolve();
    });
  });
  return certStorage;
}

// The entry that cert_storage stores once it has removed obsolete entries
// from the database (see PRUNED_MARKER_KEY in cert_storage).
const PRUNED_MARKER = {
  key: bytes("bug-2095295-cleanup"),
  value: boolValue(true),
};
