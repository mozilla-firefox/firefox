/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the state of grid highlighters after toggling the checkbox of subgrids.

const TEST_URI = URL_ROOT + "doc_subgrid.html";

add_task(async () => {
  await addTab(TEST_URI);
  const { gridInspector, inspector } = await openLayoutView();
  const { document: doc } = gridInspector;
  const { highlighters, store } = inspector;

  await selectNode(".container", inspector);
  const parentEl = doc.getElementById("grid-list");
  // Input for .container
  const parentInput = parentEl.children[0].querySelector("input");
  const subgridEl = parentEl.children[1];
  // Input for <main>
  const subgridInput = subgridEl.children[1].querySelector("input");
  const grandSubgridEl = subgridEl.children[2];
  // Input for .aside1
  const grandSubgridInput = grandSubgridEl.children[0].querySelector("input");

  info(
    "Toggling ON the CSS grid highlighters for .container, <main> and .aside1"
  );
  await toggleHighlighter(parentInput, highlighters);
  await toggleHighlighter(subgridInput, highlighters);
  await toggleHighlighter(grandSubgridInput, highlighters);
  await waitUntilState(
    store,
    state => state.grids.filter(g => g.highlighted).length === 3
  );

  info("Check the state of highlighters");
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: false }),
    3,
    "All highlighters are use as normal highlighter"
  );

  info("Toggling OFF the CSS grid highlighter for <main>");
  await toggleHighlighter(subgridInput, highlighters);
  await waitUntilState(
    store,
    state => state.grids.filter(g => g.highlighted).length === 2
  );

  info("Check the state of highlighters after hiding subgrid for <main>");

  is(
    await getNumberOfVisibleGridHighlighters({ isParent: false }),
    2,
    "2 highlighters are use as normal highlighter"
  );
  is(
    await getNumberOfVisibleGridHighlighters({ isParent: true }),
    1,
    "The highlighter for <main> is used as parent highlighter"
  );
});

async function toggleHighlighter(input, highlighters) {
  const eventName = input.checked
    ? "grid-highlighter-hidden"
    : "grid-highlighter-shown";
  const onHighlighterEvent = highlighters.once(eventName);
  input.click();
  await onHighlighterEvent;
}
