/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Return card containers matching a given name
function getCardsByName(aContainer, aLeafName) {
  const matchedCards = [];
  const allCards = aContainer.querySelectorAll(".card");
  for (const card of allCards) {
    const nameLabel = card.querySelector(".module-name");
    if (nameLabel.textContent == aLeafName) {
      matchedCards.push(card);
    }
  }
  return matchedCards;
}

function getDetailRow(aContainer, aLabel) {
  return aContainer.querySelector(`[data-l10n-id=${aLabel}]`).parentElement;
}

function verifyClipboardData(aModuleJson) {
  Assert.ok(
    aModuleJson.hasOwnProperty("blocked"),
    "Clipboard data should have blocked property."
  );
  const blocked = aModuleJson.blocked.filter(
    x => x.name == kUserBlockedModuleName
  );
  Assert.equal(
    blocked.length,
    1,
    "Blocked array should contain the blocked module one time."
  );
  Assert.ok(
    aModuleJson.hasOwnProperty("modules"),
    "Clipboard data should have modules property"
  );
  let aModuleArray = aModuleJson.modules;
  const filtered = aModuleArray.filter(x => x.name == kExtensionModuleName);
  Assert.equal(filtered.length, 1, "No duplicate data for the module.");

  const kDeletedPropertiesOfModule = [
    "application",
    "dllFile",
    "loadingOnMain",
    "trustFlags",
  ];
  const kDeletedPropertiesOfLoadingEvent = [
    "baseAddress",
    "isDependent",
    "mainThread",
    "moduleIndex",
    "processUptimeMS",
  ];

  for (const module of aModuleArray) {
    for (const deletedProperty of kDeletedPropertiesOfModule) {
      Assert.ok(
        !module.hasOwnProperty(deletedProperty),
        `The property \`${deletedProperty}\` is deleted.`
      );
    }

    Assert.ok(
      !module.hasOwnProperty("typeFlags") || module.typeFlags != 0,
      "typeFlags does not exist or is non-zero."
    );

    for (const event of module.events) {
      for (const deletedProperty of kDeletedPropertiesOfLoadingEvent) {
        Assert.ok(
          !event.hasOwnProperty(deletedProperty),
          `The property \`${deletedProperty}\` is deleted.`
        );
      }
    }
  }
}

function verifyModuleSorting(compareFunc) {
  const uninteresting = {
    typeFlags: 0,
    isCrasher: false,
    loadingOnMain: 0,
  };
  const crasherNotBlocked = { ...uninteresting, isCrasher: true };
  const crasherBlocked = {
    ...uninteresting,
    isCrasher: true,
    typeFlags: Ci.nsIAboutThirdParty.ModuleType_BlockedByUserAtLaunch,
  };
  const justBlocked = {
    ...uninteresting,
    typeFlags: Ci.nsIAboutThirdParty.ModuleType_BlockedByUserAtLaunch,
  };
  const uninterestingButSlow = {
    ...uninteresting,
    loadingOnMain: 10,
  };
  let modules = [
    uninteresting,
    uninterestingButSlow,
    crasherNotBlocked,
    justBlocked,
    crasherBlocked,
  ];
  modules.sort(compareFunc);
  Assert.equal(
    JSON.stringify([
      crasherBlocked,
      justBlocked,
      crasherNotBlocked,
      uninterestingButSlow,
      uninteresting,
    ]),
    JSON.stringify(modules),
    "Modules sort in expected order"
  );
}

add_task(async () => {
  registerCleanupFunction(() => {
    unregisterAll();
  });
  await registerObject();
  registerExtensions();
  loadShellExtension();

  await kATP.collectSystemInfo();
  Assert.equal(
    kATP.lookupModuleType(kExtensionModuleName),
    Ci.nsIAboutThirdParty.ModuleType_ShellExtension,
    "lookupModuleType() returns a correct type " +
      "after system info was collected."
  );

  await BrowserTestUtils.withNewTab("about:third-party", async () => {
    if (!content.fetchDataDone) {
      const mainDiv = content.document.getElementById("main");
      await BrowserTestUtils.waitForMutationCondition(
        mainDiv,
        { childList: true },
        () => mainDiv.childElementCount > 0
      );
      Assert.ok(content.fetchDataDone, "onLoad() is completed.");
    }

    const reload = content.document.getElementById("button-reload");
    if (!reload.hidden) {
      reload.click();
      await BrowserTestUtils.waitForMutationCondition(
        reload,
        { attributes: true, attributeFilter: ["hidden"] },
        () => reload.hidden
      );
    }

    Assert.ok(
      content.document.getElementById("no-data").hidden,
      "The no-data message is hidden."
    );
    const blockedCards = getCardsByName(
      content.document,
      kUserBlockedModuleName
    );
    Assert.equal(
      blockedCards.length,
      1,
      "Only one card matching the blocked module exists."
    );
    const blockedCard = blockedCards[0];
    Assert.equal(
      blockedCard.querySelectorAll(".button-block.module-blocked").length,
      1,
      "The blocked module has a button indicating it is blocked"
    );
    let blockedBlockButton = blockedCard.querySelector(
      ".button-block.module-blocked"
    );
    Assert.equal(
      blockedBlockButton.getAttribute("data-l10n-id"),
      "third-party-button-to-unblock-module",
      "Button to block the module has correct title"
    );
    blockedBlockButton.click();
    await BrowserTestUtils.promiseAlertDialogOpen("cancel");
    Assert.ok(
      !blockedBlockButton.classList.contains("module-blocked"),
      "After clicking to unblock a module, button should not have module-blocked class."
    );
    Assert.equal(
      blockedBlockButton.getAttribute("data-l10n-id"),
      "third-party-button-to-block-module",
      "After clicking to unblock a module, button should have correct title."
    );
    // Restore this to blocked for later tests
    blockedBlockButton.click();
    await BrowserTestUtils.promiseAlertDialogOpen("cancel");
    Assert.ok(
      blockedBlockButton.classList.contains("module-blocked"),
      "After clicking to block a module, button should have module-blocked class."
    );
    Assert.equal(
      blockedBlockButton.getAttribute("data-l10n-id"),
      "third-party-button-to-unblock-module",
      "After clicking to block a module, button should have correct title."
    );

    const cards = getCardsByName(content.document, kExtensionModuleName);
    Assert.equal(cards.length, 1, "Only one card matching the module exists.");
    const card = cards[0];

    const blockButton = card.querySelector(".button-block");
    Assert.ok(
      !blockButton.classList.contains("blocklist-disabled"),
      "Button to block the module does not indicate the blocklist is disabled."
    );
    Assert.ok(
      !blockButton.classList.contains("module-blocked"),
      "Button to block the module does not indicate the module is blocked."
    );

    Assert.ok(
      card.querySelector(".image-warning").hidden,
      "No warning sign for the module."
    );
    Assert.equal(
      card.querySelector(".image-unsigned").hidden,
      false,
      "The module is labeled as unsigned."
    );
    Assert.equal(
      card.querySelector(".tag-shellex").hidden,
      false,
      "The module is labeled as a shell extension."
    );
    Assert.equal(
      card.querySelector(".tag-ime").hidden,
      true,
      "The module is not labeled as an IME."
    );

    const versionRow = getDetailRow(card, "third-party-detail-version");
    Assert.equal(
      versionRow.childNodes[1].textContent,
      "1.2.3.4",
      "The version matches a value in TestShellEx.rc."
    );
    const vendorRow = getDetailRow(card, "third-party-detail-vendor");
    Assert.equal(
      vendorRow.childNodes[1].textContent,
      "Mozilla Corporation",
      "The vendor name matches a value in TestShellEx.rc."
    );
    // The page lists every load event of the session, so the events of
    // earlier runs of this test in the same browser are listed too.
    const occurrencesRow = getDetailRow(card, "third-party-detail-occurrences");
    const occurrences = Number(occurrencesRow.childNodes[1].textContent);
    Assert.greaterOrEqual(occurrences, 1, "The module was loaded.");
    const durationRow = getDetailRow(card, "third-party-detail-duration");
    Assert.ok(
      Number(durationRow.childNodes[1].textContent),
      "The duration row shows a valid number."
    );

    const eventRows = card.querySelectorAll(".event-table > tbody > tr");
    Assert.equal(
      eventRows.length,
      occurrences,
      "The table has one row per event."
    );
    let durationSum = 0;
    for (const row of eventRows) {
      const tableCells = row.querySelectorAll("td");
      Assert.equal(
        tableCells[0]
          .querySelector(".process-type")
          .getAttribute("data-l10n-id"),
        "process-type-default",
        "The module was loaded into the main process."
      );
      Assert.ok(
        Number(tableCells[0].querySelector(".process-id").textContent),
        "A valid process ID is displayed."
      );
      Assert.equal(
        tableCells[1].querySelector(".tag-background").hidden,
        true,
        "The icon handler is loaded in the main thread."
      );
      Assert.equal(
        tableCells[2].getAttribute("data-l10n-id"),
        "third-party-status-loaded",
        "The module was really loaded without being blocked."
      );
      durationSum += Number(
        tableCells[1].querySelector(".event-duration").textContent
      );
    }
    Assert.less(
      Math.abs(
        Number(durationRow.childNodes[1].textContent) -
          durationSum / occurrences
      ),
      0.001,
      "The average duration is the average of the events' durations."
    );

    const button = content.document.getElementById("button-copy-to-clipboard");
    button.click();

    // Wait until copying is done and the button becomes clickable.
    await BrowserTestUtils.waitForMutationCondition(
      button,
      { attributes: true },
      () => !button.disabled
    );

    const copiedJSON = JSON.parse(await navigator.clipboard.readText());
    Assert.ok(copiedJSON instanceof Object, "Data is an object.");
    verifyClipboardData(copiedJSON);

    verifyModuleSorting(content.moduleCompareForDisplay);
  });
});
