"use strict";

let contextMenu;

const NAVIGATION_ITEMS =
  AppConstants.platform == "macosx"
    ? [
        "context-back",
        false,
        "context-forward",
        false,
        "context-reload",
        true,
        "---",
        null,
        "context-bookmarkpage",
        true,
      ]
    : [
        "context-navigation",
        null,
        [
          "context-back",
          false,
          "context-forward",
          false,
          "context-reload",
          true,
          "context-bookmarkpage",
          true,
        ],
        null,
        "---",
        null,
      ];

const askChatMenu = [
  "context-ask-chat",
  true,
  // Need a blank entry here because the Ask Chat submenu is dynamically built with no ids.
  "",
  null,
];

add_task(async function test_setup() {
  const example_base =
    // eslint-disable-next-line @microsoft/sdl/no-insecure-url
    "http://example.com/browser/browser/base/content/test/contextMenu/";
  const url = example_base + "subtst_contextmenu_input.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  const chrome_base =
    "chrome://mochitests/content/browser/browser/base/content/test/contextMenu/";
  const contextmenu_common = chrome_base + "contextmenu_common.js";
  /* import-globals-from contextmenu_common.js */
  Services.scriptloader.loadSubScript(contextmenu_common, this);
});

add_task(async function test_text_input() {
  await test_contextmenu(
    "#input_text",
    [
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      false,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
    ],
    {
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
});

add_task(async function test_text_input_disabled() {
  await test_contextmenu(
    "#input_disabled",
    [
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      false,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
    ],
    {
      skipFocusChange: true,
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
});

add_task(async function test_password_input() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.generation.enabled", false]],
  });
  todo(
    false,
    "context-selectall is enabled on osx-e10s, and windows when" +
      " it should be disabled"
  );
  await test_contextmenu(
    "#input_password",
    [
      "manage-saved-logins",
      true,
      "---",
      null,
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      null,
      "context-reveal-password",
      null,
      "---",
      null,
      ...askChatMenu,
    ],
    {
      skipFocusChange: true,
      // Need to dynamically add the "password" type or LoginManager
      // will think that the form inputs on the page are part of a login form
      // and will add fill-login context menu items. The element needs to be
      // re-created as type=text afterwards since it uses hasBeenTypePassword.
      async preCheckContextMenuFn() {
        await SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [],
          async function () {
            let doc = content.document;
            let input = doc.getElementById("input_password");
            input.type = "password";
            input.clientTop; // force layout flush
          }
        );
      },
      async postCheckContextMenuFn() {
        await SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [],
          async function () {
            let doc = content.document;
            let input = doc.getElementById("input_password");
            input.outerHTML = `<input id=\"input_password\">`;
            input.clientTop; // force layout flush
          }
        );
      },
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function firefox_relay_input() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.firefoxRelay.feature", "enabled"]],
  });

  await test_contextmenu(
    "#input_username",
    [
      "use-relay-mask",
      true,
      "---",
      null,
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      false,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
    ],
    {
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );

  await test_contextmenu(
    "#input_email",
    [
      "use-relay-mask",
      true,
      "---",
      null,
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      null,
      "---",
      null,
      ...askChatMenu,
    ],
    {
      skipFocusChange: true,
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_tel_email_url_number_input() {
  todo(
    false,
    "context-selectall is enabled on osx-e10s, and windows when" +
      " it should be disabled"
  );
  for (let selector of [
    "#input_email",
    "#input_url",
    "#input_tel",
    "#input_number",
  ]) {
    await test_contextmenu(
      selector,
      [
        "context-undo",
        false,
        "context-redo",
        false,
        "---",
        null,
        "context-cut",
        false,
        "context-copy",
        false,
        "context-paste",
        null, // ignore clipboard state
        "context-delete",
        false,
        "context-selectall",
        null,
        "---",
        null,
        ...askChatMenu,
      ],
      {
        skipFocusChange: true,
        awaitOnMenuBuilt: {
          id: "context-ask-chat",
        },
      }
    );
  }
});

add_task(
  async function test_date_time_color_range_month_week_datetimelocal_input() {
    for (let selector of [
      "#input_date",
      "#input_time",
      "#input_color",
      "#input_range",
      "#input_month",
      "#input_week",
      "#input_datetime-local",
    ]) {
      await test_contextmenu(
        selector,
        [
          ...NAVIGATION_ITEMS,
          "context-savepage",
          true,
          "context-selectall",
          null,
          "---",
          null,
          "context-take-screenshot",
          true,
          "---",
          null,
          ...askChatMenu,
          "---",
          null,
          "context-viewsource",
          true,
          "---",
          null,
          "context-sendpagetodevice",
          true,
          // This entry will be blank because the submenu is dynamically generated.
          [],
          null,
        ],
        {
          skipFocusChange: true,
          awaitOnMenuBuilt: {
            id: "context-ask-chat",
          },
        }
      );
    }
  }
);

add_task(async function test_search_input() {
  todo(
    false,
    "context-selectall is enabled on osx-e10s, and windows when" +
      " it should be disabled"
  );
  await test_contextmenu(
    "#input_search",
    [
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      null,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
    ],
    {
      skipFocusChange: true,
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
});

add_task(async function test_text_input_readonly() {
  todo(
    false,
    "context-selectall is enabled on osx-e10s, and windows when" +
      " it should be disabled"
  );
  todo(
    false,
    "spell-check should not be enabled for input[readonly]. see bug 1246296"
  );
  await test_contextmenu(
    "#input_readonly",
    [
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      false,
      "context-copy",
      false,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      false,
      "context-selectall",
      null,
      "---",
      null,
      ...askChatMenu,
    ],
    {
      skipFocusChange: true,
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
});

add_task(async function test_cleanup() {
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
