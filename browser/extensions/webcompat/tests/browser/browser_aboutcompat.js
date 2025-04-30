"use strict";

add_task(async function test_about_compat_loads_properly() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:compat",
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("#interventions tr[data-id]"),
      "interventions are listed"
    );
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("#smartblock tr[data-id]"),
      "SmartBlock shims are listed"
    );
    ok(true, "Interventions are listed");

    // also choose an intervention and a shim with content-scripts, and confirm that toggling them
    // on and off works (by checking that their content-scripts are de-registered and re-registered).
    const bgWin = content.wrappedJSObject.browser.extension.getBackgroundPage();
    const interventionWithContentScripts =
      bgWin.interventions._availableInterventions.find(
        i => i.active && i.interventions?.find(v => v.content_scripts)
      );
    const shimWithContentScripts = [...bgWin.shims.shims.values()].find(
      s => s._contentScriptRegistrations.length
    );

    async function findRegisteredScript(id) {
      return (
        await content.wrappedJSObject.browser.scripting.getRegisteredContentScripts(
          { ids: [id] }
        )
      )[0];
    }

    // both should have their content scripts registered at startup
    const interventionRCSId = `webcompat intervention for ${interventionWithContentScripts.label}`;
    const shimRCSId = `shim-${shimWithContentScripts.id}-0`;
    ok(
      await findRegisteredScript(interventionRCSId),
      `Found registered script for intervention: '${interventionRCSId}'`
    );
    ok(
      await findRegisteredScript(shimRCSId),
      `Found registered script for shim: '${shimRCSId}'`
    );

    async function testToggling(
      buttonEnabled,
      buttonDisabled,
      interventionOrShim,
      rcsId,
      type
    ) {
      // click to disable the intervention/shim
      content.document.querySelector(buttonEnabled).click();
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(buttonDisabled),
        `toggle button for ${type} now says 'enable'`
      );

      await ContentTaskUtils.waitForCondition(
        () => !interventionOrShim.active && !interventionOrShim.enabled,
        `${type} is inactive`
      );

      // verify that its content scripts have been de-registered
      ok(
        !(await findRegisteredScript(rcsId)),
        `Found no registered script for ${type}: '${rcsId}'`
      );

      // click to re-enable the intervention/shim
      content.document.querySelector(buttonDisabled).click();
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(buttonEnabled),
        `toggle button for ${type} again says 'disable'`
      );

      await ContentTaskUtils.waitForCondition(
        () => interventionOrShim.active || interventionOrShim.enabled,
        `${type} is active`
      );

      // verify that its content scripts have been re-registered
      ok(
        await findRegisteredScript(rcsId),
        `Found registered script for ${type}: '${rcsId}'`
      );
    }

    // toggle the intervention
    await testToggling(
      `tr[data-id='${interventionWithContentScripts.id}'] button[data-l10n-id=label-disable]`,
      `tr[data-id='${interventionWithContentScripts.id}'] button[data-l10n-id=label-enable]`,
      interventionWithContentScripts,
      interventionRCSId,
      "intervention"
    );

    // toggle the shim
    await testToggling(
      `tr[data-id='${shimWithContentScripts.id}'] button[data-l10n-id=label-disable]`,
      `tr[data-id='${shimWithContentScripts.id}'] button[data-l10n-id=label-enable]`,
      shimWithContentScripts,
      shimRCSId,
      "shim"
    );
  });

  await BrowserTestUtils.removeTab(tab);
});
