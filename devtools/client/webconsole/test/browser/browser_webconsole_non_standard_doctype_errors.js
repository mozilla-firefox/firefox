/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that warning messages are displayed for documents with non-standards doctype

const QUIRKY_DOCTYPE = "<!DOCTYPE xhtml2>";
const ALMOST_STANDARD_DOCTYPE = `<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN""http://www.w3.org/TR/html4/loose.dtd">`;
const STANDARD_DOCTYPE = "<!DOCTYPE html>";

const TEST_URI_QUIRKY_DOCTYPE = `data:text/html,${QUIRKY_DOCTYPE}<meta charset="utf8"><h1>Quirky</h1>`;
const TEST_URI_ALMOST_STANDARD_DOCTYPE = `data:text/html,${ALMOST_STANDARD_DOCTYPE}<meta charset="utf8"><h1>Almost standard</h1>`;
const TEST_URI_NO_DOCTYPE = `data:text/html,<meta charset="utf8"><h1>No DocType</h1>`;
const TEST_URI_STANDARD_DOCTYPE = `data:text/html,${STANDARD_DOCTYPE}<meta charset="utf8"><h1>Standard</h1>`;

const LEARN_MORE_URI =
  "https://developer.mozilla.org/docs/Web/HTML/Guides/Quirks_Mode_and_Standards_Mode" +
  DOCS_GA_PARAMS;

add_task(async function () {
  info("Navigate to page with quirky doctype");
  const hud = await openNewTabAndConsole(TEST_URI_QUIRKY_DOCTYPE);

  const quirkyDocTypeMessage = await waitFor(() =>
    findWarningMessage(
      hud,
      `This page is in Quirks Mode. Page layout may be impacted. For Standards Mode use “<!DOCTYPE html>”`
    )
  );
  ok(!!quirkyDocTypeMessage, "Quirky doctype warning message is visible");

  info("Clicking on the Learn More link");
  const quirkyDocTypeMessageLearnMoreLink =
    quirkyDocTypeMessage.querySelector(".learn-more-link");
  let linkSimulation = await simulateLinkClick(
    quirkyDocTypeMessageLearnMoreLink
  );

  is(
    linkSimulation.link,
    LEARN_MORE_URI,
    `Clicking the provided link opens expected URL`
  );
  is(linkSimulation.where, "tab", `Clicking the provided link opens in a tab`);

  info("Navigate to page with almost standard doctype");
  await navigateTo(TEST_URI_ALMOST_STANDARD_DOCTYPE);

  const almostStandardDocTypeMessage = await waitFor(() =>
    findWarningMessage(
      hud,
      `This page is in Almost Standards Mode. Page layout may be impacted. For Standards Mode use “<!DOCTYPE html>”`
    )
  );
  ok(
    !!almostStandardDocTypeMessage,
    "Almost standards mode doctype warning message is visible"
  );

  info("Clicking on the Learn More link");
  const almostStandardDocTypeMessageLearnMoreLink =
    almostStandardDocTypeMessage.querySelector(".learn-more-link");
  linkSimulation = await simulateLinkClick(
    almostStandardDocTypeMessageLearnMoreLink
  );

  is(
    linkSimulation.link,
    LEARN_MORE_URI,
    `Clicking the provided link opens expected URL`
  );
  is(linkSimulation.where, "tab", `Clicking the provided link opens in a tab`);

  info("Navigate to page with no doctype");
  await navigateTo(TEST_URI_NO_DOCTYPE);

  const noDocTypeMessage = await waitFor(() =>
    findWarningMessage(
      hud,
      `This page is in Quirks Mode. Page layout may be impacted. For Standards Mode use “<!DOCTYPE html>”`
    )
  );
  ok(!!noDocTypeMessage, "No doctype warning message is visible");

  info("Clicking on the Learn More link");
  const noDocTypeMessageLearnMoreLink =
    noDocTypeMessage.querySelector(".learn-more-link");
  linkSimulation = await simulateLinkClick(noDocTypeMessageLearnMoreLink);

  is(
    linkSimulation.link,
    LEARN_MORE_URI,
    `Clicking the provided link opens expected URL`
  );
  is(linkSimulation.where, "tab", `Clicking the provided link opens in a tab`);

  info("Navigate to a page with standard doctype");
  await navigateTo(TEST_URI_STANDARD_DOCTYPE);
  info("Wait for a bit to make sure there is no doctype messages");
  await wait(1000);
  ok(
    !findWarningMessage(hud, `doctype`),
    "There is no doctype warning message"
  );

  info("Navigate to a about:blank");
  await navigateTo("about:blank");
  info("Wait for a bit to make sure there is no doctype messages");
  await wait(1000);
  ok(
    !findWarningMessage(hud, `doctype`),
    "There is no doctype warning message for about:blank"
  );

  info("Navigate to a view-source uri");
  await navigateTo(`view-source:${TEST_URI_NO_DOCTYPE}`);
  info("Wait for a bit to make sure there is no doctype messages");
  await wait(1000);
  ok(
    !findWarningMessage(hud, `doctype`),
    "There is no doctype warning message for view-source"
  );

  await closeConsole();
});
