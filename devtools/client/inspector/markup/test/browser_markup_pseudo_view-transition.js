/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL = `data:text/html,<!DOCTYPE html><meta charset=utf8>${encodeURIComponent(`
  <style>
    ::view-transition-group(root) {
       /* large number so the view-transition pseudo elements are available during the whole test */
       animation-duration: 3600s;
    }
    header {
      view-transition-name: main-header;
    }

    header h1 {
      view-transition-name: main-header-text;
    }
  </style>
  <header>
    <h1>::view-transition</h1>
  </header>
`)}`;

add_task(async function samePageViewTransition() {
  const { inspector } = await openInspectorForURL(TEST_URL);

  await selectNode("html", inspector);

  const onMarkupMutation = inspector.once("markupmutation");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    const document = content.document;
    content.testTransition = document.startViewTransition(() => {
      document.querySelector("h1").replaceChildren("updated");
    });
    await content.testTransition.ready;
    await content.testTransition.updateCallbackDone;
  });
  await onMarkupMutation;

  const htmlNodeFront = await getNodeFront("html", inspector);
  const htmlContainer = await getContainerForNodeFront(
    htmlNodeFront,
    inspector
  );

  const viewTransitionMarkupNodeEl = htmlContainer.children.childNodes[2];
  is(
    viewTransitionMarkupNodeEl.textContent,
    "::view-transition",
    "::view-transition node is displayed"
  );

  const viewTransitionContainer = viewTransitionMarkupNodeEl.container;
  is(
    viewTransitionContainer.type,
    "readonlycontainer",
    "The ::view-transition container is read-only"
  );

  info("Expand the whole ::view-transition subtree");
  await toggleContainerByClick(inspector, viewTransitionContainer, {
    altKey: true,
  });

  let tree = `
    html
      head!ignore-children
      body!ignore-children
      ::view-transition
        ::view-transition-group(root)
          ::view-transition-image-pair(root)
            ::view-transition-old(root)
            ::view-transition-new(root)
        ::view-transition-group(main-header)
          ::view-transition-image-pair(main-header)
            ::view-transition-old(main-header)
            ::view-transition-new(main-header)
        ::view-transition-group(main-header-text)
          ::view-transition-image-pair(main-header-text)
            ::view-transition-old(main-header-text)
            ::view-transition-new(main-header-text)
      `.trim();
  await assertMarkupViewAsTree(tree, "html", inspector);

  info(
    "Check that html element is selected back when view transition is over and pseudo elements removed"
  );

  const htmlChildren = await inspector.markup.walker.children(htmlNodeFront);
  const viewTransitionNodeFront = htmlChildren.nodes[2];
  await selectNode(viewTransitionNodeFront, inspector);
  is(
    inspector.selection.nodeFront.displayName,
    "::view-transition",
    "::view-transition element is properly selected"
  );

  info("Stop the view transition");
  const onSelection = inspector.selection.once("new-node-front");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    content.testTransition.skipTransition();
    delete content.testTransition;
  });
  await onSelection;

  is(
    inspector.selection.nodeFront.displayName,
    "html",
    "html element was selected after the view transition was skipped"
  );

  // Check that the ::view-transition elements are removed
  tree = `
    html
      head!ignore-children
      body!ignore-children
      `.trim();
  await assertMarkupViewAsTree(tree, "html", inspector);
});

const TEST_URL_START =
  URL_ROOT + "doc_markup_view_transition_cross_document_start.html";
const TEST_URL_END =
  URL_ROOT + "doc_markup_view_transition_cross_document_end.html";
add_task(async function crossDocumentViewTransition() {
  const { inspector } = await openInspectorForURL(TEST_URL_START);

  info("Navigating to a page to trigger the view transition");
  await navigateTo(TEST_URL_END, { waitForLoad: false });

  const htmlNodeFront = await getNodeFront("html", inspector);
  const htmlContainer = await getContainerForNodeFront(
    htmlNodeFront,
    inspector
  );

  await waitFor(() => htmlContainer.children.childNodes.length === 3);

  const viewTransitionMarkupNodeEl = htmlContainer.children.childNodes[2];
  is(
    viewTransitionMarkupNodeEl.textContent,
    "::view-transition",
    "::view-transition node is displayed"
  );

  const viewTransitionContainer = viewTransitionMarkupNodeEl.container;
  is(
    viewTransitionContainer.type,
    "readonlycontainer",
    "The ::view-transition container is read-only"
  );

  info("Expand the whole ::view-transition subtree");
  await toggleContainerByClick(inspector, viewTransitionContainer, {
    altKey: true,
  });

  let tree = `
    html
      head!ignore-children
      body!ignore-children
      ::view-transition
        ::view-transition-group(root)
          ::view-transition-image-pair(root)
            ::view-transition-old(root)
            ::view-transition-new(root)
        ::view-transition-group(my-vt)
          ::view-transition-image-pair(my-vt)
            ::view-transition-old(my-vt)
            ::view-transition-new(my-vt)
      `.trim();
  await assertMarkupViewAsTree(tree, "html", inspector);

  info(
    "Check that html element is selected back when view transition is over and pseudo elements removed"
  );

  const htmlChildren = await inspector.markup.walker.children(htmlNodeFront);
  const viewTransitionNodeFront = htmlChildren.nodes[2];
  await selectNode(viewTransitionNodeFront, inspector);
  is(
    inspector.selection.nodeFront.displayName,
    "::view-transition",
    "::view-transition element is properly selected"
  );

  info("Stop the view transition");
  const onSelection = inspector.selection.once("new-node-front");
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], () =>
    content.document.activeViewTransition.skipTransition()
  );
  await onSelection;

  is(
    inspector.selection.nodeFront.displayName,
    "html",
    "html element was selected after the view transition was skipped"
  );

  // Check that the ::view-transition elements are removed
  tree = `
    html
      head!ignore-children
      body!ignore-children
      `.trim();
  await assertMarkupViewAsTree(tree, "html", inspector);
});
