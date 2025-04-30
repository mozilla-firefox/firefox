"use strict";

const TEST_PATH_COM = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);
const TEST_PATH_ORG = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.org"
);

const GEO_URL =
  "http://mochi.test:8888/tests/dom/geolocation/test/mochitest/network_geolocation.sjs";

add_task(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["geo.provider.network.url", GEO_URL],
      ["geo.timeout", 4000],
    ],
  });

  await BrowserTestUtils.withNewTab(
    TEST_PATH_COM + "empty.html",
    async browser => {
      let notificationPopup = document.getElementById("notification-popup");
      let notificationShown = BrowserTestUtils.waitForPopupEvent(
        notificationPopup,
        "shown"
      );

      const notificationHidden = BrowserTestUtils.waitForPopupEvent(
        notificationPopup,
        "hidden"
      );

      let res = SpecialPowers.spawn(browser, [], () => {
        return new Promise(resolve =>
          content.navigator.geolocation.getCurrentPosition(
            () => resolve("allowed"),
            () => resolve("disallowed")
          )
        );
      });

      await notificationShown;
      notificationPopup
        .querySelector("button.popup-notification-primary-button")
        .click();

      await notificationHidden;

      let result = await res;
      is(result, "allowed", "Geolocation allowed");

      BrowserTestUtils.startLoadingURIString(
        browser,
        TEST_PATH_COM + "empty.html"
      );
      await BrowserTestUtils.browserLoaded(browser);

      const bc = await SpecialPowers.spawn(
        browser,
        [TEST_PATH_ORG + "empty.html"],
        async url => {
          const { document } = content;
          const embed = document.createElement("embed");
          embed.src = url;
          const promise = new Promise(resolve => {
            embed.addEventListener("load", resolve, { once: true });
          });
          document.body.appendChild(embed);
          await promise;
          return embed.browsingContext;
        }
      );

      notificationShown = BrowserTestUtils.waitForPopupEvent(
        notificationPopup,
        "shown"
      );

      res = SpecialPowers.spawn(bc, [], () => {
        return new Promise(resolve =>
          content.navigator.geolocation.getCurrentPosition(
            () => resolve("allowed"),
            () => resolve("disallowed")
          )
        );
      });

      result = await Promise.race([
        res,
        notificationShown.then(_ => {
          return "notification shown";
        }),
      ]);
      is(result, "disallowed", "Geolocation disallowed");
    }
  );
});
