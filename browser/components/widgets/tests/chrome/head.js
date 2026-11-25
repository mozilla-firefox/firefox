/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { BrowserTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/BrowserTestUtils.sys.mjs"
);

const TIP_PREF = "browser.widgets.generated.promptTip.seen";

SimpleTest.registerCleanupFunction(() =>
  Services.prefs.clearUserPref(TIP_PREF)
);

async function createComposer(props = {}) {
  await customElements.whenDefined("widget-composer");
  let composer = document.createElement("widget-composer");
  Object.assign(composer, props);
  document.getElementById("content").append(composer);
  await composer.updateComplete;
  return composer;
}

function nextSubmit(composer) {
  return BrowserTestUtils.waitForEvent(composer, "WidgetComposer:Submit").then(
    event => event.detail
  );
}

const tip = composer => composer.shadowRoot.querySelector("moz-promo");

// moz-promo and its close moz-button render asynchronously after the composer.
async function tipRendered(composer) {
  let promo = tip(composer);
  await promo.updateComplete;
  await promo.closeButton.updateComplete;
  return promo;
}

async function typeInNewComposer(text) {
  let composer = await createComposer();
  composer.focus();
  sendString(text);
  await composer.updateComplete;
  return composer;
}
