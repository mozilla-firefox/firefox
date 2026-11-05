/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(
  /**
   * Tests that the redesigned history setting-group's
   * search-l10n-ids list covers every history mode's description string.
   */
  async function history_search_l10n_ids_description_strings() {
    await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });

    let historyMode = gBrowser.contentDocument.querySelector(
      "setting-group[groupid='history'] #historyMode"
    );
    ok(historyMode, "historyMode element is rendered");

    let attr = historyMode.getAttribute("search-l10n-ids");
    ok(attr, "historyMode has a search-l10n-ids attribute");

    let searchIds = attr.split(",").map(s => s.trim());

    for (let id of [
      "history-remember-description4",
      "history-dontremember-description4",
      "history-custom-description4",
    ]) {
      ok(
        searchIds.includes(id),
        `search-l10n-ids on historyMode includes ${id}`
      );
    }

    await assertRadioGroupAccessibleName(historyMode);

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
);
