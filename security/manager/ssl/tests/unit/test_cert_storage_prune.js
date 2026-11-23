/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// This file tests that cert_storage removes entries that no longer affect any
// lookup: both entries that earlier versions left in the database, and entries
// that would otherwise become obsolete as the data is updated. None of this is
// visible through nsICertStorage, so the test reads and writes the database
// file directly (see head_cert_storage_db.js).

// Entries in the database before cert_storage opens it, and whether
// cert_storage should keep them.
const PREEXISTING = [
  {
    description: "revocation data type marker",
    key: bytes("datatype", [Ci.nsICertStorage.DATA_TYPE_REVOCATION]),
    value: boolValue(true),
    kept: true,
  },
  {
    description: "enforced issuer:serial revocation",
    key: bytes("is", "issuer 1", "serial 1"),
    value: i64Value(Ci.nsICertStorage.STATE_ENFORCE),
    kept: true,
  },
  {
    description: "unset issuer:serial revocation",
    key: bytes("is", "issuer 1", "serial 2"),
    value: i64Value(Ci.nsICertStorage.STATE_UNSET),
    kept: false,
  },
  {
    description: "enforced subject:pubKeyHash revocation",
    key: bytes("spk", "subject 1", "pubKeyHash 1"),
    value: i64Value(Ci.nsICertStorage.STATE_ENFORCE),
    kept: true,
  },
  {
    description: "unset subject:pubKeyHash revocation",
    key: bytes("spk", "subject 1", "pubKeyHash 2"),
    value: i64Value(Ci.nsICertStorage.STATE_UNSET),
    kept: false,
  },
  {
    description: "enrolled CRLite enrollment entry",
    key: bytes("crlite", "subject 2", "pubKeyHash 3"),
    value: i64Value(Ci.nsICertStorage.STATE_ENFORCE),
    kept: false,
  },
  {
    description: "unenrolled CRLite enrollment entry",
    key: bytes("crlite", "subject 3", "pubKeyHash 4"),
    value: i64Value(Ci.nsICertStorage.STATE_UNSET),
    kept: false,
  },
  // DATA_TYPE_CRLITE, DATA_TYPE_CRLITE_FILTER_FULL, and
  // DATA_TYPE_CRLITE_FILTER_INCREMENTAL.
  {
    description: "CRLite data type marker",
    key: bytes("datatype", [3]),
    value: boolValue(true),
    kept: false,
  },
  {
    description: "full CRLite filter data type marker",
    key: bytes("datatype", [4]),
    value: u64Value(1650000000),
    kept: false,
  },
  {
    description: "incremental CRLite filter data type marker",
    key: bytes("datatype", [5]),
    value: u64Value(1650000000),
    kept: false,
  },
  {
    description: "time of the last CRLite update",
    key: bytes("last_crlite_update"),
    value: u64Value(1650000000),
    kept: true,
  },
  {
    description: "certificate data type marker",
    key: bytes("datatype", [Ci.nsICertStorage.DATA_TYPE_CERTIFICATE]),
    value: boolValue(true),
    kept: true,
  },
  {
    description: "empty list of certificates with a subject",
    key: bytes("subject", "subject 4"),
    value: blobValue([]),
    kept: false,
  },
  {
    description: "non-empty list of certificates with a subject",
    key: bytes("subject", "subject 5"),
    value: blobValue(new Array(32).fill(1)),
    kept: true,
  },
];

class IssuerAndSerialRevocationState {
  constructor(issuer, serial, state) {
    this.issuer = btoa(issuer);
    this.serial = btoa(serial);
    this.state = state;
  }
}
IssuerAndSerialRevocationState.prototype.QueryInterface =
  ChromeUtils.generateQI(["nsIIssuerAndSerialRevocationState"]);

class CertInfo {
  constructor(cert, subject) {
    this.cert = btoa(cert);
    this.subject = btoa(subject);
    this.trust = Ci.nsICertStorage.TRUST_INHERIT;
  }
}
CertInfo.prototype.QueryInterface = ChromeUtils.generateQI(["nsICertInfo"]);

let gCertStorage;

async function addCerts(certInfos) {
  let result = await new Promise(resolve => {
    gCertStorage.addCerts(certInfos, resolve);
  });
  Assert.equal(result, Cr.NS_OK, "addCerts should succeed");
}

async function removeCertsByHashes(hashesBase64) {
  let result = await new Promise(resolve => {
    gCertStorage.removeCertsByHashes(hashesBase64, resolve);
  });
  Assert.equal(result, Cr.NS_OK, "removeCertsByHashes should succeed");
}

async function setRevocations(revocations) {
  let result = await new Promise(resolve => {
    gCertStorage.setRevocations(revocations, resolve);
  });
  Assert.equal(result, Cr.NS_OK, "setRevocations should succeed");
}

function getRevocationState(issuer, serial) {
  return gCertStorage.getRevocationState(
    stringToArray(issuer),
    stringToArray(serial),
    [],
    []
  );
}

add_setup(async function () {
  await writeDatabase(PREEXISTING.map(({ key, value }) => [key, value]));
});

add_task(async function test_prune_preexisting() {
  // Opening the database prunes obsolete entries.
  gCertStorage = await openDatabase();

  let entries = await readDatabase();
  Assert.equal(
    entries.get(hex(PRUNED_MARKER.key)),
    hex(PRUNED_MARKER.value),
    "should mark the database as pruned"
  );
  for (let { description, key, value, kept } of PREEXISTING) {
    if (kept) {
      Assert.equal(
        entries.get(hex(key)),
        hex(value),
        `should keep the ${description}`
      );
    } else {
      Assert.ok(!entries.has(hex(key)), `should remove the ${description}`);
    }
  }
  Assert.equal(
    entries.size,
    PREEXISTING.filter(({ kept }) => kept).length + 1,
    "should have no other entries"
  );

  Assert.equal(
    getRevocationState("issuer 1", "serial 1"),
    Ci.nsICertStorage.STATE_ENFORCE,
    "enforced revocation should still be enforced"
  );
  Assert.equal(
    getRevocationState("issuer 1", "serial 2"),
    Ci.nsICertStorage.STATE_UNSET,
    "pruned revocation should still be unset"
  );
});

add_task(async function test_unset_revocation_is_deleted() {
  let key = hex(bytes("is", "issuer 2", "serial 1"));
  await setRevocations([
    new IssuerAndSerialRevocationState(
      "issuer 2",
      "serial 1",
      Ci.nsICertStorage.STATE_ENFORCE
    ),
  ]);
  Assert.equal(
    (await readDatabase()).get(key),
    hex(i64Value(Ci.nsICertStorage.STATE_ENFORCE)),
    "should store an enforced revocation"
  );

  // Unsetting a revocation twice in one batch, as happens when OneCRL has
  // duplicate records for an entry, should succeed.
  let unset = new IssuerAndSerialRevocationState(
    "issuer 2",
    "serial 1",
    Ci.nsICertStorage.STATE_UNSET
  );
  await setRevocations([unset, unset]);
  Assert.ok(
    !(await readDatabase()).has(key),
    "should delete an unset revocation"
  );
  Assert.equal(
    getRevocationState("issuer 2", "serial 1"),
    Ci.nsICertStorage.STATE_UNSET,
    "deleted revocation should be unset"
  );

  // An updated OneCRL record is unset and then enforced in the same batch.
  await setRevocations([
    unset,
    new IssuerAndSerialRevocationState(
      "issuer 2",
      "serial 1",
      Ci.nsICertStorage.STATE_ENFORCE
    ),
  ]);
  Assert.equal(
    getRevocationState("issuer 2", "serial 1"),
    Ci.nsICertStorage.STATE_ENFORCE,
    "revocation that is unset and then enforced should be enforced"
  );
});

add_task(async function test_empty_subject_list_is_deleted() {
  let key = hex(bytes("subject", "prune subject"));
  await addCerts([
    new CertInfo("prune certificate bytes 1", "prune subject"),
    new CertInfo("prune certificate bytes 2", "prune subject"),
  ]);
  Assert.ok(
    (await readDatabase()).has(key),
    "should store a list of certificates with the subject"
  );

  // echo -n "prune certificate bytes 1" | sha256sum | xxd -r -p | base64
  await removeCertsByHashes(["Uqi+kR08J/6wqwp+56GZymIuYXPmPr1HAuiFO55+3OE="]);
  Assert.ok(
    (await readDatabase()).has(key),
    "should keep the list while it has a certificate"
  );
  Assert.deepEqual(
    gCertStorage
      .findCertsBySubject(stringToArray("prune subject"))
      .map(arrayToString),
    ["prune certificate bytes 2"],
    "should find the remaining certificate"
  );

  // echo -n "prune certificate bytes 2" | sha256sum | xxd -r -p | base64
  await removeCertsByHashes(["lNHhobjEsGA8x45mM41YT8fiSNcvqui5btimDKUNnhg="]);
  Assert.ok(
    !(await readDatabase()).has(key),
    "should delete the list when its last certificate is removed"
  );
  Assert.equal(
    gCertStorage.findCertsBySubject(stringToArray("prune subject")).length,
    0,
    "should find no certificates with the subject"
  );
});
