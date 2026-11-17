/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Ensure non-toplevel security errors are displayed

"use strict";

const TEST_URI =
  "data:text/html;charset=utf-8,<!DOCTYPE html>Web Console subresource STS warning test";
const TEST_DOC =
  "https://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-subresource-security-error.html";
const SAMPLE_MSG = "specified a header that could not be parsed successfully.";

// STS headers are only processed on network responses, so if the test runs
// again in the same browser, a cached script wouldn't log the warning.
registerCleanupFunction(() => Services.cache2.clear());

add_task(async function () {
  const hud = await openNewTabAndConsole(TEST_URI);
  await clearOutput(hud);
  await navigateTo(TEST_DOC);

  await waitFor(() => findWarningMessage(hud, SAMPLE_MSG));

  ok(true, "non-toplevel security warning message was displayed");
});
