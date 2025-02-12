/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_valid_hostname() {
  await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
  const connectionURL =
    "chrome://browser/content/preferences/dialogs/connection.xhtml";
  let dialog = await openAndLoadSubDialog(connectionURL);
  let dialogElement = dialog.document.getElementById("ConnectionsDialog");

  let oldNetworkProxyType = Services.prefs.getIntPref("network.proxy.type");
  registerCleanupFunction(function () {
    Services.prefs.setIntPref("network.proxy.type", oldNetworkProxyType);
    Services.prefs.clearUserPref("network.proxy.share_proxy_settings");
    for (let proxyType of ["http", "ssl", "socks"]) {
      Services.prefs.clearUserPref("network.proxy." + proxyType);
      Services.prefs.clearUserPref("network.proxy." + proxyType + "_port");
      if (proxyType == "http") {
        continue;
      }
      Services.prefs.clearUserPref("network.proxy.backup." + proxyType);
      Services.prefs.clearUserPref(
        "network.proxy.backup." + proxyType + "_port"
      );
    }
  });

  let proxyType = dialog.Preferences.get("network.proxy.type");
  let httpPref = dialog.Preferences.get("network.proxy.http");
  let httpPortPref = dialog.Preferences.get("network.proxy.http_port");
  proxyType.value = 1;
  httpPortPref.value = 1234;

  httpPref.value = "bad://hostname";
  let beforeAcceptCalled = BrowserTestUtils.waitForEvent(
    dialogElement,
    "beforeaccept"
  );
  dialogElement.acceptDialog();
  let event = await beforeAcceptCalled;
  Assert.ok(event.defaultPrevented, "The dialog was not accepted");

  httpPref.value = "goodhostname";
  beforeAcceptCalled = BrowserTestUtils.waitForEvent(
    dialogElement,
    "beforeaccept"
  );
  dialogElement.acceptDialog();
  event = await beforeAcceptCalled;
  Assert.ok(!event.defaultPrevented, "The dialog was accepted");

  gBrowser.removeCurrentTab();
});

add_task(async function test_socks_file_uri() {
  await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
  const connectionURL =
    "chrome://browser/content/preferences/dialogs/connection.xhtml";
  let dialog = await openAndLoadSubDialog(connectionURL);
  let dialogElement = dialog.document.getElementById("ConnectionsDialog");

  let oldNetworkProxyType = Services.prefs.getIntPref("network.proxy.type");
  let oldSocksPort = Services.prefs.getIntPref("network.proxy.socks_port");
  registerCleanupFunction(function () {
    Services.prefs.setIntPref("network.proxy.type", oldNetworkProxyType);
    Services.prefs.setIntPref("network.proxy.socks_port", oldSocksPort);
    Services.prefs.clearUserPref("network.proxy.share_proxy_settings");
    for (let proxyType of ["http", "ssl", "socks"]) {
      Services.prefs.clearUserPref("network.proxy." + proxyType);
      Services.prefs.clearUserPref("network.proxy." + proxyType + "_port");
      if (proxyType == "http") {
        continue;
      }
      Services.prefs.clearUserPref("network.proxy.backup." + proxyType);
      Services.prefs.clearUserPref(
        "network.proxy.backup." + proxyType + "_port"
      );
    }
  });

  let proxyType = dialog.Preferences.get("network.proxy.type");
  let socksPref = dialog.Preferences.get("network.proxy.socks");
  let socksPortPref = dialog.Preferences.get("network.proxy.socks_port");
  proxyType.value = 1;
  socksPortPref.value = 1234;

  // file:// URIs should be accepted for SOCKS
  socksPref.value = "file:///var/run/socks5.sock";
  let beforeAcceptCalled = BrowserTestUtils.waitForEvent(
    dialogElement,
    "beforeaccept"
  );
  dialogElement.acceptDialog();
  let event = await beforeAcceptCalled;
  Assert.ok(
    !event.defaultPrevented,
    "The dialog was accepted with file:// SOCKS path"
  );

  // npipe:// URIs should also be accepted for SOCKS (Windows named pipes)
  socksPref.value = "npipe:///./pipe/socks";
  beforeAcceptCalled = BrowserTestUtils.waitForEvent(
    dialogElement,
    "beforeaccept"
  );
  dialogElement.acceptDialog();
  event = await beforeAcceptCalled;
  Assert.ok(
    !event.defaultPrevented,
    "The dialog was accepted with npipe:// SOCKS path"
  );

  // Regular hostnames should still work for SOCKS
  socksPref.value = "localhost";
  beforeAcceptCalled = BrowserTestUtils.waitForEvent(
    dialogElement,
    "beforeaccept"
  );
  dialogElement.acceptDialog();
  event = await beforeAcceptCalled;
  Assert.ok(
    !event.defaultPrevented,
    "The dialog was accepted with regular SOCKS hostname"
  );

  gBrowser.removeCurrentTab();
});
