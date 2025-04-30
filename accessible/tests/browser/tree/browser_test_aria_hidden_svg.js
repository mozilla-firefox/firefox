/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

const SVG_DOCUMENT_ID = "rootSVG";
const HIDDEN_SVG_URI =
  "data:image/svg+xml,%3Csvg%20id%3D%22rootSVG%22%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20aria-hidden%3D%22true%22%3E%3Ctext%20x%3D%2210%22%20y%3D%2250%22%20font-size%3D%2230%22%20id%3D%22textSVG%22%3EMy%20SVG%3C%2Ftext%3E%3C%2Fsvg%3E";
const SVG_URI =
  "data:image/svg+xml,%3Csvg%20id%3D%22rootSVG%22%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%3E%3Ctext%20x%3D%2210%22%20y%3D%2250%22%20font-size%3D%2230%22%20id%3D%22textSVG%22%3EMy%20SVG%3C%2Ftext%3E%3C%2Fsvg%3E";

/**
 * Verify loading an SVG document with aria-hidden=true renders the
 * entire document subtree.
 * Non-root svg elements, like those in embedded iframes, should
 * continue to respect aria-hidden when applied.
 */
addAccessibleTask(
  `hello world`,
  async function testSVGDocument(browser) {
    let loaded = waitForEvent(EVENT_DOCUMENT_LOAD_COMPLETE, SVG_DOCUMENT_ID);
    info("Loading SVG");
    browser.loadURI(Services.io.newURI(HIDDEN_SVG_URI), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
    await loaded;

    const tree = {
      DOCUMENT: [
        {
          TEXT_CONTAINER: [
            {
              TEXT_LEAF: [],
            },
          ],
        },
      ],
    };
    const root = getRootAccessible(document);
    const svgRoot = findAccessibleChildByID(root, SVG_DOCUMENT_ID);
    testAccessibleTree(svgRoot, tree);
  },
  { chrome: true, topLevel: true, iframe: false, remoteIframe: false }
);

/**
 * Verify loading an SVG document with aria-hidden=true
 * in an iframe does not render the document subtree.
 */
addAccessibleTask(
  `hello world`,
  async function testSVGIframeDocument(browser) {
    info("Loading SVG");
    const loaded = waitForEvent(EVENT_DOCUMENT_LOAD_COMPLETE, SVG_DOCUMENT_ID);
    await SpecialPowers.spawn(
      browser,
      [DEFAULT_IFRAME_ID, HIDDEN_SVG_URI],
      (_id, _uri) => {
        content.document.getElementById(_id).src = _uri;
      }
    );
    await loaded;

    const tree = {
      DOCUMENT: [],
    };

    const root = getRootAccessible(document);
    const svgRoot = findAccessibleChildByID(root, SVG_DOCUMENT_ID);
    testAccessibleTree(svgRoot, tree);
  },
  { chrome: false, topLevel: false, iframe: true, remoteIframe: true }
);

/**
 * Verify adding aria-hidden to root svg elements has no effect.
 * Non-root svg elements, like those in embedded iframes, should
 * continue to respect aria-hidden when applied.
 */
addAccessibleTask(
  `hello world`,
  async function testSVGDocumentMutation(browser) {
    let loaded = waitForEvent(EVENT_DOCUMENT_LOAD_COMPLETE, SVG_DOCUMENT_ID);
    info("Loading SVG");
    browser.loadURI(Services.io.newURI(SVG_URI), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
    await loaded;

    const originalTree = {
      DOCUMENT: [
        {
          TEXT_CONTAINER: [
            {
              TEXT_LEAF: [],
            },
          ],
        },
      ],
    };
    const root = getRootAccessible(document);
    const svgRoot = findAccessibleChildByID(root, SVG_DOCUMENT_ID);
    testAccessibleTree(svgRoot, originalTree);
    info("Adding aria-hidden=true to svg");
    // XXX Bug 1959547: We incorrectly get a reorder
    // here. The tree should be unaffected by this attribute,
    // but it seems like it isn't! Below we'll verify that
    // the tree isn't removed, despite this reorder.
    const unexpectedEvents = { expected: [[EVENT_REORDER, SVG_DOCUMENT_ID]] };
    info("Adding aria-hidden");
    await contentSpawnMutation(
      browser,
      unexpectedEvents,
      function (_id) {
        const d = content.document.getElementById(_id);
        d.setAttribute("aria-hidden", "true");
      },
      [SVG_DOCUMENT_ID]
    );
    // XXX Bug 1959547: We end up with an extra node in the
    // tree after adding aria-hidden. It seems like SVG root
    // element is splitting off / no longer behaves as the
    // document...?
    const newTree = {
      DOCUMENT: [
        {
          DIAGRAM: [
            {
              TEXT_CONTAINER: [
                {
                  TEXT_LEAF: [],
                },
              ],
            },
          ],
        },
      ],
    };
    testAccessibleTree(svgRoot, newTree);
  },
  { chrome: true, topLevel: true, iframe: false, remoteIframe: false }
);

/**
 * Verify adding aria-hidden to root svg elements in iframes removes
 * the svg subtree.
 */
addAccessibleTask(
  `hello world`,
  async function testSVGIframeDocumentMutation(browser) {
    info("Loading SVG");
    const loaded = waitForEvent(EVENT_DOCUMENT_LOAD_COMPLETE, SVG_DOCUMENT_ID);
    await SpecialPowers.spawn(
      browser,
      [DEFAULT_IFRAME_ID, SVG_URI],
      (contentId, _uri) => {
        content.document.getElementById(contentId).src = _uri;
      }
    );
    await loaded;
    const originalTree = {
      DOCUMENT: [
        {
          TEXT_CONTAINER: [
            {
              TEXT_LEAF: [],
            },
          ],
        },
      ],
    };
    const svgRoot = findAccessibleChildByID(
      getRootAccessible(document),
      SVG_DOCUMENT_ID
    );
    testAccessibleTree(svgRoot, originalTree);

    info("Adding aria-hidden=true to svg");
    const events = { expected: [[EVENT_REORDER, SVG_DOCUMENT_ID]] };
    await contentSpawnMutation(
      browser,
      events,
      function (_id) {
        const d = content.document.getElementById(_id);
        d.setAttribute("aria-hidden", "true");
      },
      [SVG_DOCUMENT_ID]
    );

    const newTree = { DOCUMENT: [] };
    testAccessibleTree(svgRoot, newTree);
  },
  { chrome: false, topLevel: false, iframe: true, remoteIframe: true }
);
