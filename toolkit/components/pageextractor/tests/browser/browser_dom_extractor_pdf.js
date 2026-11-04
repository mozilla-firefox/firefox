/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_dom_extractor_pdf() {
  const { cleanup, getPageExtractor } = await openSupportFile("page.pdf");

  is(
    (await getPageExtractor().getText()).text,
    [
      "Etymology of Mochitests",
      'It\'s interesting that inside of Mozilla most people call mochitests "mohkee tests". I believe this is because it is',
      'adjacent to the term"mocha tests", which is pronounced with the hard k sound. However, thetesting',
      'infrastructure is named after the delicious Japanese treat knownas mochi. Mochi, pronounced like "moh',
      'chee" is a food that is made frompounding steamed rice into a soft elastic mass.',
    ].join("\n"),
    "Text is able to be extracted from the pdf."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_pdf_tab_closed() {
  const { cleanup, getPageExtractor } = await openSupportFile("page.pdf");

  // Block the viewer's handler so extraction stays pending until tab closure.
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.addEventListener(
      "requestTextContent",
      event => event.stopImmediatePropagation(),
      { capture: true }
    );
  });

  const textPromise = getPageExtractor().getText();
  await cleanup();

  await Assert.rejects(
    textPromise,
    error => error.name === "AbortError",
    "Closing the tab rejects the pending text extraction."
  );
});
