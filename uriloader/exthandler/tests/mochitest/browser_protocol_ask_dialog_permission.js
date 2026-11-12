/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

let gHandlerService = Cc["@mozilla.org/uriloader/handler-service;1"].getService(
  Ci.nsIHandlerService
);

const ROOT_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content/",
  ""
);

// Testing multiple protocol / origin combinations takes long on debug.
requestLongerTimeout(7);

const DIALOG_URL_APP_CHOOSER =
  "chrome://mozapps/content/handling/appChooser.xhtml";
const DIALOG_URL_PERMISSION =
  "chrome://mozapps/content/handling/permissionDialog.xhtml";

const PROTOCOL_HANDLER_OPEN_PERM_KEY = "open-protocol-handler";
const PERMISSION_KEY_DELIMITER = "^";

const TEST_PROTOS = ["foo", "bar"];
const WALLET_PROTO = "moz-test-wallet";

let testDir = getChromeDir(getResolvedURI(gTestPath));

const ORIGIN1 = "https://example.com";
const ORIGIN2 = "https://example.org";
const ORIGIN3 = Services.io.newFileURI(testDir).spec;
const PRINCIPAL1 =
  Services.scriptSecurityManager.createContentPrincipalFromOrigin(ORIGIN1);
const PRINCIPAL2 =
  Services.scriptSecurityManager.createContentPrincipalFromOrigin(ORIGIN2);
const PRINCIPAL3 =
  Services.scriptSecurityManager.createContentPrincipalFromOrigin(ORIGIN3);

const NULL_PRINCIPAL_SCHEME = Services.scriptSecurityManager
  .createNullPrincipal({})
  .scheme.toLowerCase();

/**
 * Get the open protocol handler permission key for a given protocol scheme.
 *
 * @param {string} aProtocolScheme - Scheme of protocol to construct permission
 * key with.
 */
function getSkipProtoDialogPermissionKey(aProtocolScheme) {
  return (
    PROTOCOL_HANDLER_OPEN_PERM_KEY + PERMISSION_KEY_DELIMITER + aProtocolScheme
  );
}

/**
 * Creates dummy web protocol handlers used for testing.
 */
function initTestHandlers() {
  const allProtos = structuredClone(TEST_PROTOS);
  allProtos.push(WALLET_PROTO);
  allProtos.forEach(scheme => {
    let webHandler = Cc[
      "@mozilla.org/uriloader/web-handler-app;1"
    ].createInstance(Ci.nsIWebHandlerApp);
    webHandler.name = scheme + "Handler";
    webHandler.uriTemplate = ORIGIN1 + "/?url=%s";

    let handlerInfo = HandlerServiceTestUtils.getBlankHandlerInfo(scheme);
    handlerInfo.possibleApplicationHandlers.appendElement(webHandler);
    handlerInfo.preferredApplicationHandler = webHandler;
    gHandlerService.store(handlerInfo);
  });
}

/**
 * Update whether the protocol handler dialog is shown for our test protocol +
 * handler.
 *
 * @param {string} scheme - Scheme of the protocol to change the ask state for.
 * @param {boolean} ask - true => show dialog, false => skip dialog.
 */
function updateAlwaysAsk(scheme, ask) {
  let handlerInfo = HandlerServiceTestUtils.getHandlerInfo(scheme);
  handlerInfo.alwaysAskBeforeHandling = ask;
  gHandlerService.store(handlerInfo);
}

/**
 * Test whether the protocol handler dialog is set to show for our
 * test protocol + handler.
 *
 * @param {string} scheme - Scheme of the protocol to test the ask state for.
 * @param {boolean} ask - true => show dialog, false => skip dialog.
 */
function testAlwaysAsk(scheme, ask) {
  is(
    HandlerServiceTestUtils.getHandlerInfo(scheme).alwaysAskBeforeHandling,
    ask,
    "Should have correct alwaysAsk state."
  );
}

/**
 * Triggers the load via a server redirect.
 *
 * @param {string} serverRedirect - The redirect type.
 */
function useServerRedirect(serverRedirect) {
  return async (browser, scheme) => {
    let uri = `${scheme}://test`;

    let innerParams = new URLSearchParams();
    innerParams.set("uri", uri);
    innerParams.set("redirectType", serverRedirect);
    let params = new URLSearchParams();
    params.set(
      "uri",
      "https://example.com/" +
        ROOT_PATH +
        "redirect_helper.sjs?" +
        innerParams.toString()
    );
    uri =
      "https://example.org/" +
      ROOT_PATH +
      "redirect_helper.sjs?" +
      params.toString();
    BrowserTestUtils.startLoadingURIString(browser, uri);
  };
}

/**
 * Triggers the load with a specific principal or the browser's current
 * principal.
 *
 * @param {nsIPrincipal} [principal] - Principal to use to trigger the load.
 */
function useTriggeringPrincipal(principal = undefined) {
  return async (browser, scheme) => {
    let uri = `${scheme}://test`;
    let triggeringPrincipal = principal ?? browser.contentPrincipal;

    info("Loading uri: " + uri);
    browser.loadURI(Services.io.newURI(uri), { triggeringPrincipal });
  };
}

/**
 * Navigates to a test URL with the given protocol scheme and waits for the
 * result.
 *
 * @param {MozBrowser} browser - Browser to navigate.
 * @param {string} scheme - Scheme of the test url. e.g. irc
 * @param {object} [options] - Test options.
 * @param {object} [options.permDialogOptions] - Test options for the permission
 * dialog. If defined, we expect this dialog to be shown.
 * @param {object} [options.chooserDialogOptions] - Test options for the chooser
 * dialog. If defined, we expect this dialog to be shown.
 * @param {Function} [options.triggerLoad] - An async callback function to
 * trigger the load. Will be passed the browser and scheme to use.
 * @param {nsIPrincipal} [options.triggeringPrincipal] - Principal to trigger
 * the load with. Defaults to the browsers content principal.
 * @returns {Promise} - A promise which resolves once the test is complete.
 */
async function testOpenProto(
  browser,
  scheme,
  {
    permDialogOptions,
    chooserDialogOptions,
    triggerLoad = useTriggeringPrincipal(),
  } = {}
) {
  let permDialogOpenPromise;
  let chooserDialogOpenPromise;

  if (permDialogOptions) {
    info("Should see permission dialog");
    permDialogOpenPromise = waitForProtocolPermissionDialog(browser, true);
  }

  if (chooserDialogOptions) {
    info("Should see chooser dialog");
    chooserDialogOpenPromise = waitForProtocolAppChooserDialog(browser, true);
  }
  await triggerLoad(browser, scheme);
  let webHandlerLoadedPromise;

  let webHandlerShouldOpen =
    (!permDialogOptions && !chooserDialogOptions) ||
    ((permDialogOptions?.actionConfirm || permDialogOptions?.actionChangeApp) &&
      chooserDialogOptions?.actionConfirm);

  // Register web handler load listener if we expect to trigger it.
  if (webHandlerShouldOpen) {
    webHandlerLoadedPromise = waitForHandlerURL(browser, scheme);
  }

  if (permDialogOpenPromise) {
    let dialog = await permDialogOpenPromise;
    let dialogEl = getDialogElementFromSubDialog(dialog);
    let dialogType = getDialogType(dialog);

    let {
      hasCheckbox,
      checkboxOrigin,
      hasChangeApp,
      chooserIsNext,
      actionCheckbox,
      actionConfirm,
      actionChangeApp,
      checkContents,
      hasWalletWarning = false,
    } = permDialogOptions;

    if (actionChangeApp) {
      actionConfirm = false;
    }

    let descriptionEl = dialogEl.querySelector("#description");
    let warningEl = dialogEl.querySelector("#warning-bar");
    if (hasWalletWarning) {
      ok(
        descriptionEl && !BrowserTestUtils.isVisible(descriptionEl),
        "Has an invisible description element."
      );
      ok(
        warningEl && BrowserTestUtils.isVisible(warningEl),
        "Has a visible warning element."
      );
      ok(
        !warningEl.innerHTML.toLowerCase().includes(NULL_PRINCIPAL_SCHEME),
        "Warning does not include NullPrincipal scheme."
      );
    } else {
      ok(
        descriptionEl && BrowserTestUtils.isVisible(descriptionEl),
        "Has a visible description element."
      );

      ok(
        !descriptionEl.innerHTML.toLowerCase().includes(NULL_PRINCIPAL_SCHEME),
        "Description does not include NullPrincipal scheme."
      );
      ok(
        warningEl && !BrowserTestUtils.isVisible(warningEl),
        "Has an invisible warning element."
      );
    }

    await testCheckbox(dialogEl, dialogType, {
      hasCheckbox,
      actionCheckbox,
      checkboxOrigin,
    });

    // Check the button label depending on whether we would show the chooser
    // dialog next or directly open the handler.
    let acceptBtnLabel = dialogEl.getButton("accept")?.label;

    if (chooserIsNext) {
      is(
        acceptBtnLabel,
        "Choose Application",
        "Accept button has choose app label"
      );
    } else {
      is(acceptBtnLabel, "Open Link", "Accept button has open link label");
    }

    let changeAppLink = dialogEl.ownerDocument.getElementById("change-app");
    if (typeof hasChangeApp == "boolean") {
      ok(changeAppLink, "Permission dialog should have changeApp link label");
      is(
        !changeAppLink.hidden,
        hasChangeApp,
        "Permission dialog change app link label"
      );
    }

    if (checkContents) {
      checkContents(dialogEl);
    }

    if (actionChangeApp) {
      let dialogClosedPromise = waitForProtocolPermissionDialog(browser, false);
      changeAppLink.click();
      await dialogClosedPromise;
    } else {
      await closeDialog(browser, dialog, actionConfirm, scheme);
    }
  }

  if (chooserDialogOpenPromise) {
    let dialog = await chooserDialogOpenPromise;
    let dialogEl = getDialogElementFromSubDialog(dialog);
    let dialogType = getDialogType(dialog);

    let { hasCheckbox, actionCheckbox, actionConfirm } = chooserDialogOptions;

    await testCheckbox(dialogEl, dialogType, {
      hasCheckbox,
      actionCheckbox,
    });

    await closeDialog(browser, dialog, actionConfirm, scheme);
  }

  if (webHandlerShouldOpen) {
    info("Waiting for web handler to open");
    await webHandlerLoadedPromise;
  } else {
    info("Web handler open canceled");
  }
}

/**
 * Inspects the checkbox state and interacts with it.
 *
 * @param {dialog} dialogEl
 * @param {string} dialogType - String identifier of dialog type.
 * Either "permission" or "chooser".
 * @param {object} options - Test Options.
 * @param {boolean} [options.hasCheckbox] - Whether the dialog is expected to
 * have a visible checkbox.
 * @param {boolean} [options.hasCheckboxState] - The check state of the checkbox
 * to test for. true = checked, false = unchecked.
 * @param {boolean} [options.actionCheckbox] - The state to set on the checkbox.
 * true = checked, false = unchecked.
 */
async function testCheckbox(
  dialogEl,
  dialogType,
  { hasCheckbox, hasCheckboxState = false, actionCheckbox, checkboxOrigin }
) {
  let checkbox = dialogEl.ownerDocument.getElementById("remember");
  if (typeof hasCheckbox == "boolean") {
    is(
      checkbox && BrowserTestUtils.isVisible(checkbox),
      hasCheckbox,
      "Dialog checkbox has correct visibility."
    );

    let checkboxLabel = dialogEl.ownerDocument.getElementById("remember-label");
    is(
      checkbox && BrowserTestUtils.isVisible(checkboxLabel),
      hasCheckbox,
      "Dialog checkbox label has correct visibility."
    );
    if (hasCheckbox) {
      ok(
        !checkboxLabel.innerHTML.toLowerCase().includes(NULL_PRINCIPAL_SCHEME),
        "Dialog checkbox label does not include NullPrincipal scheme."
      );
    }
  }

  if (typeof hasCheckboxState == "boolean") {
    is(checkbox.checked, hasCheckboxState, "Dialog checkbox has correct state");
  }

  if (checkboxOrigin) {
    let doc = dialogEl.ownerDocument;
    let hostFromLabel = doc.l10n.getAttributes(
      doc.getElementById("remember-label")
    ).args.host;
    is(hostFromLabel, checkboxOrigin, "Checkbox should be for correct domain.");
  }

  if (typeof actionCheckbox == "boolean") {
    checkbox.click();
  }
}

/**
 * Wait for the test handler to be opened.
 *
 * @param {MozBrowser} browser - The browser the load should occur in.
 * @param {string} scheme - Scheme which triggered the handler to open.
 */
function waitForHandlerURL(browser, scheme) {
  return BrowserTestUtils.browserLoaded(
    browser,
    false,
    url => url == `${ORIGIN1}/?url=${scheme}%3A%2F%2Ftest`
  );
}

/**
 * Test for open-protocol-handler permission.
 *
 * @param {nsIPrincipal} principal - The principal to test the permission on.
 * @param {string} scheme - Scheme to generate permission key.
 * @param {boolean} hasPerm - Whether we expect the princial to set the
 * permission (true), or not (false).
 */
function testPermission(principal, scheme, hasPerm) {
  let permKey = getSkipProtoDialogPermissionKey(scheme);
  let result = Services.perms.testPermissionFromPrincipal(principal, permKey);
  let message = `${permKey} ${hasPerm ? "is" : "is not"} set for ${
    principal.origin
  }.`;
  is(result == Services.perms.ALLOW_ACTION, hasPerm, message);
}

/**
 * Get the checkbox element of the dialog used to remember the handler choice or
 * store the permission.
 *
 * @param {SubDialog} dialog - Protocol handler dialog embedded in a SubDialog.
 * @param {string} dialogType - Type of the dialog which holds the checkbox.
 * @returns {HTMLInputElement} - Checkbox of the dialog.
 */
function getDialogCheckbox(dialog, dialogType) {
  let id;
  if (dialogType == "permission") {
    id = "remember-permission";
  } else {
    id = "remember";
  }
  return dialog._frame.contentDocument.getElementById(id);
}

function getDialogType(dialog) {
  let url = dialog._frame.currentURI.spec;

  if (url === DIALOG_URL_PERMISSION) {
    return "permission";
  }
  if (url === DIALOG_URL_APP_CHOOSER) {
    return "chooser";
  }
  throw new Error("Dialog with unexpected url");
}

/**
 * Exit a protocol handler SubDialog and wait for it to be fully closed.
 *
 * @param {MozBrowser} browser - Browser element of the tab where the dialog is
 * shown.
 * @param {SubDialog} dialog - SubDialog object which holds the protocol handler
 * @param {boolean} confirm - Whether to confirm (true) or cancel (false) the
 * dialog.
 * @param {string} scheme - The scheme of the protocol the dialog is opened for.
 * dialog.
 */
async function closeDialog(browser, dialog, confirm, scheme) {
  let dialogClosedPromise = waitForSubDialog(browser, dialog._openedURL, false);
  let dialogEl = getDialogElementFromSubDialog(dialog);

  if (confirm) {
    if (getDialogType(dialog) == "chooser") {
      // Select our test protocol handler
      let listItem = dialogEl.ownerDocument.querySelector(
        `richlistitem[name="${scheme}Handler"]`
      );
      listItem.click();
    }

    dialogEl.removeAttribute("buttondisabledaccept");
    dialogEl.acceptDialog();
  } else {
    dialogEl.cancelDialog();
  }

  return dialogClosedPromise;
}

registerCleanupFunction(function () {
  // Clean up test handlers
  [...TEST_PROTOS, WALLET_PROTO].forEach(scheme => {
    let handlerInfo = HandlerServiceTestUtils.getHandlerInfo(scheme);
    gHandlerService.remove(handlerInfo);
  });

  // Clear permissions
  Services.perms.removeAll();
});

add_setup(async function () {
  initTestHandlers();
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.wallet_schemes", WALLET_PROTO]],
  });
});

/**
 * Tests that when "remember" is unchecked, we only allow the protocol to be
 * opened once and don't store any permission.
 */
add_task(async function test_permission_allow_once() {
  for (let scheme of TEST_PROTOS) {
    await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
      await testOpenProto(browser, scheme, {
        permDialogOptions: {
          hasCheckbox: true,
          hasChangeApp: false,
          chooserIsNext: true,
          actionConfirm: true,
        },
        chooserDialogOptions: { hasCheckbox: true, actionConfirm: true },
      });
    });

    // No permission should be set
    testPermission(PRINCIPAL1, scheme, false);
    testPermission(PRINCIPAL2, scheme, false);

    // No preferred app should be set
    testAlwaysAsk(scheme, true);

    // If we open again we should see the permission dialog
    await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
      await testOpenProto(browser, scheme, {
        permDialogOptions: {
          hasCheckbox: true,
          hasChangeApp: false,
          chooserIsNext: true,
          actionConfirm: false,
        },
      });
    });
  }
});

/**
 * Tests that when checking the "remember" checkbox, the protocol permission
 * is set correctly and allows the caller to skip the permission dialog in
 * subsequent calls.
 */
add_task(async function test_permission_allow_persist() {
  for (let [origin, principal] of [
    [ORIGIN1, PRINCIPAL1],
    [ORIGIN3, PRINCIPAL3],
  ]) {
    for (let scheme of TEST_PROTOS) {
      info("Testing with origin " + origin);
      info("testing with principal of origin " + principal.origin);
      info("testing with protocol " + scheme);

      // Set a permission for an unrelated protocol.
      // We should still see the permission dialog.
      Services.perms.addFromPrincipal(
        principal,
        getSkipProtoDialogPermissionKey("foobar"),
        Services.perms.ALLOW_ACTION
      );

      await BrowserTestUtils.withNewTab(origin, async browser => {
        await testOpenProto(browser, scheme, {
          permDialogOptions: {
            hasCheckbox: true,
            hasChangeApp: false,
            chooserIsNext: true,
            actionCheckbox: true,
            actionConfirm: true,
          },
          chooserDialogOptions: { hasCheckbox: true, actionConfirm: true },
        });
      });

      // Permission should be set
      testPermission(principal, scheme, true);
      testPermission(PRINCIPAL2, scheme, false);

      // No preferred app should be set
      testAlwaysAsk(scheme, true);

      // If we open again with the origin where we granted permission, we should
      // directly get the chooser dialog.
      await BrowserTestUtils.withNewTab(origin, async browser => {
        await testOpenProto(browser, scheme, {
          chooserDialogOptions: {
            hasCheckbox: true,
            actionConfirm: false,
          },
        });
      });

      // If we open with the other origin, we should see the permission dialog
      await BrowserTestUtils.withNewTab(ORIGIN2, async browser => {
        await testOpenProto(browser, scheme, {
          permDialogOptions: {
            hasCheckbox: true,
            hasChangeApp: false,
            chooserIsNext: true,
            actionConfirm: false,
          },
        });
      });

      // Cleanup permissions
      Services.perms.removeAll();
    }
  }
});

/**
 * Tests that if a preferred protocol handler is set, the permission dialog
 * shows the application name and a link which leads to the app chooser.
 */
add_task(async function test_permission_application_set() {
  let scheme = TEST_PROTOS[0];
  updateAlwaysAsk(scheme, false);
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      permDialogOptions: {
        hasCheckbox: true,
        hasChangeApp: true,
        chooserIsNext: false,
        actionChangeApp: true,
      },
      chooserDialogOptions: { hasCheckbox: true, actionConfirm: true },
    });
  });

  // Cleanup
  updateAlwaysAsk(scheme, true);
});

/**
 * Tests that we correctly handle system principals. They should always
 * show the permission dialog, but give the option to choose another
 * app if there isn't a default handler.
 */
add_task(async function test_permission_system_principal() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      permDialogOptions: {
        hasCheckbox: false,
        hasChangeApp: false,
        chooserIsNext: true,
        actionChangeApp: false,
      },
      triggerLoad: useTriggeringPrincipal(
        Services.scriptSecurityManager.getSystemPrincipal()
      ),
    });
  });
});

/**
 * Tests that we correctly handle system principals and show
 * a simplified permission dialog if there is a default handler.
 */
add_task(async function test_permission_system_principal_have_default() {
  let scheme = getSystemProtocol();
  if (!scheme) {
    return;
  }
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      permDialogOptions: {
        hasCheckbox: false,
        hasChangeApp: false,
        chooserIsNext: false,
        actionChangeApp: false,
      },
      triggerLoad: useTriggeringPrincipal(
        Services.scriptSecurityManager.getSystemPrincipal()
      ),
    });
  });
});

/**
 * Tests that we directly open the handler if permission and handler are set.
 */
add_task(async function test_app_and_permission_set() {
  let scheme = TEST_PROTOS[1];

  updateAlwaysAsk(scheme, false);
  Services.perms.addFromPrincipal(
    PRINCIPAL2,
    getSkipProtoDialogPermissionKey(scheme),
    Services.perms.ALLOW_ACTION
  );

  await BrowserTestUtils.withNewTab(ORIGIN2, async browser => {
    await testOpenProto(browser, scheme);
  });

  // Cleanup
  Services.perms.removeAll();
  updateAlwaysAsk(scheme, true);
});

/**
 * Tests that the alwaysAsk state is not updated if the user cancels the dialog
 */
add_task(async function test_change_app_checkbox_cancel() {
  let scheme = TEST_PROTOS[0];

  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      permDialogOptions: {
        hasCheckbox: true,
        chooserIsNext: true,
        hasChangeApp: false,
        actionConfirm: true,
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionCheckbox: true, // Activate checkbox
        actionConfirm: false, // Cancel dialog
      },
    });
  });

  // Should not have applied value from checkbox
  testAlwaysAsk(scheme, true);
});

/**
 * Tests that the external protocol dialogs behave correctly when a null
 * principal is passed.
 */
add_task(async function test_null_principal() {
  let scheme = TEST_PROTOS[0];

  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: () => {
        let uri = `${scheme}://test`;
        // TODO: Switch to SpecialPowers.spawn
        // eslint-disable-next-line mozilla/reject-contenttask-spawn
        ContentTask.spawn(browser, { uri }, args => {
          let frame = content.document.createElement("iframe");
          frame.src = `data:text/html,<script>location.href="${args.uri}"</script>`;
          content.document.body.appendChild(frame);
        });
      },
      permDialogOptions: {
        hasCheckbox: false,
        chooserIsNext: true,
        hasChangeApp: false,
        actionConfirm: true,
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Tests that the external protocol dialogs behave correctly when no principal
 * is passed.
 */
add_task(async function test_no_principal() {
  let scheme = TEST_PROTOS[1];

  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: () => {
        let uri = `${scheme}://test`;

        let contentDispatchChooser = Cc[
          "@mozilla.org/content-dispatch-chooser;1"
        ].createInstance(Ci.nsIContentDispatchChooser);

        let handler = HandlerServiceTestUtils.getHandlerInfo(scheme);

        contentDispatchChooser.handleURI(
          handler,
          Services.io.newURI(uri),
          null,
          browser.browsingContext
        );
      },
      permDialogOptions: {
        hasCheckbox: false,
        chooserIsNext: true,
        hasChangeApp: false,
        actionConfirm: true,
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Tests that if a URI scheme has a non-standard protocol, an OS default exists,
 * and the user hasn't selected an alternative only the permission dialog is shown.
 */
add_task(async function test_non_standard_protocol() {
  let scheme = getSystemProtocol();
  if (!scheme) {
    return;
  }
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      permDialogOptions: {
        hasCheckbox: true,
        hasChangeApp: true,
        chooserIsNext: false,
        actionChangeApp: false,
      },
    });
  });
});

/**
 * Tests that we show the permission dialog for extension content scripts.
 */
add_task(async function test_extension_content_script_permission() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    let testExtension;

    await testOpenProto(browser, scheme, {
      triggerLoad: async () => {
        let uri = `${scheme}://test`;

        const EXTENSION_DATA = {
          manifest: {
            content_scripts: [
              {
                matches: [browser.currentURI.spec],
                js: ["navigate.js"],
              },
            ],
            browser_specific_settings: {
              gecko: { id: "allowed@mochi.test" },
            },
          },
          files: {
            "navigate.js": `window.location.href = "${uri}";`,
          },
          useAddonManager: "permanent",
        };

        testExtension = ExtensionTestUtils.loadExtension(EXTENSION_DATA);
        await testExtension.startup();
      },
      permDialogOptions: {
        hasCheckbox: true,
        chooserIsNext: true,
        hasChangeApp: false,
        actionCheckbox: true,
        actionConfirm: true,
        checkContents: dialogEl => {
          let description = dialogEl.querySelector("#description");
          let { id, args } =
            description.ownerDocument.l10n.getAttributes(description);
          is(
            id,
            "permission-dialog-description-extension",
            "Should be using the correct string."
          );
          is(
            args.extension,
            "Generated extension",
            "Should have the correct extension name."
          );
        },
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });

    let extensionPrincipal =
      Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
        {}
      );
    let extensionPrivatePrincipal =
      Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
        { privateBrowsingId: 1 }
      );

    let key = getSkipProtoDialogPermissionKey(scheme);
    is(
      Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
      Services.perms.ALLOW_ACTION,
      "Should have permanently allowed the extension"
    );
    is(
      Services.perms.testPermissionFromPrincipal(
        extensionPrivatePrincipal,
        key
      ),
      Services.perms.UNKNOWN_ACTION,
      "Should not have changed the private principal permission"
    );
    is(
      Services.perms.testPermissionFromPrincipal(PRINCIPAL1, key),
      Services.perms.UNKNOWN_ACTION,
      "Should not have allowed the page"
    );

    await testExtension.unload();

    is(
      Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
      Services.perms.UNKNOWN_ACTION,
      "Should have cleared the extension's normal principal permission"
    );
    is(
      Services.perms.testPermissionFromPrincipal(
        extensionPrivatePrincipal,
        key
      ),
      Services.perms.UNKNOWN_ACTION,
      "Should have cleared the private browsing principal"
    );
  });
});

/**
 * Tests that we show the permission dialog for extension content scripts.
 */
add_task(async function test_extension_private_content_script_permission() {
  let scheme = TEST_PROTOS[0];
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  await BrowserTestUtils.withNewTab(
    { gBrowser: win.gBrowser, url: ORIGIN1 },
    async browser => {
      let testExtension;

      await testOpenProto(browser, scheme, {
        triggerLoad: async () => {
          let uri = `${scheme}://test`;

          const EXTENSION_DATA = {
            manifest: {
              content_scripts: [
                {
                  matches: [browser.currentURI.spec],
                  js: ["navigate.js"],
                },
              ],
              browser_specific_settings: {
                gecko: { id: "allowed@mochi.test" },
              },
            },
            files: {
              "navigate.js": `window.location.href = "${uri}";`,
            },
            useAddonManager: "permanent",
          };

          testExtension = ExtensionTestUtils.loadExtension(EXTENSION_DATA);
          await testExtension.startup();
          let perms = {
            permissions: ["internal:privateBrowsingAllowed"],
            origins: [],
          };
          await ExtensionPermissions.add("allowed@mochi.test", perms);
          let addon = await AddonManager.getAddonByID("allowed@mochi.test");
          await addon.reload();
        },
        permDialogOptions: {
          hasCheckbox: true,
          chooserIsNext: true,
          hasChangeApp: false,
          actionCheckbox: true,
          actionConfirm: true,
          checkContents: dialogEl => {
            let description = dialogEl.querySelector("#description");
            let { id, args } =
              description.ownerDocument.l10n.getAttributes(description);
            is(
              id,
              "permission-dialog-description-extension",
              "Should be using the correct string."
            );
            is(
              args.extension,
              "Generated extension",
              "Should have the correct extension name."
            );
          },
        },
        chooserDialogOptions: {
          hasCheckbox: true,
          actionConfirm: false, // Cancel dialog
        },
      });

      let extensionPrincipal =
        Services.scriptSecurityManager.createContentPrincipal(
          Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
          {}
        );
      let extensionPrivatePrincipal =
        Services.scriptSecurityManager.createContentPrincipal(
          Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
          { privateBrowsingId: 1 }
        );

      let key = getSkipProtoDialogPermissionKey(scheme);
      is(
        Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
        Services.perms.UNKNOWN_ACTION,
        "Should not have changed the extension's normal principal permission"
      );
      is(
        Services.perms.testPermissionFromPrincipal(
          extensionPrivatePrincipal,
          key
        ),
        Services.perms.ALLOW_ACTION,
        "Should have allowed the private browsing principal"
      );
      is(
        Services.perms.testPermissionFromPrincipal(PRINCIPAL1, key),
        Services.perms.UNKNOWN_ACTION,
        "Should not have allowed the page"
      );

      await testExtension.unload();

      is(
        Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
        Services.perms.UNKNOWN_ACTION,
        "Should have cleared the extension's normal principal permission"
      );
      is(
        Services.perms.testPermissionFromPrincipal(
          extensionPrivatePrincipal,
          key
        ),
        Services.perms.UNKNOWN_ACTION,
        "Should have cleared the private browsing principal"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that we do not show the permission dialog for extension content scripts
 * when the page already has permission.
 */
add_task(async function test_extension_allowed_content() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    let testExtension;

    let key = getSkipProtoDialogPermissionKey(scheme);
    Services.perms.addFromPrincipal(
      PRINCIPAL1,
      key,
      Services.perms.ALLOW_ACTION,
      Services.perms.EXPIRE_NEVER
    );

    await testOpenProto(browser, scheme, {
      triggerLoad: async () => {
        let uri = `${scheme}://test`;

        const EXTENSION_DATA = {
          manifest: {
            content_scripts: [
              {
                matches: [browser.currentURI.spec],
                js: ["navigate.js"],
              },
            ],
          },
          files: {
            "navigate.js": `window.location.href = "${uri}";`,
          },
        };

        testExtension = ExtensionTestUtils.loadExtension(EXTENSION_DATA);
        await testExtension.startup();
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });

    let extensionPrincipal =
      Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
        {}
      );

    is(
      Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
      Services.perms.UNKNOWN_ACTION,
      "Should not have permanently allowed the extension"
    );

    await testExtension.unload();
    Services.perms.removeFromPrincipal(PRINCIPAL1, key);
  });
});

/**
 * Tests that we do not show the permission dialog for extension content scripts
 * when the extension already has permission.
 */
add_task(async function test_extension_allowed_extension() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    let testExtension;

    let key = getSkipProtoDialogPermissionKey(scheme);

    await testOpenProto(browser, scheme, {
      triggerLoad: async () => {
        const EXTENSION_DATA = {
          manifest: {
            permissions: [`${ORIGIN1}/*`],
          },
          background() {
            browser.test.onMessage.addListener(async (msg, uri) => {
              switch (msg) {
                case "engage":
                  browser.tabs.executeScript({
                    code: `window.location.href = "${uri}";`,
                  });
                  break;
                default:
                  browser.test.fail(`Unexpected message received: ${msg}`);
              }
            });
          },
        };

        testExtension = ExtensionTestUtils.loadExtension(EXTENSION_DATA);
        await testExtension.startup();

        let extensionPrincipal =
          Services.scriptSecurityManager.createContentPrincipal(
            Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
            {}
          );
        Services.perms.addFromPrincipal(
          extensionPrincipal,
          key,
          Services.perms.ALLOW_ACTION,
          Services.perms.EXPIRE_NEVER
        );

        testExtension.sendMessage("engage", `${scheme}://test`);
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });

    await testExtension.unload();
    Services.perms.removeFromPrincipal(PRINCIPAL1, key);
  });
});

/**
 * Tests that we show the permission dialog for extensions directly opening a
 * protocol.
 */
add_task(async function test_extension_principal() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    let testExtension;

    await testOpenProto(browser, scheme, {
      triggerLoad: async () => {
        const EXTENSION_DATA = {
          background() {
            browser.test.onMessage.addListener(async (msg, url) => {
              switch (msg) {
                case "engage":
                  browser.tabs.update({
                    url,
                  });
                  break;
                default:
                  browser.test.fail(`Unexpected message received: ${msg}`);
              }
            });
          },
        };

        testExtension = ExtensionTestUtils.loadExtension(EXTENSION_DATA);
        await testExtension.startup();
        testExtension.sendMessage("engage", `${scheme}://test`);
      },
      permDialogOptions: {
        hasCheckbox: true,
        chooserIsNext: true,
        hasChangeApp: false,
        actionCheckbox: true,
        actionConfirm: true,
        checkContents: dialogEl => {
          let description = dialogEl.querySelector("#description");
          let { id, args } =
            description.ownerDocument.l10n.getAttributes(description);
          is(
            id,
            "permission-dialog-description-extension",
            "Should be using the correct string."
          );
          is(
            args.extension,
            "Generated extension",
            "Should have the correct extension name."
          );
        },
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });

    let extensionPrincipal =
      Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(`moz-extension://${testExtension.uuid}/`),
        {}
      );

    let key = getSkipProtoDialogPermissionKey(scheme);
    is(
      Services.perms.testPermissionFromPrincipal(extensionPrincipal, key),
      Services.perms.ALLOW_ACTION,
      "Should have permanently allowed the extension"
    );
    is(
      Services.perms.testPermissionFromPrincipal(PRINCIPAL1, key),
      Services.perms.UNKNOWN_ACTION,
      "Should not have allowed the page"
    );

    await testExtension.unload();
  });
});

/**
 * Test that we use the redirect principal for the dialog when applicable.
 */
add_task(async function test_redirect_principal() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: useServerRedirect("location"),
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Test that we use the redirect principal for the dialog for refresh headers.
 */
add_task(async function test_redirect_principal_refresh_header() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: useServerRedirect("refresh"),
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Test that we use the redirect principal for the dialog for meta refreshes.
 */
add_task(async function test_redirect_principal_meta() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: useServerRedirect("meta-refresh"),
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Test that we use the redirect principal for the dialog for JS redirects.
 */
add_task(async function test_redirect_principal_js() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: () => {
        let uri = `${scheme}://test`;

        let innerParams = new URLSearchParams();
        innerParams.set("uri", uri);
        let params = new URLSearchParams();
        params.set(
          "uri",
          "https://example.com/" +
            ROOT_PATH +
            "script_redirect.html?" +
            innerParams.toString()
        );
        uri =
          "https://example.org/" +
          ROOT_PATH +
          "script_redirect.html?" +
          params.toString();
        BrowserTestUtils.startLoadingURIString(browser, uri);
      },
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Test that we use the redirect principal for the dialog for link clicks.
 */
add_task(async function test_redirect_principal_links() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad: async () => {
        let uri = `${scheme}://test`;

        let params = new URLSearchParams();
        params.set("uri", uri);
        uri =
          "https://example.com/" +
          ROOT_PATH +
          "redirect_helper.sjs?" +
          params.toString();
        // TODO: Switch to SpecialPowers.spawn
        // eslint-disable-next-line mozilla/reject-contenttask-spawn
        await ContentTask.spawn(browser, { uri }, args => {
          let textLink = content.document.createElement("a");
          textLink.href = args.uri;
          textLink.textContent = "click me";
          content.document.body.appendChild(textLink);
          textLink.click();
        });
      },
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

add_task(async function test_unloaded_iframe() {
  let scheme = TEST_PROTOS[0];
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, scheme, {
      triggerLoad() {
        let uri = `${scheme}://test`;
        // TODO: Switch to SpecialPowers.spawn
        // eslint-disable-next-line mozilla/reject-contenttask-spawn
        return ContentTask.spawn(browser, { uri }, args => {
          let frame = content.document.createElement("iframe");
          frame.setAttribute("loading", "lazy");
          frame.setAttribute("src", "about:blank");
          frame.setAttribute("style", "margin-top: 10000px;");
          frame.setAttribute("name", "yo");
          content.document.body.append(frame);
          // Navigate...
          content.open(args.uri, "yo");
          // Then remove the iframe again so that we can't find a
          // currentWindowGlobal for the BC once we show the dialog.
          frame.remove();
        });
      },
      permDialogOptions: {
        checkboxOrigin: ORIGIN1,
        chooserIsNext: true,
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });
});

/**
 * Tests that we show a warning UI element for a wallet scheme
 */
add_task(async function test_prompt_warning_for_wallet_scheme() {
  // Test that we show the warning in the simple case.
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, WALLET_PROTO, {
      permDialogOptions: {
        hasCheckbox: true,
        hasChangeApp: false,
        chooserIsNext: true,
        actionConfirm: true,
        hasWalletWarning: true,
      },
      chooserDialogOptions: { hasCheckbox: true, actionConfirm: true },
    });
  });

  // Test that we show the warning with a null principal
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, WALLET_PROTO, {
      triggerLoad: () => {
        let uri = `${WALLET_PROTO}://test`;
        // TODO: Switch to SpecialPowers.spawn
        // eslint-disable-next-line mozilla/reject-contenttask-spawn
        ContentTask.spawn(browser, { uri }, args => {
          let frame = content.document.createElement("iframe");
          frame.src = `data:text/html,<script>location.href="${args.uri}"</script>`;
          content.document.body.appendChild(frame);
        });
      },
      permDialogOptions: {
        hasCheckbox: false,
        chooserIsNext: true,
        hasChangeApp: false,
        actionConfirm: true,
        hasWalletWarning: true,
      },
      chooserDialogOptions: {
        hasCheckbox: true,
        actionConfirm: false, // Cancel dialog
      },
    });
  });

  // Test that we show the warning with a system principal
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, WALLET_PROTO, {
      permDialogOptions: {
        hasCheckbox: false,
        hasChangeApp: false,
        chooserIsNext: true,
        actionChangeApp: false,
        hasWalletWarning: true,
      },
      triggerLoad: useTriggeringPrincipal(
        Services.scriptSecurityManager.getSystemPrincipal()
      ),
    });
  });

  // Test that we don't show the warning for another scheme.
  await BrowserTestUtils.withNewTab(ORIGIN1, async browser => {
    await testOpenProto(browser, TEST_PROTOS[0], {
      permDialogOptions: {
        hasCheckbox: true,
        hasChangeApp: false,
        chooserIsNext: true,
        actionConfirm: true,
        hasWalletWarning: false,
      },
      chooserDialogOptions: { hasCheckbox: true, actionConfirm: true },
    });
  });
});
