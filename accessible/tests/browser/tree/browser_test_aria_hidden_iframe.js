/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Verify loading an iframe document with aria-hidden specified on the root element
 * correctly hides the root element and its content. This test is meaninfully
 * different from testIframeDocument which tests aria-hidden specified on the
 * body element.
 */
addAccessibleTask(
  `hello world`,
  async function testIframeRootDocument(browser) {
    info("Loading iframe document");
    const HIDDEN_IFRAME_URI =
      "data:text/html,<html id='new_html' aria-hidden='true'><body id='iframeBody'><u>hello world</u></body></html>";
    const loaded = waitForEvent(EVENT_DOCUMENT_LOAD_COMPLETE, "iframeBody");
    await SpecialPowers.spawn(
      browser,
      [DEFAULT_IFRAME_ID, HIDDEN_IFRAME_URI],
      (_id, _uri) => {
        content.document.getElementById(_id).src = _uri;
      }
    );
    await loaded;

    const tree = {
      INTERNAL_FRAME: [
        {
          DOCUMENT: [],
        },
      ],
    };
    const root = getRootAccessible(document);
    const iframeDoc = findAccessibleChildByID(root, DEFAULT_IFRAME_ID);
    testAccessibleTree(iframeDoc, tree);
  },
  {
    chrome: false,
    topLevel: false,
    iframe: true,
    remoteIframe: true,
  }
);

/**
 * Verify loading an iframe doc with aria-hidden doesn't render the document.
 * This test ONLY tests iframe documents, it should not run in tab docs.
 * There is a separate test for tab docs in browser_test_aria_hidden.js.
 */
addAccessibleTask(
  `
  <p id="content">I am some content in a document</p>
  `,
  async function testIframeDocument(browser, docAcc, topLevel) {
    const originalTree = { DOCUMENT: [{ INTERNAL_FRAME: [{ DOCUMENT: [] }] }] };
    testAccessibleTree(topLevel, originalTree);
  },
  {
    chrome: false,
    topLevel: false,
    iframe: true,
    remoteIframe: true,
    iframeDocBodyAttrs: { "aria-hidden": "true" },
  }
);

/**
 * Verify adding aria-hidden to iframe doc elements removes
 * their subtree. This test ONLY tests iframe documents, it
 * should not run in tab documents. There is a separate test for
 * tab documents in browser_test_aria_hidden.js.
 */
addAccessibleTask(
  `
  <p id="content">I am some content in a document</p>
  `,
  async function testIframeDocumentMutation(browser, docAcc, topLevel) {
    const originalTree = {
      DOCUMENT: [
        {
          INTERNAL_FRAME: [
            {
              DOCUMENT: [
                {
                  PARAGRAPH: [
                    {
                      TEXT_LEAF: [],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    };

    testAccessibleTree(topLevel, originalTree);
    info("Adding aria-hidden=true to content doc");
    await contentSpawnMutation(
      browser,
      { expected: [[EVENT_REORDER, docAcc]] },
      function () {
        const b = content.document.body;
        b.setAttribute("aria-hidden", "true");
      }
    );
    const newTree = {
      DOCUMENT: [
        {
          INTERNAL_FRAME: [
            {
              DOCUMENT: [],
            },
          ],
        },
      ],
    };
    testAccessibleTree(topLevel, newTree);
  },
  { chrome: false, topLevel: false, iframe: true, remoteIframe: true }
);
