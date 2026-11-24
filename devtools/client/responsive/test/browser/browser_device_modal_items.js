/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the content of device items in the modal.

const TEST_URL = "data:text/html;charset=utf-8,";
const {
  parseUserAgent,
} = require("resource://devtools/client/responsive/utils/ua.js");

addRDMTask(
  TEST_URL,
  async function ({ ui }) {
    const { toolWindow } = ui;
    const { store, document } = toolWindow;

    await openDeviceModal(ui);

    const { devices } = store.getState();

    ok(devices.types.length, "We have some device types");

    for (const type of devices.types) {
      const list = devices[type];

      const header = document.querySelector(
        `.device-type-${type} .device-header`
      );

      if (type == "custom") {
        // we don't have custom devices, so there shouldn't be a header for it.
        is(list.length, 0, `We don't have any custom devices`);
        ok(!header, `There's no header for "custom"`);
        continue;
      }

      ok(list.length, `We have ${type} devices`);
      ok(header, `There's a header for ${type} devices`);

      is(
        header?.textContent,
        getDeviceString(type),
        `Got expected text for ${type} header`
      );

      for (const item of list) {
        info(`Check the element for ${item.name} on the modal`);

        const targetEl = findDeviceLabel(item.name, document);
        ok(targetEl, "The element for the device is on the modal");

        const { browser, os } = parseUserAgent(item.userAgent);
        const browserEl = targetEl.querySelector(".device-browser");
        if (browser) {
          ok(browserEl, "The element for the browser is in the device element");
          const expectedClassName = browser.name.toLowerCase();
          ok(
            browserEl.classList.contains(expectedClassName),
            `The browser element contains .${expectedClassName}`
          );
        } else {
          ok(
            !browserEl,
            "The element for the browser is not in the device element"
          );
        }

        const osEl = targetEl.querySelector(".device-os");
        if (os) {
          ok(osEl, "The element for the os is in the device element");
          const expectedText = os.version
            ? `${os.name} ${os.version}`
            : os.name;
          is(
            osEl.textContent,
            expectedText,
            "The text in os element is correct"
          );
        } else {
          ok(!osEl, "The element for the os is not in the device element");
        }
      }
    }
  },
  { waitForDeviceList: true }
);

function findDeviceLabel(deviceName, document) {
  const deviceNameEls = document.querySelectorAll(".device-name");
  const targetEl = [...deviceNameEls].find(el => el.textContent === deviceName);
  return targetEl ? targetEl.closest(".device-label") : null;
}
