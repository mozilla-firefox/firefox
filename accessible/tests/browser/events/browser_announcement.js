/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Test internal announce method.
 */
addAccessibleTask(
  `<p id="p">abc</p>`,
  async function testAnnounce(browser, accDoc) {
    function announce(announcement, priority) {
      return invokeContentTask(
        browser,
        [announcement, priority],
        (cAnnouncement, cPriority) => {
          const accService = Cc[
            "@mozilla.org/accessibilityService;1"
          ].getService(Ci.nsIAccessibilityService);
          const cAcc = accService.getAccessibleFor(
            content.document.getElementById("p")
          );
          cAcc.announce(cAnnouncement, cPriority);
        }
      );
    }

    let acc = findAccessibleChildByID(accDoc, "p");
    let onAnnounce = waitForEvent(EVENT_ANNOUNCEMENT, acc);
    await announce("please", nsIAccessibleAnnouncementEvent.POLITE);
    let evt = await onAnnounce;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "please", "announcement matches.");
    is(evt.priority, nsIAccessibleAnnouncementEvent.POLITE, "priority matches");

    onAnnounce = waitForEvent(EVENT_ANNOUNCEMENT, acc);
    await announce("do it", nsIAccessibleAnnouncementEvent.ASSERTIVE);
    evt = await onAnnounce;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "do it", "announcement matches.");
    is(
      evt.priority,
      nsIAccessibleAnnouncementEvent.ASSERTIVE,
      "priority matches"
    );
  },
  { chrome: true, topLevel: true, iframe: true, remoteIframe: true }
);

/**
 * Test ariaNotify.
 */
addAccessibleTask(
  `
<p id="p">abc</p>
<div id="host">
  <template shadowrootmode="open">
    <p id="shadow">shadow</p>
    <slot></slot>
  </template>
  <p id="slotted">slotted</p>
</div>
  `,
  async function testAriaNotify(browser, docAcc) {
    const p = findAccessibleChildByID(docAcc, "p");
    info("p.ariaNotify a, priority unspecified");
    let announced = waitForEvent(EVENT_ANNOUNCEMENT, p);
    await invokeContentTask(browser, [], () => {
      content.p = content.document.getElementById("p");
      content.p.ariaNotify("a");
    });
    let evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "a", "announcement matches");
    is(evt.priority, nsIAccessibleAnnouncementEvent.POLITE, "priority correct");

    info("p.ariaNotify b, priority normal");
    announced = waitForEvent(EVENT_ANNOUNCEMENT, p);
    await invokeContentTask(browser, [], () => {
      content.p.ariaNotify("b", { priority: "normal" });
    });
    evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "b", "announcement matches");
    is(evt.priority, nsIAccessibleAnnouncementEvent.POLITE, "priority correct");

    info("p.ariaNotify c, priority high");
    announced = waitForEvent(EVENT_ANNOUNCEMENT, p);
    await invokeContentTask(browser, [], () => {
      content.p.ariaNotify("c", { priority: "high" });
    });
    evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "c", "announcement matches");
    is(
      evt.priority,
      nsIAccessibleAnnouncementEvent.ASSERTIVE,
      "priority correct"
    );

    info("doc.ariaNotify d, priority unspecified");
    announced = waitForEvent(EVENT_ANNOUNCEMENT, docAcc);
    await invokeContentTask(browser, [], () => {
      content.document.ariaNotify("d");
    });
    evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "d", "announcement matches");
    is(evt.priority, nsIAccessibleAnnouncementEvent.POLITE, "priority correct");

    const shadow = findAccessibleChildByID(docAcc, "shadow");
    info("shadow.ariaNotify e");
    announced = waitForEvent(EVENT_ANNOUNCEMENT, shadow);
    await invokeContentTask(browser, [], () => {
      content.document
        .getElementById("host")
        .shadowRoot.getElementById("shadow")
        .ariaNotify("e");
    });
    evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "e", "announcement matches");

    const slotted = findAccessibleChildByID(docAcc, "slotted");
    info("slotted.ariaNotify f");
    announced = waitForEvent(EVENT_ANNOUNCEMENT, slotted);
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("slotted").ariaNotify("f");
    });
    evt = await announced;
    evt.QueryInterface(nsIAccessibleAnnouncementEvent);
    is(evt.announcement, "f", "announcement matches");

    info("disconnected.ariaNotify g");
    await contentSpawnMutation(
      browser,
      { unexpected: [[EVENT_ANNOUNCEMENT, null]] },
      () => {
        content.document.createElement("p").ariaNotify("g");
      }
    );
  },
  { chrome: true, topLevel: true, iframe: true, remoteIframe: true }
);

/**
 * Test ariaNotify's permissions policy.
 */
addAccessibleTask(
  ``,
  async function testAriaNotifyPolicy(browser, docAcc) {
    info("doc.ariaNotify a");
    await contentSpawnMutation(
      browser,
      { unexpected: [[EVENT_ANNOUNCEMENT, docAcc]] },
      () => {
        content.document.ariaNotify("a");
      }
    );
  },
  {
    topLevel: false,
    remoteIframe: true,
    iframeAttrs: { allow: "aria-notify none" },
  }
);

/**
 * Test that announcements don't fire if the tab or window is in the background.
 */
addAccessibleTask(
  ``,
  async function testActive(browser, docAcc) {
    const events = [[EVENT_ANNOUNCEMENT, docAcc]];
    const expected = { expected: events };
    const unexpected = { unexpected: events };

    function ariaNotify(waitFor) {
      return contentSpawnMutation(browser, waitFor, async function () {
        content.document.ariaNotify("test");
      });
    }

    async function waitForDocVisibilityChange() {
      // It can take some time for the BrowsingContext's isActive state to
      // change, even after we've received the DOM and accessibility focus
      // events.
      // We use SpecialPowers.spawn directly here so the linter doesn't
      // complain about ContentTaskUtils being undefined.
      await SpecialPowers.spawn(docAcc.browsingContext, [], async function () {
        await ContentTaskUtils.waitForEvent(
          content.document,
          "visibilitychange"
        );
      });
    }

    info("ariaNotify when tab in foreground");
    await ariaNotify(expected);

    info("Opening new tab");
    let visibilityChanged = waitForDocVisibilityChange();
    await BrowserTestUtils.withNewTab(
      "https://example.com/",
      async function () {
        await visibilityChanged;
        info("ariaNotify when tab in background");
        await ariaNotify(unexpected);
        info("Closing new tab");
        visibilityChanged = waitForDocVisibilityChange();
      }
    );
    await visibilityChanged;
    info("ariaNotify with tab in foreground");
    await ariaNotify(expected);

    info("Minimizing window");
    visibilityChanged = waitForDocVisibilityChange();
    window.minimize();
    await visibilityChanged;
    info("ariaNotify with window in background");
    await ariaNotify(unexpected);
    info("Restoring window");
    visibilityChanged = waitForDocVisibilityChange();
    window.restore();
    await visibilityChanged;
    info("ariaNotify when tab in foreground");
    await ariaNotify(expected);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test that an ariaNotify call prior to accessibility tree construction doesn't
 * cause a failed assertion.
 */
addAccessibleTask(
  `<script> document.ariaNotify(""); </script>`,
  async function testBeforeDocLoad() {}
);
