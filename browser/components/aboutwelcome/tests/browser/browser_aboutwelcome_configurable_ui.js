"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

add_setup(async function () {
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("browser.aboutwelcome.didSeeFinalScreen");
  });
});

/**
 * Test configurability of single select picker icons styles
 */
add_task(async function test_aboutwelcome_single_select_icon_styles() {
  let screens = [
    makeTestContent(`TEST_SINGLE_SELECT_ICONS`, {
      tiles: {
        type: "single-select",
        selected: "horizontal",
        action: {
          picker: "<event>",
        },
        data: [
          {
            icon: {
              background: `center / contain no-repeat url("chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif")`,
              width: "150px",
              height: "100px",
              marginInline: "10px",
              borderRadius: "5px",
            },
            id: "test1",
            label: {
              raw: "test1 label",
            },
            action: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "test1.pref",
                  value: true,
                },
              },
            },
          },
          {
            defaultValue: true,
            icon: {
              background: `center / contain no-repeat url("chrome://activity-stream/content/data/content/assets/heart.webp")`,
              width: "150px",
              height: "100px",
              marginInline: "10px",
              borderRadius: "5px",
            },
            id: "test2",
            label: {
              raw: "test2 label",
            },
            action: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "test2.pref",
                  value: false,
                },
              },
            },
          },
        ],
      },
    }),
  ];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_element_styles(
    browser,
    ".icon.test1",
    // Expected styles:
    {
      "background-image":
        'url("chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif")',
      "background-repeat": "no-repeat",
      "background-size": "contain",
      width: "150px",
      height: "100px",
      "margin-inline": "10px",
      "border-radius": "5px",
    }
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

add_task(async function test_aboutwelcome_link_paragraph_inline_image() {
  const TEST_ICON_URL =
    "resource://content-accessible/media/picture-in-picture-open.svg";
  const TEST_CONTENT = makeTestContent("TEST_LINK_PARAGRAPH_IMAGE_STEP", {
    above_button_content: [
      {
        type: "text",
        text: [
          "Learn more ",
          { imageURL: TEST_ICON_URL, alt: "icon" },
          " here.",
        ],
      },
    ],
  });

  let browser = await openAboutWelcome(JSON.stringify([TEST_CONTENT]));

  await test_screen_content(
    browser,
    "renders an image segment in a LinkParagraph",
    [`.link-paragraph img.inline-icon[src="${TEST_ICON_URL}"][alt="icon"]`]
  );

  browser.closeBrowser();
});

add_task(async function test_aboutwelcome_link_paragraph_segment_action() {
  const MANAGE_DATA_PREF = "test-manage-data-pref";
  // SET_PREF namespaces the prefs it writes.
  const MANAGE_DATA_FULL_PREF = `messaging-system-action.${MANAGE_DATA_PREF}`;
  const TEST_CONTENT = makeTestContent("TEST_LINK_PARAGRAPH_ACTION_STEP", {
    above_button_content: [
      {
        type: "text",
        font_styles: "legal",
        text: [
          "By continuing, you agree to the ",
          { raw: "Firefox Terms of Use", link_key: "terms_of_use" },
          ".",
        ],
      },
      {
        type: "text",
        font_styles: "legal",
        text: [
          {
            raw: "Manage diagnostic and interaction data",
            id: "manage_data",
            action: {
              type: "SET_PREF",
              data: { pref: { name: MANAGE_DATA_PREF, value: true } },
            },
          },
        ],
      },
    ],
    terms_of_use: {
      action: {
        type: "OPEN_URL",
        data: { args: "https://example.com/terms", where: "tab" },
      },
    },
  });

  let browser = await openAboutWelcome(JSON.stringify([TEST_CONTENT]));

  await test_screen_content(
    browser,
    "renders multiple legal paragraphs above the primary button",
    [
      `.legal-paragraph a[value="terms_of_use"]`,
      `.legal-paragraph a[value="manage_data"]`,
    ]
  );

  await SpecialPowers.spawn(browser, [], async () => {
    is(
      content.document.querySelectorAll(".legal-paragraph").length,
      2,
      "Both legal paragraphs render"
    );
  });

  // A segment carrying its own action triggers that special message action,
  // rather than only being able to open a URL through `href`.
  ok(
    !Services.prefs.getBoolPref(MANAGE_DATA_FULL_PREF, false),
    "Manage data pref is unset before clicking the link"
  );
  await onButtonClick(browser, `.legal-paragraph a[value="manage_data"]`);
  await TestUtils.waitForCondition(
    () => Services.prefs.getBoolPref(MANAGE_DATA_FULL_PREF, false),
    "Waiting for the manage data link's SET_PREF action"
  );

  Services.prefs.clearUserPref(MANAGE_DATA_FULL_PREF);
  browser.closeBrowser();
});
