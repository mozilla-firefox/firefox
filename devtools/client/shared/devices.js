/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyGetter(this, "L10N", () => {
  return new Localization(["devtools/client/device.ftl"], true);
});

const DEVICE_TYPE_L10N_IDS = {
  phones: "device-type-phones",
  tablets: "device-type-tablets",
  laptops: "device-type-laptops",
  televisions: "device-type-televisions",
  consoles: "device-type-consoles",
  watches: "device-type-watches",
  custom: "device-type-custom",
};

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

loader.lazyRequireGetter(
  this,
  "asyncStorage",
  "resource://devtools/shared/async-storage.js"
);

const LOCAL_DEVICES = "devtools.devices.local";

/* This is a catalog of common web-enabled devices and their properties,
 * intended for (mobile) device emulation.
 *
 * The properties of a device are:
 * - name: brand and model(s).
 * - width: viewport width.
 * - height: viewport height.
 * - pixelRatio: ratio from viewport to physical screen pixels.
 * - userAgent: UA string of the device's browser.
 * - touch: whether it has a touch screen.
 * - os: default OS, such as "ios", "fxos", "android".
 *
 * The device types are:
 *   ["phones", "tablets", "laptops", "televisions", "consoles", "watches"].
 *
 * To propose new devices for the shared catalog, see
 * https://firefox-source-docs.mozilla.org/devtools/responsive/devices.html#adding-and-removing-devices.
 *
 * You can easily add more devices to this catalog from your own code (e.g. an
 * addon) like so:
 *
 *   var myPhone = { name: "My Phone", ... };
 *   require("devtools/client/shared/devices").addDevice(myPhone, "phones");
 */

// Local devices catalog that addons can add to.
let localDevices;
let localDevicesLoaded = false;

/**
 * Load local devices from storage.
 */
async function loadLocalDevices() {
  if (localDevicesLoaded) {
    return;
  }
  let devicesJSON = await asyncStorage.getItem(LOCAL_DEVICES);
  if (!devicesJSON) {
    devicesJSON = "{}";
  }
  localDevices = JSON.parse(devicesJSON);
  localDevicesLoaded = true;
}

/**
 * Add a device to the local catalog.
 * Returns `true` if the device is added, `false` otherwise.
 */
async function addDevice(device, type = "phones") {
  await loadLocalDevices();
  let list = localDevices[type];
  if (!list) {
    list = localDevices[type] = [];
  }

  // Ensure the new device is has a unique name
  const exists = list.some(entry => entry.name == device.name);
  if (exists) {
    return false;
  }

  list.push(Object.assign({}, device));
  await asyncStorage.setItem(LOCAL_DEVICES, JSON.stringify(localDevices));

  return true;
}

/**
 * Edit a device from the local catalog.
 * Returns `true` if the device is edited, `false` otherwise.
 */
async function editDevice(oldDevice, newDevice, type = "phones") {
  await loadLocalDevices();
  const list = localDevices[type];
  if (!list) {
    return false;
  }

  const index = list.findIndex(entry => entry.name == oldDevice.name);
  if (index == -1) {
    return false;
  }

  // Replace old device info with new one
  list.splice(index, 1, newDevice);
  await asyncStorage.setItem(LOCAL_DEVICES, JSON.stringify(localDevices));

  return true;
}

/**
 * Remove a device from the local catalog.
 * Returns `true` if the device is removed, `false` otherwise.
 */
async function removeDevice(device, type = "phones") {
  await loadLocalDevices();
  const list = localDevices[type];
  if (!list) {
    return false;
  }

  const index = list.findIndex(entry => entry.name == device.name);
  if (index == -1) {
    return false;
  }

  list.splice(index, 1);
  await asyncStorage.setItem(LOCAL_DEVICES, JSON.stringify(localDevices));

  return true;
}

/**
 * Remove all local devices.  Useful to clear everything when testing.
 */
async function removeLocalDevices() {
  await asyncStorage.removeItem(LOCAL_DEVICES);
  localDevices = {};
}

/**
 * Get the complete devices catalog.
 */
async function getDevices() {
  const records = await RemoteSettings("devtools-devices").get();
  const devicesByType = new Map();
  for (const record of records) {
    const { type } = record;
    if (!devicesByType.has(type)) {
      devicesByType.set(type, []);
    }
    devicesByType.get(type).push(record);
  }

  await loadLocalDevices();
  for (const type in localDevices) {
    if (!devicesByType.has(type)) {
      devicesByType.set(type, []);
    }
    devicesByType.get(type).push(...localDevices[type]);
  }
  return devicesByType;
}

/**
 * Get the localized string for a device type.
 */
function getDeviceString(deviceType) {
  const l10nId = DEVICE_TYPE_L10N_IDS[deviceType];
  return l10nId ? L10N.formatValueSync(l10nId) : deviceType;
}

module.exports = {
  addDevice,
  editDevice,
  removeDevice,
  removeLocalDevices,
  getDevices,
  getDeviceString,
};
