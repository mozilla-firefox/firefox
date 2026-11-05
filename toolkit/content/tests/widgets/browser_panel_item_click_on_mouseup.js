/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BrowserTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/BrowserTestUtils.sys.mjs"
);

// panel-list needs a document that may load chrome modules, which an http
// document isn't; about:certificate is the closest content document that is.
const TEST_PAGE = "about:certificate";

add_task(async function test_clickOnMouseupInContentDocument() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      let doc = content.document;
      let script = doc.createElement("script");
      script.type = "module";
      script.src = "chrome://global/content/elements/panel-list.mjs";
      let loaded = new Promise(resolve => (script.onload = resolve));
      doc.head.appendChild(script);
      await loaded;

      let container = doc.createElement("div");
      container.id = "container";
      let anchor = doc.createElement("button");
      anchor.id = "anchor";
      anchor.textContent = "Open";
      let panelList = Cu.waiveXrays(doc.createElement("panel-list"));
      panelList.toggleAttribute("click-on-mouseup", true);
      for (let id of ["first", "second"]) {
        let item = doc.createElement("panel-item");
        item.id = id;
        item.textContent = id;
        panelList.append(item);
      }
      container.append(anchor, panelList);
      doc.body.replaceChildren(container);

      anchor.addEventListener("mousedown", event => panelList.toggle(event));
      // The log outlives this sandbox, so it lives in the document.
      doc.documentElement.dataset.clicks = "";
      doc.addEventListener("click", event => {
        doc.documentElement.dataset.clicks += event.target.id + ";";
      });
      panelList.addEventListener("shown", () => {
        doc.documentElement.dataset.shown = "true";
      });
    });

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#anchor",
      { type: "mousedown" },
      browser
    );
    await SpecialPowers.spawn(browser, [], async () => {
      let root = content.document.documentElement;
      await ContentTaskUtils.waitForMutationCondition(
        root,
        { attributeFilter: ["data-shown"] },
        () => root.dataset.shown
      );
    });

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#first",
      { type: "mouseup" },
      browser
    );

    let clicks = await SpecialPowers.spawn(browser, [], async () => {
      let doc = content.document;
      await ContentTaskUtils.waitForCondition(
        () => doc.documentElement.dataset.clicks,
        "The release produces a click"
      );
      return doc.documentElement.dataset.clicks;
    });
    is(
      clicks,
      "first;",
      "Releasing over an item clicks it, not its common ancestor with the anchor"
    );
  });
});
