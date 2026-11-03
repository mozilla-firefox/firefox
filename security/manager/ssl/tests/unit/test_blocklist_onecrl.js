"use strict";

do_get_profile();

const { Utils } = ChromeUtils.importESModule(
  "resource://services-settings/Utils.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { RemoteSecuritySettings } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/RemoteSecuritySettings.sys.mjs"
);
const { OneCRLBlocklistClient } = RemoteSecuritySettings.init();

add_task(async function test_uses_a_custom_signer() {
  Assert.notEqual(
    OneCRLBlocklistClient.signerName,
    RemoteSettings("not-specified").signerName
  );
});

add_task(async function test_has_initial_dump() {
  Assert.ok(
    await Utils.hasLocalDump(
      OneCRLBlocklistClient.bucketName,
      OneCRLBlocklistClient.collectionName
    )
  );
});

add_task(async function test_default_jexl_filter_is_used() {
  Assert.deepEqual(
    OneCRLBlocklistClient.filterCreator,
    RemoteSettings("not-specified").filterCreator
  );
});

add_task(
  async function test_revocations_are_updated_on_sync_with_cert_storage() {
    const certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
      Ci.nsICertStorage
    );
    const has_revocations = () =>
      new Promise(resolve => {
        certStorage.hasPriorData(
          Ci.nsICertStorage.DATA_TYPE_REVOCATION,
          (rv, hasPriorData) => {
            if (rv == Cr.NS_OK) {
              return resolve(hasPriorData);
            }
            return resolve(false);
          }
        );
      });

    Assert.ok(!(await has_revocations()));

    await OneCRLBlocklistClient.emit("sync", {
      data: {
        current: [],
        created: [
          {
            issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
            serialNumber: "a0X7/7DlTaedpgrIJg25iBPOkIM=",
          },
        ],
        updated: [],
        deleted: [],
      },
    });

    Assert.ok(await has_revocations());
  }
);

add_task(async function test_updated_entry() {
  // Revoke a particular issuer/serial number.
  await OneCRLBlocklistClient.emit("sync", {
    data: {
      current: [],
      created: [
        {
          issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
          serialNumber: "a0X7/7DlTaedpgrIJg25iBPOkIM=",
        },
      ],
      updated: [],
      deleted: [],
    },
  });
  const certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );
  let issuerArray = [
    0x30, 0x12, 0x31, 0x10, 0x30, 0xe, 0x6, 0x3, 0x55, 0x4, 0x3, 0xc, 0x7, 0x54,
    0x65, 0x73, 0x74, 0x20, 0x43, 0x41,
  ];
  let serialArray = [
    0x6b, 0x45, 0xfb, 0xff, 0xb0, 0xe5, 0x4d, 0xa7, 0x9d, 0xa6, 0xa, 0xc8, 0x26,
    0xd, 0xb9, 0x88, 0x13, 0xce, 0x90, 0x83,
  ];
  let revocationState = certStorage.getRevocationState(
    issuerArray,
    serialArray,
    [],
    []
  );
  Assert.equal(revocationState, Ci.nsICertStorage.STATE_ENFORCE);

  // Update the revocation to be a different serial number; the original
  // (issuer, serial) pair should now not be revoked.
  await OneCRLBlocklistClient.emit("sync", {
    data: {
      current: [],
      created: [],
      updated: [
        {
          old: {
            issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
            serialNumber: "a0X7/7DlTaedpgrIJg25iBPOkIM=",
          },
          new: {
            issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
            serialNumber: "ALtF+/+w5U0=",
          },
        },
      ],
      deleted: [],
    },
  });
  let oldRevocationState = certStorage.getRevocationState(
    issuerArray,
    serialArray,
    [],
    []
  );
  Assert.equal(oldRevocationState, Ci.nsICertStorage.STATE_UNSET);

  let newSerialArray = [0x00, 0xbb, 0x45, 0xfb, 0xff, 0xb0, 0xe5, 0x4d];
  let newRevocationState = certStorage.getRevocationState(
    issuerArray,
    newSerialArray,
    [],
    []
  );
  Assert.equal(newRevocationState, Ci.nsICertStorage.STATE_ENFORCE);
});
