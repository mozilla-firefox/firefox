/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ORIGIN = "https://example.com";
const CROSS_SUBFRAME_PAGE =
  getRootDirectory(gTestPath).replace("chrome://mochitests/content", ORIGIN) +
  "temporary_permissions_subframe.html";

const CROSS_FRAME_PAGE =
  getRootDirectory(gTestPath).replace("chrome://mochitests/content", ORIGIN) +
  "temporary_permissions_frame.html";

const PromptResult = {
  ALLOW: "allow",
  DENY: "deny",
  PROMPT: "prompt",
};

var Perms = Services.perms;
var uri = NetUtil.newURI(ORIGIN);
var principal = Services.scriptSecurityManager.createContentPrincipal(uri, {});

async function checkNotificationBothOrigins(
  firstPartyOrigin,
  thirdPartyOrigin
) {
  // Notification is shown, check label and deny to clean
  let popuphidden = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popuphidden"
  );

  let notification = PopupNotifications.panel.firstElementChild;
  // Check the label of the notificaiton should be the first party
  is(
    PopupNotifications.getNotification("geolocation").options.name,
    firstPartyOrigin,
    "Use first party's origin"
  );

  // Check the second name of the notificaiton should be the third party
  is(
    PopupNotifications.getNotification("geolocation").options.secondName,
    thirdPartyOrigin,
    "Use third party's origin"
  );

  // Check remember checkbox is hidden
  let checkbox = notification.checkbox;
  ok(!!checkbox, "checkbox is present");
  ok(checkbox.hidden, "checkbox is not visible");
  ok(!checkbox.checked, "checkbox not checked");

  EventUtils.synthesizeMouseAtCenter(notification.secondaryButton, {});
  await popuphidden;
}

async function checkGeolocation(browser, frameId, expect) {
  let isPrompt = expect == PromptResult.PROMPT;
  let waitForPrompt;
  if (isPrompt) {
    waitForPrompt = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popupshown"
    );
  }

  await SpecialPowers.spawn(
    browser,
    [{ frameId, expect, isPrompt }],
    async args => {
      let frame = content.document.getElementById(args.frameId);

      let waitForNoPrompt = new Promise(resolve => {
        function onMessage(event) {
          // Check the result right here because there's no notification
          Assert.equal(
            event.data,
            args.expect,
            "Correct expectation for third party"
          );
          content.window.removeEventListener("message", onMessage);
          resolve();
        }

        if (!args.isPrompt) {
          content.window.addEventListener("message", onMessage);
        }
      });

      await content.SpecialPowers.spawn(frame, [], async () => {
        const { E10SUtils } = ChromeUtils.importESModule(
          "resource://gre/modules/E10SUtils.sys.mjs"
        );

        E10SUtils.wrapHandlingUserInput(this.content, true, function () {
          let frameDoc = this.content.document;
          frameDoc.getElementById("geo").click();
        });
      });

      if (!args.isPrompt) {
        await waitForNoPrompt;
      }
    }
  );

  if (isPrompt) {
    await waitForPrompt;
  }
}

add_setup(async function () {
  await new Promise(r => {
    SpecialPowers.pushPrefEnv(
      {
        set: [
          ["dom.security.permissionsPolicy.header.enabled", true],
          ["dom.security.permissionsPolicy.webidl.enabled", true],
          // This is the amount of time before the repeating
          // NetworkGeolocationProvider timer is stopped.
          // It needs to be less than 5000ms, or the timer will be
          // reported as left behind by the test.
          ["geo.timeout", 4000],
        ],
      },
      r
    );
  });
});

// Test that temp blocked permissions in first party affect the third party
// iframe.
add_task(async function testUseTempPermissionsFirstParty() {
  await BrowserTestUtils.withNewTab(
    CROSS_SUBFRAME_PAGE,
    async function (browser) {
      SitePermissions.setForPrincipal(
        principal,
        "geo",
        SitePermissions.BLOCK,
        SitePermissions.SCOPE_TEMPORARY,
        browser
      );

      await checkGeolocation(browser, "frame", PromptResult.DENY);

      SitePermissions.removeFromPrincipal(principal, "geo", browser);
    }
  );
});

// Test that persistent permissions in first party affect the third party
// iframe.
add_task(async function testUsePersistentPermissionsFirstParty() {
  await BrowserTestUtils.withNewTab(
    CROSS_SUBFRAME_PAGE,
    async function (browser) {
      async function checkPermission(aPermission, aExpect) {
        PermissionTestUtils.add(uri, "geo", aPermission);
        await checkGeolocation(browser, "frame", aExpect);

        if (aExpect == PromptResult.PROMPT) {
          // Notification is shown, check label and deny to clean
          let popuphidden = BrowserTestUtils.waitForEvent(
            PopupNotifications.panel,
            "popuphidden"
          );

          let notification = PopupNotifications.panel.firstElementChild;
          // Check the label of the notificaiton should be the first party
          is(
            PopupNotifications.getNotification("geolocation").options.name,
            uri.host,
            "Use first party's origin"
          );

          EventUtils.synthesizeMouseAtCenter(notification.secondaryButton, {});

          await popuphidden;
          SitePermissions.removeFromPrincipal(null, "geo", browser);
        }

        PermissionTestUtils.remove(uri, "geo");
      }

      await checkPermission(Perms.PROMPT_ACTION, PromptResult.PROMPT);
      await checkPermission(Perms.DENY_ACTION, PromptResult.DENY);
      await checkPermission(Perms.ALLOW_ACTION, PromptResult.ALLOW);
    }
  );
});

// Test that we do not prompt for maybe unsafe permission delegation if the
// origin of the page is the original src origin.
add_task(async function testPromptInMaybeUnsafePermissionDelegation() {
  await BrowserTestUtils.withNewTab(
    CROSS_SUBFRAME_PAGE,
    async function (browser) {
      // Persistent allow top level origin
      PermissionTestUtils.add(uri, "geo", Perms.ALLOW_ACTION);

      await checkGeolocation(browser, "frameAllowsAll", PromptResult.ALLOW);

      SitePermissions.removeFromPrincipal(null, "geo", browser);
      PermissionTestUtils.remove(uri, "geo");
    }
  );
});

// Test that we should prompt if we are in unsafe permission delegation and
// change location to origin which is not explicitly trusted. The prompt popup
// should include both first and third party origin.
add_task(async function testPromptChangeLocationUnsafePermissionDelegation() {
  await BrowserTestUtils.withNewTab(
    CROSS_SUBFRAME_PAGE,
    async function (browser) {
      // Persistent allow top level origin
      PermissionTestUtils.add(uri, "geo", Perms.ALLOW_ACTION);

      let iframe = await SpecialPowers.spawn(browser, [], () => {
        return content.document.getElementById("frameAllowsAll")
          .browsingContext;
      });

      let otherURI =
        "https://test1.example.com/browser/browser/base/content/test/browser-permissions/permissions.html";
      let loaded = BrowserTestUtils.browserLoaded(browser, true, otherURI);
      await SpecialPowers.spawn(iframe, [otherURI], async function (_otherURI) {
        content.location = _otherURI;
      });
      await loaded;

      await checkGeolocation(browser, "frameAllowsAll", PromptResult.PROMPT);
      await checkNotificationBothOrigins(uri.host, "test1.example.com");

      SitePermissions.removeFromPrincipal(null, "geo", browser);
      PermissionTestUtils.remove(uri, "geo");
    }
  );
});

for (let page of [
  "permissions_delegate_geo.xhtml",
  "permissions_delegate_geo.svg",
]) {
  add_task(async function testUnsafeDelegationInXMLDocument() {
    await BrowserTestUtils.withNewTab(CROSS_SUBFRAME_PAGE, async browser => {
      PermissionTestUtils.add(uri, "geo", Perms.ALLOW_ACTION);
      try {
        let iframe = await SpecialPowers.spawn(browser, [], () => {
          return content.document.getElementById("frameAllowsAll")
            .browsingContext;
        });
        let otherURI = `https://test1.example.com/browser/browser/base/content/test/browser-permissions/${page}`;
        let loaded = BrowserTestUtils.browserLoaded(browser, true, otherURI);
        await SpecialPowers.spawn(iframe, [otherURI], target => {
          content.location = target;
        });
        await loaded;

        await checkGeolocation(browser, "frameAllowsAll", PromptResult.PROMPT);
        await checkNotificationBothOrigins(uri.host, "test1.example.com");
      } finally {
        SitePermissions.removeFromPrincipal(null, "geo", browser);
        PermissionTestUtils.remove(uri, "geo");
      }
    });
  });
}

// If we are in unsafe permission delegation and the origin is explicitly
// trusted in ancestor chain. Do not need prompt
add_task(async function testExplicitlyAllowedInChain() {
  await BrowserTestUtils.withNewTab(CROSS_FRAME_PAGE, async function (browser) {
    // Persistent allow top level origin
    PermissionTestUtils.add(uri, "geo", Perms.ALLOW_ACTION);

    let iframeAncestor = await SpecialPowers.spawn(browser, [], () => {
      return content.document.getElementById("frameAncestor").browsingContext;
    });

    let iframe = await SpecialPowers.spawn(iframeAncestor, [], () => {
      return content.document.getElementById("frameAllowsAll").browsingContext;
    });

    // Change location to check that we actually look at the ancestor chain
    // instead of just considering the "same origin as src" rule.
    let otherURI =
      "https://test2.example.com/browser/browser/base/content/test/browser-permissions/permissions.html";
    let loaded = BrowserTestUtils.browserLoaded(browser, true, otherURI);
    await SpecialPowers.spawn(iframe, [otherURI], async function (_otherURI) {
      content.location = _otherURI;
    });
    await loaded;

    await checkGeolocation(
      iframeAncestor,
      "frameAllowsAll",
      PromptResult.ALLOW
    );

    PermissionTestUtils.remove(uri, "geo");
  });
});
