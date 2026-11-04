/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const networkLocationsModule = require("resource://devtools/client/aboutdebugging/src/modules/network-locations.js");

/**
 * Test the sidebar is updated correctly when network runtimes are added/removed.
 */

add_task(async function () {
  const { document, tab } = await openAboutDebugging();

  const noDevicesElement = document.querySelector(".qa-sidebar-no-devices");
  ok(noDevicesElement, "Sidebar shows the 'no devices' element");

  info("Add a network location");
  networkLocationsModule.addNetworkLocation("localhost:6080");

  info("Wait for 'no devices' element to disappear");
  await waitUntil(() => !document.querySelector(".qa-sidebar-no-devices"));
  ok(
    findSidebarItemByText("localhost:6080", document),
    "Found a sidebar item for localhost:6080"
  );

  info("Remove the network location");
  networkLocationsModule.removeNetworkLocation("localhost:6080");

  info("Wait for 'no devices' element to reappear");
  await waitUntil(() => document.querySelector(".qa-sidebar-no-devices"));
  ok(
    !findSidebarItemByText("localhost:6080", document),
    "Sidebar item for localhost:6080 removed"
  );

  await removeTab(tab);
});
