const TEST_URL_PATH = `https://example.org${DIRECTORY_PATH}form_basic_signup.html`;

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/passwordmgr/test/browser/browser_relay_utils.js",
  this
);

registerCleanupFunction(() => {
  try {
    Services.prefs.clearUserPref("signon.firefoxRelay.feature");
  } catch (_) {}
});

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.firefoxRelay.showToAllBrowsers", true],
      ["browser.settings-redesign.enabled", true],
    ],
  });
});

add_task(
  async function test_showToAllBrowsers_displays_Relay_autocomplete_item_to_unauthenticated_browser() {
    const rsSandbox = await stubRemoteSettingsAllowList();
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const popup = document.getElementById("PopupAutoComplete");
        await openACPopup(popup, browser, "#form-basic-username");

        const relayItem = getRelayItemFromACPopup(popup);
        Assert.ok(
          relayItem,
          "Relay item SHOULD be present in the autocomplete popup when the browser IS NOT signed in and the signon.firefoxRelay.showToAllBrowsers config is set to true."
        );
      }
    );
    rsSandbox.restore();
  }
);

add_task(async function test_site_not_on_allowList_doesnt_show_Relay() {
  const rsSandbox = await stubRemoteSettingsAllowList([
    { domain: "not-example.org" },
  ]);
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: TEST_URL_PATH,
    },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const relayItem = getRelayItemFromACPopup(popup);
      Assert.ok(
        !relayItem,
        "Relay item SHOULD NOT be present in the autocomplete popup when the site is not on the allow-list."
      );
    }
  );
  rsSandbox.restore();
});

add_task(
  async function test_showToAllBrowsers_open_ACPopup_twice_calls_RemoteSettings_once() {
    const rsSandbox = await stubRemoteSettingsAllowList();
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const popup = document.getElementById("PopupAutoComplete");
        await openACPopup(popup, browser, "#form-basic-username");

        const relayItem = getRelayItemFromACPopup(popup);
        Assert.ok(
          relayItem,
          "Relay item SHOULD be present in the autocomplete popup when the browser IS NOT signed in and the signon.firefoxRelay.showToAllBrowsers config is set to true."
        );
      }
    );
    const rsSandboxRemoteSettingsGetCallsBeforeSecondACPopup =
      rsSandbox.getFakes()[0].callCount;
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const popup = document.getElementById("PopupAutoComplete");
        await openACPopup(popup, browser, "#form-basic-username");

        const relayItem = getRelayItemFromACPopup(popup);
        Assert.ok(
          relayItem,
          "Relay item SHOULD be present in the autocomplete popup when the browser IS NOT signed in and the signon.firefoxRelay.showToAllBrowsers config is set to true."
        );
      }
    );
    Assert.equal(
      rsSandbox.getFakes()[0].callCount,
      rsSandboxRemoteSettingsGetCallsBeforeSecondACPopup,
      "FirefoxRelay shouldShowRelay should only call RemoteSettings.get() once."
    );
    rsSandbox.restore();
  }
);

add_task(
  async function test_showToAllBrowsers_click_on_Relay_opens_optin_prompt() {
    const rsSandbox = await stubRemoteSettingsAllowList();
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const acPopup = document.getElementById("PopupAutoComplete");
        await openACPopup(acPopup, browser, "#form-basic-username");
        await clickRelayItemAndWaitForPopup(acPopup);

        const fxaRelayOptInPrompt = document.getElementById(
          "fxa-and-relay-integration-offer-with-domain-and-value-prop-notification"
        );
        Assert.ok(
          fxaRelayOptInPrompt,
          "Clicking on Relay auto-complete item should open the FXA + Relay opt-in prompt"
        );
        const relayLearnMoreLink =
          fxaRelayOptInPrompt.querySelector(".firefox-relay-learn-more-url") ||
          fxaRelayOptInPrompt.querySelector(
            ".popup-notification-learnmore-link"
          );
        Assert.ok(
          relayLearnMoreLink,
          "Relay opt-in prompt includes link to learn more."
        );
        const substrings = ["support.mozilla.org", "firefox-relay-integration"];
        Assert.ok(
          substrings.every(val => relayLearnMoreLink.href.includes(val))
        );
      }
    );
    rsSandbox.restore();
  }
);

add_task(async function test_dismiss_Relay_optin_shows_Relay_again_later() {
  const rsSandbox = await stubRemoteSettingsAllowList();
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: TEST_URL_PATH,
    },
    async function (browser) {
      const acPopup = document.getElementById("PopupAutoComplete");
      const notificationPopup = document.getElementById("notification-popup");

      await openACPopup(acPopup, browser, "#form-basic-username");
      await clickRelayItemAndWaitForPopup(acPopup);

      const secondaryDismissButton = notificationPopup.querySelector(
        "button.popup-notification-secondary-button"
      );
      await clickButtonAndWaitForPopupToClose(secondaryDismissButton);

      await openACPopup(acPopup, browser, "#form-basic-username");
      const postDismissRelayItem = getRelayItemFromACPopup(acPopup);
      Assert.ok(
        postDismissRelayItem,
        "Relay item SHOULD be present in the autocomplete popup when: 1. the browser IS NOT signed in and 2. the signon.firefoxRelay.showToAllBrowsers config is set to true and 3. the user postponed the FXA + Relay opt-in popup."
      );
    }
  );
  rsSandbox.restore();
});

async function clickThruMoreActionsToDisableRelay(notificationPopup) {
  notificationPopup
    .querySelector("button.popup-notification-dropmarker")
    .click();
  const menuPopup = document.querySelector(
    "[data-l10n-id='popup-notification-more-actions-button']"
  );
  await BrowserTestUtils.waitForPopupEvent(menuPopup, "shown");
  const buttonToClick = menuPopup.querySelector("menuitem[accesskey='D']");
  if (buttonToClick.parentNode.isNativeMenu) {
    notificationPopup.activateItem(buttonToClick);
  } else {
    await clickButtonAndWaitForPopupToClose(buttonToClick);
  }
}

add_task(
  async function test_disable_Relay_optin_does_not_show_Relay_again_later() {
    const rsSandbox = await stubRemoteSettingsAllowList();
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const acPopup = document.getElementById("PopupAutoComplete");
        const notificationPopup = document.getElementById("notification-popup");

        await openACPopup(acPopup, browser, "#form-basic-username");
        await clickRelayItemAndWaitForPopup(acPopup);
        await clickThruMoreActionsToDisableRelay(notificationPopup);

        await openACPopup(acPopup, browser, "#form-basic-username");
        const postDisableRelayItem = getRelayItemFromACPopup(acPopup);
        Assert.ok(
          !postDisableRelayItem,
          "Relay item SHOULD NOT be present in the autocomplete popup when: 1. the browser IS NOT signed in and 2. the signon.firefoxRelay.showToAllBrowsers config is set to true and 3. the user disabled the FXA + Relay opt-in popup."
        );
      }
    );
    rsSandbox.restore();

    // restore Relay to default
    await SpecialPowers.clearUserPref("signon.firefoxRelay.feature");
  }
);

add_task(
  async function test_disable_Relay_optin_can_reenable_via_preferences() {
    const rsSandbox = await stubRemoteSettingsAllowList();
    // Disable Relay from the opt-in prompt
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const acPopup = document.getElementById("PopupAutoComplete");
        const notificationPopup = document.getElementById("notification-popup");

        await openACPopup(acPopup, browser, "#form-basic-username");
        await clickRelayItemAndWaitForPopup(acPopup);
        await clickThruMoreActionsToDisableRelay(notificationPopup);

        await openACPopup(acPopup, browser, "#form-basic-username");
        const postDisableRelayItem = getRelayItemFromACPopup(acPopup);
        Assert.ok(
          !postDisableRelayItem,
          "Relay item SHOULD NOT be present in the autocomplete popup when: 1. the browser IS NOT signed in and 2. the signon.firefoxRelay.showToAllBrowsers config is set to true and 3. the user disabled the FXA + Relay opt-in popup."
        );
      }
    );

    // Re-enable Relay via preferences
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "about:preferences#privacy",
      },
      async _browser => {
        const relayIntegrationCheckbox = content.document.querySelector(
          "moz-checkbox#relayIntegration"
        );
        let prefChanged = TestUtils.waitForCondition(() => {
          return Services.prefs.getBoolPref(
            "signon.firefoxRelay.feature",
            true
          );
        }, "Waiting for signon.firefoxRelay.feature pref to be enabled");
        relayIntegrationCheckbox.click();
        await prefChanged;
      }
    );
    // Visit the test page again and see the Relay autocomplete item is back
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const popup = document.getElementById("PopupAutoComplete");
        await openACPopup(popup, browser, "#form-basic-username");

        const relayItem = getRelayItemFromACPopup(popup);
        Assert.ok(
          relayItem,
          "Relay item SHOULD be present in the autocomplete popup when 1. the browser IS NOT signed in and 2. the signon.firefoxRelay.showToAllBrowsers config is set to true and 3. the user disabled Relay but 4. the user re-enabled Relay via preferences"
        );
      }
    );
    rsSandbox.restore();
    Services.prefs.clearUserPref("signon.firefoxRelay.feature");
  }
);

add_task(
  async function test_unauthenticated_browser_use_email_mask_opens_fxa_signin() {
    const relayParams = {
      service: "relay",
      utm_source: "relay-integration",
      utm_medium: "firefox-desktop",
    };
    const fxaSigninUrlString =
      await gFxAccounts.constructor.config.promiseConnectAccountURI(
        "relay_integration",
        relayParams
      );
    const fxaSigninURL = new URL(fxaSigninUrlString);
    // Now that we have a URL object, we can use its components
    const fxaServer = new HttpServer();
    fxaServer.registerPathHandler(
      fxaSigninURL.URI.pathQueryRef,
      (request, response) => {
        response.setStatusLine(request.httpVersion, 200, "OK");
        response.write("Mock FxA Sign-in page");
      }
    );
    fxaServer.start(-1);
    const mockFxaServerURL = new URL(fxaSigninUrlString);
    mockFxaServerURL.protocol = fxaServer.identity.primaryScheme + ":";
    mockFxaServerURL.hostname = fxaServer.identity.primaryHost;
    mockFxaServerURL.port = fxaServer.identity.primaryPort;
    // Override fxaccounts config to use mock server host
    await SpecialPowers.pushPrefEnv({
      set: [
        ["identity.fxaccounts.allowHttp", true],
        ["identity.fxaccounts.remote.root", mockFxaServerURL.origin],
      ],
    });

    const rsSandbox = await stubRemoteSettingsAllowList();
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: TEST_URL_PATH,
      },
      async function (browser) {
        const acPopup = document.getElementById("PopupAutoComplete");
        const notificationPopup = document.getElementById("notification-popup");

        await openACPopup(acPopup, browser, "#form-basic-username");
        await clickRelayItemAndWaitForPopup(acPopup);

        const primaryButton = notificationPopup.querySelector(
          "button.popup-notification-primary-button"
        );

        // oauth makes checking the url as a string difficult.
        let isSigninUrl = href => {
          const url = new URL(href);
          if (
            url.protocol != mockFxaServerURL.protocol ||
            url.hostname != mockFxaServerURL.hostname ||
            url.port != mockFxaServerURL.port
          ) {
            return false;
          }
          for (const [name, value] of Object.entries(relayParams)) {
            if (url.searchParams.get(name) != value) {
              return false;
            }
          }
          return true;
        };

        const newTabPromise = BrowserTestUtils.waitForNewTab(
          gBrowser,
          isSigninUrl,
          true
        );

        await clickButtonAndWaitForPopupToClose(primaryButton);

        const newTab = await newTabPromise;
        const loadedUrl = newTab.linkedBrowser.currentURI.spec;
        Assert.ok(
          isSigninUrl(loadedUrl),
          "Clicking Use email mask should open a new tab to FXA sign-in."
        );
        BrowserTestUtils.removeTab(newTab);
      }
    );
    rsSandbox.restore();
    await new Promise(resolve => {
      fxaServer.stop(resolve);
    });
    await SpecialPowers.popPrefEnv();

    try {
      Services.prefs.clearUserPref("signon.firefoxRelay.feature");
    } catch (_) {}
  }
);
