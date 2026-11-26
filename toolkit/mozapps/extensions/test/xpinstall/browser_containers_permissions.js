/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const MY_CONTEXT = 2;

const SITE_PRINCIPAL = Services.scriptSecurityManager.createContentPrincipal(
  Services.io.newURI(SECURE_TESTROOT),
  {}
);

const PAGE_URL =
  SECURE_TESTROOT +
  "navigate.html?" +
  encodeURIComponent(SECURE_TESTROOT + "amosigned.xpi");

// Resolves with whichever notification fires first, which tells us which
// branch of installAddonFromWebpage the install request took.
function waitForFirstInstallTopic() {
  const TOPICS = [
    "addon-install-blocked-silent",
    "addon-install-blocked",
    "addon-install-started",
  ];
  return new Promise(resolve => {
    const observer = {
      observe(subject, topic) {
        for (const t of TOPICS) {
          Services.obs.removeObserver(observer, t);
        }
        resolve(topic);
      },
    };
    for (const t of TOPICS) {
      Services.obs.addObserver(observer, t);
    }
  });
}

async function cancelAllInstalls() {
  for (const install of await AddonManager.getAllInstalls()) {
    install.cancel();
  }
}

// The "install" permission records whether the browser trusts a site as an
// add-on source, which is not a per-container decision, so a DENY set on the
// site must also block installs triggered from a container tab.
add_task(async function test_userContext_install_permission_deny() {
  PermissionTestUtils.add(
    SITE_PRINCIPAL,
    "install",
    Services.perms.DENY_ACTION
  );
  registerCleanupFunction(() =>
    PermissionTestUtils.remove(SITE_PRINCIPAL, "install")
  );

  for (const userContextId of [0, MY_CONTEXT]) {
    const firstTopic = waitForFirstInstallTopic();
    const tab = BrowserTestUtils.addTab(gBrowser, PAGE_URL, { userContextId });
    gBrowser.selectedTab = tab;

    is(
      await firstTopic,
      "addon-install-blocked-silent",
      `Install is blocked without a prompt with userContextId=${userContextId}`
    );

    await cancelAllInstalls();
    BrowserTestUtils.removeTab(tab);
  }

  PermissionTestUtils.remove(SITE_PRINCIPAL, "install");
});

// Choosing "Never Allow" from a container tab must block the site in every
// context, consistent with how the "install" permission is checked.
add_task(async function test_userContext_install_never_allow() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["xpinstall.whitelist.required", true],
      ["extensions.postDownloadThirdPartyPrompt", false],
    ],
  });

  const containerPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(SITE_PRINCIPAL.URI, {
      userContextId: MY_CONTEXT,
    });
  registerCleanupFunction(() => {
    PermissionTestUtils.remove(SITE_PRINCIPAL, "install");
    PermissionTestUtils.remove(containerPrincipal, "install");
  });

  const popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  const tab = BrowserTestUtils.addTab(gBrowser, PAGE_URL, {
    userContextId: MY_CONTEXT,
  });
  gBrowser.selectedTab = tab;
  await popupShown;

  const notification = PopupNotifications.panel.firstElementChild;
  is(
    notification.getAttribute("popupid"),
    "addon-install-blocked",
    "The install blocked prompt is shown in the container tab"
  );

  const popupHidden = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popuphidden"
  );
  notification.menupopup.firstElementChild.click();
  await popupHidden;

  is(
    PermissionTestUtils.testPermission(SITE_PRINCIPAL, "install"),
    Services.perms.DENY_ACTION,
    "Never Allow from a container tab blocks the site in the default context"
  );
  is(
    PermissionTestUtils.testPermission(containerPrincipal, "install"),
    Services.perms.DENY_ACTION,
    "Never Allow from a container tab blocks the site in the container"
  );

  await cancelAllInstalls();
  BrowserTestUtils.removeTab(tab);

  for (const userContextId of [0, MY_CONTEXT]) {
    const firstTopic = waitForFirstInstallTopic();
    const blockedTab = BrowserTestUtils.addTab(gBrowser, PAGE_URL, {
      userContextId,
    });
    gBrowser.selectedTab = blockedTab;

    is(
      await firstTopic,
      "addon-install-blocked-silent",
      `Install is blocked without a prompt with userContextId=${userContextId}`
    );

    await cancelAllInstalls();
    BrowserTestUtils.removeTab(blockedTab);
  }

  PermissionTestUtils.remove(SITE_PRINCIPAL, "install");
  PermissionTestUtils.remove(containerPrincipal, "install");
  await SpecialPowers.popPrefEnv();
});

// A site the browser trusts as an add-on source stays trusted in a container tab.
add_task(async function test_userContext_install_permission_allow() {
  await SpecialPowers.pushPrefEnv({
    set: [["xpinstall.whitelist.required", true]],
  });

  PermissionTestUtils.add(
    SITE_PRINCIPAL,
    "install",
    Services.perms.ALLOW_ACTION
  );
  registerCleanupFunction(() =>
    PermissionTestUtils.remove(SITE_PRINCIPAL, "install")
  );

  for (const userContextId of [0, MY_CONTEXT]) {
    const principal = Services.scriptSecurityManager.createContentPrincipal(
      SITE_PRINCIPAL.URI,
      { userContextId }
    );
    ok(
      AddonManager.isInstallAllowed("application/x-xpinstall", principal),
      `Install is allowed with userContextId=${userContextId}`
    );
  }

  PermissionTestUtils.remove(SITE_PRINCIPAL, "install");
  await SpecialPowers.popPrefEnv();
});
