/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the list of grids show the subgrids in the correct nested list and toggling
// the CSS grid highlighter for a subgrid.

const TEST_URI = URL_ROOT + "doc_subgrid.html";

add_task(async function () {
  await addTab(TEST_URI);
  const { gridInspector, inspector } = await openLayoutView();
  const { document: doc } = gridInspector;
  const { highlighters, store } = inspector;

  await selectNode(".container", inspector);
  const gridListEl = doc.getElementById("grid-list");
  const containerSubgridListEl = gridListEl.children[1];
  const mainSubgridListEl = containerSubgridListEl.querySelector("ul");

  info("Checking the initial state of the Grid Inspector.");
  is(
    getGridItemElements(gridListEl).length,
    1,
    "One grid container is listed."
  );
  is(
    getGridItemElements(containerSubgridListEl).length,
    2,
    "Got the correct number of subgrids in div.container"
  );
  is(
    getGridItemElements(mainSubgridListEl).length,
    2,
    "Got the correct number of subgrids in main.subgrid"
  );
  is(
    await getNumberOfVisibleGridHighlighters(),
    0,
    "No CSS grid highlighter is shown."
  );

  info("Toggling ON the CSS grid highlighter for header.");
  let onHighlighterShown = highlighters.once("grid-highlighter-shown");
  let onCheckboxChange = waitUntilState(
    store,
    state => state.grids[1].highlighted
  );
  let checkbox = containerSubgridListEl.children[0].querySelector("input");
  checkbox.click();
  await onHighlighterShown;
  await onCheckboxChange;

  info(
    "Checking the CSS grid highlighter and parent grid highlighter are created."
  );
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: false }),
    1,
    "CSS grid highlighter is shown."
  );
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: true }),
    1,
    "CSS grid highlighter for parent grid container is shown."
  );

  info("Toggling ON the CSS grid highlighter for main.");
  onHighlighterShown = highlighters.once("grid-highlighter-shown");
  onCheckboxChange = waitUntilState(
    store,
    state => state.grids[1].highlighted && state.grids[2].highlighted
  );
  checkbox = containerSubgridListEl.children[1].querySelector("input");
  checkbox.click();
  await onHighlighterShown;
  await onCheckboxChange;

  info("Checking the number of CSS grid highlighters present.");
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: false }),
    2,
    "Got the correct number of CSS grid highlighter shown."
  );
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: true }),
    1,
    "Only 1 parent grid highlighter should be shown for the same subgrid parent."
  );

  info("Toggling OFF the CSS grid highlighter for main.");
  let onHighlighterHidden = highlighters.once("grid-highlighter-hidden");
  onCheckboxChange = waitUntilState(
    store,
    state => state.grids[1].highlighted && !state.grids[2].highlighted
  );
  checkbox.click();
  await onHighlighterHidden;
  await onCheckboxChange;

  info("Checking the number of CSS grid highlighters present.");
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: false }),
    1,
    "Got the correct number of CSS grid highlighter shown."
  );
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: true }),
    1,
    "Got the correct number of CSS grid parent highlighter shown."
  );

  info("Toggling OFF the CSS grid highlighter for header.");
  onHighlighterHidden = highlighters.once("grid-highlighter-hidden");
  onCheckboxChange = waitUntilState(
    store,
    state => !state.grids[1].highlighted
  );
  checkbox = containerSubgridListEl.children[0].querySelector("input");
  checkbox.click();
  await onHighlighterHidden;
  await onCheckboxChange;

  info("Checking the CSS grid highlighter is not shown.");
  is(
    await getNumberOfVisibleGridHighlighters(),
    0,
    "No CSS grid highlighter is shown."
  );
});

/**
 * Returns the grid item elements <li> from the grid list element <ul>.
 *
 * @param  {Element} gridListEl
 *         The grid list element <ul>.
 * @return {Array<Element>} containing the grid item elements <li>.
 */
function getGridItemElements(gridListEl) {
  return [...gridListEl.children].filter(node => node.nodeName === "li");
}
