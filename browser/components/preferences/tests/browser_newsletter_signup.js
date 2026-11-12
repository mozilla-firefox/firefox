/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const EXPECTED_URL =
  "https://www.firefox.com/newsletter/?utm_source=firefox-settings&utm_campaign=newsletter-signup&utm_medium=referral&utm_content=about-firefox";

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

async function getNewsletterControl() {
  await openPreferencesViaOpenPreferencesAPI("paneAbout", { leaveOpen: true });
  return settingControlRenders("newsletterSignUp", gBrowser.contentWindow);
}

add_task(async function test_newsletter_visible_for_supported_locales() {
  for (let locale of ["en-US", "en-CA", "en-GB", "de", "fr"]) {
    setLocale(locale);
    let control = await getNewsletterControl();
    let group = control.closest("setting-group");
    is_element_visible(group, `Newsletter settings are visible for ${locale}`);
    is(
      control.controlEl.href,
      EXPECTED_URL,
      "Newsletter link has the expected URL"
    );
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function test_newsletter_hidden_for_unsupported_locale() {
  setLocale("af");
  let control = await getNewsletterControl();
  let group = control.closest("setting-group");
  is_element_hidden(
    group,
    "Newsletter settings are hidden for unsupported locale"
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
