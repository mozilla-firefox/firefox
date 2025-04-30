/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Verify loading a tab document with aria-hidden specified on the root element
 * correctly renders the root element and its content. This test is meaninfully
 * different from testTabDocument which tests aria-hidden specified on the
 * body element.
 */
addAccessibleTask(
  `<html aria-hidden="true"><u>hello world`,
  async function testTabRootDocument(_, accDoc) {
    const tree = {
      DOCUMENT: [
        {
          TEXT_LEAF: [],
        },
      ],
    };
    testAccessibleTree(accDoc, tree);
  },
  {
    chrome: true,
    topLevel: true,
    iframe: false,
    remoteIframe: false,
  }
);

/**
 * Verify loading a tab doc with aria-hidden on the body renders the document.
 * Body elements inside of embedded iframes, should continue
 * to respect aria-hidden when present. This test ONLY tests
 * tab documents, it should not run in iframes. There is a separate
 * test for iframes in browser_test_aria_hidden_iframe.js.
 */
addAccessibleTask(
  `
  <p id="content">I am some content in a document</p>
  `,
  async function testTabDocument(browser, docAcc) {
    const originalTree = { DOCUMENT: [{ PARAGRAPH: [{ TEXT_LEAF: [] }] }] };
    testAccessibleTree(docAcc, originalTree);
  },
  {
    chrome: true,
    topLevel: true,
    iframe: false,
    remoteIframe: false,
    contentDocBodyAttrs: { "aria-hidden": "true" },
  }
);

/**
 * Verify adding aria-hidden to root doc elements has no effect.
 * Non-root doc elements, like embedded iframes, should continue
 * to respect aria-hidden when applied. This test ONLY tests
 * tab documents, it should not run in iframes. There is a separate
 * test for iframes in browser_test_aria_hidden_iframe.js.
 */
addAccessibleTask(
  `
  <p id="content">I am some content in a document</p>
  `,
  async function testTabDocumentMutation(browser, docAcc) {
    const originalTree = { DOCUMENT: [{ PARAGRAPH: [{ TEXT_LEAF: [] }] }] };

    testAccessibleTree(docAcc, originalTree);
    info("Adding aria-hidden=true to content doc");
    const unexpectedEvents = { unexpected: [[EVENT_REORDER, docAcc]] };
    await contentSpawnMutation(browser, unexpectedEvents, function () {
      const b = content.document.body;
      b.setAttribute("aria-hidden", "true");
    });

    testAccessibleTree(docAcc, originalTree);
  },
  { chrome: true, topLevel: true, iframe: false, remoteIframe: false }
);
