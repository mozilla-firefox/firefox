"use strict";

const BASE_CONTENT = {
  id: "MOBILE_DOWNLOADS",
  content: {
    tiles: {
      type: "mobile_downloads",
      data: {
        QR_code: {
          image_url:
            "chrome://browser/content/asrouter/assets/desktop-to-mobile-non-eu-QR.svg",
          alt_text: "Test alt",
        },
        email: {
          link_text: "Email yourself a link",
        },
      },
    },
  },
};

const ALT_TEXT = BASE_CONTENT.content.tiles.data.QR_code.alt_text;

/**
 * Test rendering a screen with a mobile downloads tile
 * including QR code and email elements
 */
add_task(async function test_aboutwelcome_mobile_downloads_all() {
  const TEST_JSON = JSON.stringify([BASE_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders screen with all mobile download elements",
    // Expected selectors:
    [`img.qr-code-image[alt="${ALT_TEXT}"]`, "button.email-link"]
  );
});

/**
 * Test rendering a screen with a mobile downloads tile
 * including only a QR code
 */
add_task(async function test_aboutwelcome_mobile_downloads_qr() {
  let SCREEN_CONTENT = structuredClone(BASE_CONTENT);
  const QR_CODE_SRC = SCREEN_CONTENT.content.tiles.data.QR_code.image_url;

  delete SCREEN_CONTENT.content.tiles.data.email;
  const TEST_JSON = JSON.stringify([SCREEN_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders screen with QR code",
    // Expected selectors:
    [`img.qr-code-image[alt="${ALT_TEXT}"][src="${QR_CODE_SRC}"]`],
    // Unexpected selectors:
    ["button.email-link"]
  );
});
