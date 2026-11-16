/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const EXPECTED_URL =
  "https://www.firefox.com/newsletter/?utm_source=firefox-settings&utm_campaign=newsletter-signup&utm_medium=referral&utm_content=about-firefox";

const SUPPORTED_LOCALES = ["en-US", "en-CA", "en-GB", "de", "fr"];

const initialAvailable = Services.locale.availableLocales;
const initialRequested = Services.locale.requestedLocales;

function setLocale(language) {
  Services.locale.availableLocales = [language];
  Services.locale.requestedLocales = [language];
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ clear: [["intl.locale.requested"]] });
  registerCleanupFunction(() => {
    Services.locale.availableLocales = initialAvailable;
    Services.locale.requestedLocales = initialRequested;
  });
});

async function setLocaleAndWaitForGroup(group, locale, hidden) {
  let updated = BrowserTestUtils.waitForMutationCondition(
    group,
    { attributeFilter: ["data-hidden-by-setting-group"] },
    () => group.hasAttribute("data-hidden-by-setting-group") == hidden
  );
  setLocale(locale);
  await updated;
}

add_task(async function test_newsletter_visibility_follows_locale_changes() {
  await openPreferencesViaOpenPreferencesAPI("paneAbout", { leaveOpen: true });
  let control = await settingControlRenders(
    "newsletterSignUp",
    gBrowser.contentWindow
  );
  let group = control.closest("setting-group");

  for (let locale of SUPPORTED_LOCALES) {
    setLocale(locale);
    await control.updateComplete;
    is_element_visible(
      control,
      `Newsletter sign up link is visible for ${locale}`
    );
    is(
      control.controlEl.href,
      EXPECTED_URL,
      "Newsletter link has the expected URL"
    );
  }

  await setLocaleAndWaitForGroup(group, "af", true);
  is_element_hidden(
    group,
    "Newsletter settings are hidden for unsupported locale"
  );

  await setLocaleAndWaitForGroup(group, SUPPORTED_LOCALES[0], false);
  is_element_visible(
    group,
    "Newsletter settings are shown again after switching back to a supported locale"
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
