/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Test that the Report Broken Site errors messages are shown on
 * the UI if the user enters an invalid URL or clicks the send
 * button while it is disabled due to not selecting a "reason"
 */

"use strict";

add_common_setup();

add_task(async function test_invalid_user_inputs() {
  ensureReportBrokenSitePreffedOn();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    for (const menu of [AppMenu(), ProtectionsPanel(), HelpMenu()]) {
      const rbs = await menu.openReportBrokenSite();

      // test that the first slide only allows progression if the URL is valid.
      await isNotVisible(
        rbs.urlComponent.errorMessage,
        "no URL error message by default"
      );

      let test = "empty URL";
      rbs.setURL("");
      await isVisible(rbs.urlComponent.errorMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      test = "valid URL";
      rbs.setURL("https://asdf");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isNotDisabled(rbs.progressionButtons, test);

      test = "invalid URL";
      rbs.setURL("http:/ /asdf");
      await isVisible(rbs.urlComponent.errorMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      test = "back to valid URL";
      rbs.setURL("https://asdf");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isNotDisabled(rbs.progressionButtons, test);

      await rbs.clickReason("load");

      // test that the second slide only allows progression if the URL and description are both valid.
      test = "empty URL";
      rbs.setURL("");
      await isNotVisible(rbs.descriptionInvalidMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      // all-whitespace comments are invalid
      test = "all-whitespace comment";
      rbs.setDescription("            ");
      await isVisible(rbs.urlComponent.errorMessage, test);
      await isVisible(rbs.descriptionInvalidMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      test = "still all-whitespace comment";
      rbs.setURL("https://asdf");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isVisible(rbs.descriptionInvalidMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      test = "comment too short";
      // a minimum number of non-space characters is required
      rbs.setDescription("   ___");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isVisible(rbs.descriptionInvalidMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      // if adding a comment, must have minimum number of valid characters
      test = "valid URL and comment";
      rbs.setDescription("    ____________");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isNotVisible(rbs.descriptionInvalidMessage, test);
      await isNotDisabled(rbs.progressionButtons, test);

      test = "empty but required comment";
      await rbs.clickBack("");
      await rbs.clickReason("other");

      // test that the second slide requires a comment if "something else" is chosen.
      rbs.setDescription("");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isVisible(rbs.descriptionInvalidMessage, test);
      await isDisabled(rbs.progressionButtons, test);

      test = "valid comment";
      rbs.setDescription("    ____________");
      await isNotVisible(rbs.urlComponent.errorMessage, test);
      await isNotVisible(rbs.descriptionInvalidMessage, test);
      await isNotDisabled(rbs.progressionButtons, test);

      await rbs.clickCancel();
    }
  });
});

add_task(async function test_description_instructions() {
  ensureReportBrokenSitePreffedOn();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    const menu = AppMenu();
    for (const [reason, l10nId] of [
      ["load", "report-broken-site-panel-instructions-optional"],
      ["other", "report-broken-site-panel-instructions-other"],
    ]) {
      const rbs = await menu.openReportBrokenSiteToDetailsPanel({ reason });
      is(
        rbs.detailsView
          .querySelector("#report-broken-site-details-instructions")
          .getAttribute("data-l10n-id"),
        l10nId,
        `The ${reason} reason shows the matching instructions`
      );
      await rbs.clickCancel();
    }
  });
});
