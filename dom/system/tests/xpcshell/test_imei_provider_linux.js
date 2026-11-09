/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  DBusMockTestUtils: "resource://testing-common/DBusMockTestUtils.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
});

const MM_BUS_NAME = "org.freedesktop.ModemManager1";
const MM_OBJECT_PATH = "/org/freedesktop/ModemManager1";
const MM_MODEM_PATH = `${MM_OBJECT_PATH}/Modem/0`;
const MM_MODEM_INTERFACE = "org.freedesktop.ModemManager1.Modem";

let gModemManagerMock = null;

async function isDBusMockAvailable() {
  try {
    for (const command of ["sh", "dbus-daemon", "gdbus"]) {
      await lazy.Subprocess.pathSearch(command);
    }
    const proc = await lazy.Subprocess.call({
      command: await lazy.Subprocess.pathSearch("python3"),
      arguments: ["-c", "import dbusmock"],
    });
    return (await proc.wait()).exitCode == 0;
  } catch (e) {
    return false;
  }
}

async function addModem(aIdentifierVariant) {
  const { exitCode, stdout } = await gModemManagerMock.mockCall("AddObject", [
    MM_MODEM_PATH,
    MM_MODEM_INTERFACE,
    `{'EquipmentIdentifier': ${aIdentifierVariant}}`,
    "@a(ssss) []",
  ]);
  Assert.equal(exitCode, 0, `Added fake modem: ${stdout}`);
}

async function resetModemManagerMock() {
  const { exitCode } = await gModemManagerMock.mockCall("Reset");
  Assert.equal(exitCode, 0, "Reset dbusmock");
}

add_setup(async function () {
  if (!(await isDBusMockAvailable())) {
    throw new Error("python-dbusmock is not available");
  }

  // The manifest points the system bus at this address, because the GTK app
  // shell connects to the system bus before the test starts.
  const address = await lazy.DBusMockTestUtils.startBus({
    system: true,
    address: Services.env.get("DBUS_SYSTEM_BUS_ADDRESS"),
  });
  gModemManagerMock = await lazy.DBusMockTestUtils.startMock({
    address,
    system: true,
    busName: MM_BUS_NAME,
    objectPath: MM_OBJECT_PATH,
    interfaceName: MM_BUS_NAME,
  });
  registerCleanupFunction(() => gModemManagerMock.stop());
});

add_task(function test_isValidImei() {
  Assert.ok(isValidImei(TEST_IMEI), "Valid IMEI");
  Assert.ok(!isValidImei("490154203237517"), "Bad check digit");
  Assert.ok(!isValidImei("49015420323751"), "Too short");
  Assert.ok(!isValidImei(""), "Empty");
});

add_task(async function test_modemmanager_imei() {
  await addModem(`<'${TEST_IMEI}'>`);
  const imei = await getImeiProvider().imei;
  Assert.equal(imei, TEST_IMEI, "IMEI read from the fake modem");
  await resetModemManagerMock();
});

add_task(async function test_modemmanager_wrong_type() {
  await addModem("<uint32 42>");
  await Assert.rejects(
    getImeiProvider().imei,
    e => e.result == Cr.NS_ERROR_ILLEGAL_VALUE,
    "Non-string EquipmentIdentifier is rejected"
  );
  await resetModemManagerMock();
});

add_task(async function test_modemmanager_no_modem() {
  await Assert.rejects(
    getImeiProvider().imei,
    e => e.result == Cr.NS_ERROR_NOT_IMPLEMENTED,
    "No modem object is rejected"
  );
});
