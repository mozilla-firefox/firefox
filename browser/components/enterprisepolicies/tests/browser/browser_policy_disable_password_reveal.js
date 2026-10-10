/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_hidden_reveal_password() {
  let login = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
    Ci.nsILoginInfo
  );
  login.init("https://example.com", "", null, "username", "password");
  login.QueryInterface(Ci.nsILoginMetaInfo);
  login.timePasswordChanged = Date.now();
  await Services.logins.addLoginAsync(login);

  await setupPolicyEngineWithJson({
    policies: {
      DisablePasswordReveal: true,
    },
  });

  let aboutLoginsTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "about:logins",
  });

  let browser = gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.documentElement.classList.contains("initialized"),
      "Waiting for about:logins to be initialized"
    );

    let loginItem = Cu.waiveXrays(content.document.querySelector("login-item"));

    let passwordReveal = loginItem.shadowRoot.querySelector(
      ".reveal-password-checkbox"
    );
    is(passwordReveal.hidden, true, "Password reveal button should be hidden");
  });
  BrowserTestUtils.removeTab(aboutLoginsTab);

  await Services.logins.removeAllLoginsAsync();
});

add_task(async function test_bug_1696948() {
  await setupPolicyEngineWithJson({
    policies: {
      DisablePasswordReveal: true,
    },
  });

  let aboutLoginsTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "about:logins",
  });

  let browser = gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.documentElement.classList.contains("initialized"),
      "Waiting for about:logins to be initialized"
    );

    let loginList = Cu.waiveXrays(content.document.querySelector("login-list"));
    let createButton = loginList._createLoginButton;
    ok(
      !createButton.disabled,
      "Create button should not be disabled initially"
    );
    let loginItem = Cu.waiveXrays(content.document.querySelector("login-item"));

    createButton.click();

    let passwordReveal = loginItem.shadowRoot.querySelector(
      ".reveal-password-checkbox"
    );
    is(passwordReveal.hidden, true, "Password reveal button should be hidden");

    // Bug 1696948
    let passwordInput = loginItem.shadowRoot.querySelector(
      "input[name='password']"
    );
    isnot(passwordInput, null, "Password field should be in the DOM");
  });
  BrowserTestUtils.removeTab(aboutLoginsTab);
});

add_task(async function test_context_menus_hide_reveal_password() {
  await setupPolicyEngineWithJson({
    policies: {
      DisablePasswordReveal: true,
    },
  });

  await BrowserTestUtils.withNewTab(
    "data:text/html,<input type='password' id='pw'>",
    async browser => {
      let contextMenu = document.getElementById("contentAreaContextMenu");
      let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
      await BrowserTestUtils.synthesizeMouseAtCenter(
        "#pw",
        { type: "contextmenu", button: 2 },
        browser
      );
      await popupShown;
      ok(
        document.getElementById("context-reveal-password").hidden,
        "Reveal Password should be hidden in the content context menu"
      );
      let popupHidden = BrowserTestUtils.waitForEvent(
        contextMenu,
        "popuphidden"
      );
      contextMenu.hidePopup();
      await popupHidden;
    }
  );

  let input = document.createElementNS("http://www.w3.org/1999/xhtml", "input");
  input.type = "password";
  document.documentElement.appendChild(input);
  let popupShown = BrowserTestUtils.waitForEvent(
    document,
    "popupshown",
    false,
    e => e.target.id == "textbox-contextmenu"
  );
  input.dispatchEvent(
    new MouseEvent("contextmenu", { bubbles: true, cancelable: true })
  );
  await popupShown;
  let popup = document.getElementById("textbox-contextmenu");
  ok(
    popup.querySelector("#textbox-contextmenu-reveal-password").hidden,
    "Reveal Password should be hidden in the chrome text context menu"
  );
  let popupHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
  popup.hidePopup();
  await popupHidden;
  input.remove();

  is(
    Services.prefs.getBoolPref("layout.forms.reveal-password-button.enabled"),
    false,
    "Reveal password button pref should be false"
  );
  ok(
    Services.prefs.prefIsLocked("layout.forms.reveal-password-button.enabled"),
    "Reveal password button pref should be locked"
  );
});
