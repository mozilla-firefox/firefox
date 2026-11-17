/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);

const cleanupInfobars = () => {
  InfoBar._universalInfobars = [];
  InfoBar._activeInfobar = null;
  if (InfoBar._observingWindowOpened) {
    InfoBar._observingWindowOpened = false;
    Services.obs.removeObserver(InfoBar, "domwindowopened");
  }
};

const removeByIdInWin = (win, id) => {
  const n = win.gNotificationBox.getNotificationWithValue(id);
  if (n) {
    win.gNotificationBox.removeNotification(n);
  }
};

const getNotificationFromWin = (win, id) =>
  win?.gNotificationBox?.getNotificationWithValue?.(id);

// Pause the first append until release() is called, optionally with an error.
const holdFirstAppend = (sandbox, box) => {
  let release;
  const held = new Promise(resolve => {
    release = resolve;
  });
  const realAppend = box.appendNotification.bind(box);
  let calls = 0;
  sandbox.stub(box, "appendNotification").callsFake(async (...args) => {
    if (++calls > 1) {
      return realAppend(...args);
    }
    const rejection = await held;
    if (rejection) {
      throw rejection;
    }
    return realAppend(...args);
  });
  return release;
};

add_setup(async function () {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(PrivateBrowsingUtils, "isWindowPrivate")
    .callsFake(win => !!win?.isPrivate);

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function universal_survives_originating_window_closure() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const incumbent = {
    id: "TEST_UNIVERSAL_HANDOVER_INCUMBENT",
    content: { type: "universal", text: "incumbent", buttons: [] },
  };
  const replacement = {
    id: "TEST_UNIVERSAL_HANDOVER_REPLACEMENT",
    content: {
      type: "universal",
      text: "replacement",
      buttons: [],
      canReplace: [incumbent.id],
    },
  };

  try {
    const incumbentNotification = await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      incumbent,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, incumbent.id),
      "Incumbent visible in the surviving window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      incumbentNotification,
      "The active infobar still names the notification that owns the bars"
    );

    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      replacement,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, replacement.id),
      "Replacement visible in the surviving window"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, incumbent.id),
      "The incumbent was evicted rather than left stacked under the replacement"
    );
  } finally {
    removeByIdInWin(win1, replacement.id);
    removeByIdInWin(win1, incumbent.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function stale_unload_does_not_resurrect_replaced_infobar() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const first = {
    id: "TEST_UNIVERSAL_STALE_FIRST",
    content: { type: "universal", text: "first", buttons: [] },
  };
  const second = {
    id: "TEST_UNIVERSAL_STALE_SECOND",
    content: {
      type: "universal",
      text: "second",
      buttons: [],
      canReplace: [first.id],
    },
  };
  const third = {
    id: "TEST_UNIVERSAL_STALE_THIRD",
    content: {
      type: "universal",
      text: "third",
      buttons: [],
      canReplace: [second.id],
    },
  };

  try {
    // Shown from win2, so win2's unload listener closes over `first`.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      first,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, first.id),
      "First message visible in both windows"
    );

    const secondNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      second,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, second.id),
      "Second message replaced the first"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.message?.id,
      second.id,
      "The stale unload left the replacement as the active infobar"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      secondNotification,
      "The active infobar still names the replacement's notification"
    );

    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      third,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, third.id),
      "Third message visible in the surviving window"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, second.id),
      "The second message was evicted rather than left stacked"
    );
  } finally {
    removeByIdInWin(win1, third.id);
    removeByIdInWin(win1, second.id);
    removeByIdInWin(win1, first.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function stale_unload_does_not_clear_active_global_infobar() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const original = {
    id: "TEST_GLOBAL_STALE_ORIGINAL",
    content: { type: "global", text: "original", buttons: [] },
  };
  const replacement = {
    id: "TEST_GLOBAL_STALE_REPLACEMENT",
    content: {
      type: "global",
      text: "replacement",
      buttons: [],
      canReplace: [original.id],
    },
  };
  const unrelated = {
    id: "TEST_GLOBAL_STALE_UNRELATED",
    content: { type: "global", text: "unrelated", buttons: [] },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      original,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, original.id),
      "Original visible in the window it was shown from"
    );

    const replacementNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      replacement,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, replacement.id),
      "Replacement visible in the first window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      replacementNotification,
      "Closing the original's window left the replacement active"
    );

    const shown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      unrelated,
      sandbox.stub()
    );
    Assert.equal(
      shown,
      null,
      "A message that replaces nothing is still refused"
    );
    Assert.ok(
      !getNotificationFromWin(win1, unrelated.id),
      "It did not stack under the replacement"
    );
  } finally {
    removeByIdInWin(win1, unrelated.id);
    removeByIdInWin(win1, replacement.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function reshown_universal_stays_tracked() {
  const sandbox = sinon.createSandbox();
  // Keep removal pending until the replacement is appended.
  await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 0]] });

  const win1 = BrowserWindowTracker.getTopWindow();
  const win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_RESHOWN",
    content: {
      type: "universal",
      text: "reshown",
      buttons: [],
      canReplace: ["TEST_UNIVERSAL_RESHOWN"],
    },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "First showing visible in both windows"
    );

    const reshown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      reshown,
      "The re-shown notification owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      2,
      "Both of the re-shown bars are tracked"
    );

    reshown.removeUniversalInfobars();
    await TestUtils.waitForCondition(
      () =>
        !getNotificationFromWin(win1, message.id) &&
        !getNotificationFromWin(win2, message.id),
      "The re-shown bars can still be removed"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    removeByIdInWin(win2, message.id);
    await BrowserTestUtils.closeWindow(win2);
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function universal_dismissed_from_later_window() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_DISMISS_LATER_WINDOW",
    content: { type: "universal", text: "later window", buttons: [] },
  };

  let win2;
  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, message.id),
      "Visible in the window it was shown from"
    );

    win2 = await BrowserTestUtils.openNewBrowserWindow();
    // Mirror observe() for a newly opened window.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      sandbox.stub(),
      true
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Visible in the window that got its bar later"
    );

    getNotificationFromWin(win2, message.id).dismiss();

    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win2, message.id),
      "Dismissed in the window it was dismissed from"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, message.id),
      "Dismissed in the other window too"
    );
    Assert.ok(
      !InfoBar._activeInfobar,
      "The active infobar was cleared by the dismissal"
    );
    Assert.ok(
      !InfoBar._observingWindowOpened,
      "The new window observer was unregistered"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      removeByIdInWin(win2, message.id);
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function universal_reaches_a_newly_opened_window() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_NEW_WINDOW",
    content: { type: "universal", text: "new window", buttons: [] },
  };

  let win2;
  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );

    win2 = await BrowserTestUtils.openNewBrowserWindow();
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Visible in the newly opened window"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      removeByIdInWin(win2, message.id);
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function failed_show_does_not_hold_the_active_slot() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const doomed = {
    id: "TEST_UNIVERSAL_DOOMED",
    content: {
      type: "universal",
      text: "doomed",
      buttons: [],
      priority: 99,
    },
  };
  const followUp = {
    id: "TEST_UNIVERSAL_FOLLOW_UP",
    content: { type: "universal", text: "follow up", buttons: [] },
  };

  try {
    await Assert.rejects(
      InfoBar.showInfoBarMessage(
        win1.gBrowser.selectedBrowser,
        doomed,
        sandbox.stub()
      ),
      /Invalid notification priority/,
      "The show reports its failure"
    );

    Assert.ok(!InfoBar._activeInfobar, "The active infobar slot was released");
    Assert.equal(
      InfoBar._universalInfobars.length,
      0,
      "No universal infobars are left tracked"
    );

    const shown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      followUp,
      sandbox.stub()
    );
    Assert.ok(shown, "A later message is not refused by the failed one");
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, followUp.id),
      "The later message is visible"
    );
  } finally {
    removeByIdInWin(win1, followUp.id);
    removeByIdInWin(win1, doomed.id);
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function superseded_failed_show_spares_its_successor() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const doomed = {
    id: "TEST_UNIVERSAL_SUPERSEDED_DOOMED",
    content: { type: "universal", text: "doomed", buttons: [] },
  };
  const successor = {
    id: "TEST_UNIVERSAL_SUPERSEDED_SUCCESSOR",
    content: {
      type: "universal",
      text: "successor",
      buttons: [],
      canReplace: [doomed.id],
    },
  };

  try {
    const release = holdFirstAppend(sandbox, win1.gNotificationBox);

    const pending = InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      doomed,
      sandbox.stub()
    );

    const successorNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      successor,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, successor.id),
      "The successor took the slot while the first was still appending"
    );

    release(new Error("held append failed"));
    await Assert.rejects(
      pending,
      /held append failed/,
      "The superseded show reports its failure"
    );

    Assert.ok(
      getNotificationFromWin(win1, successor.id),
      "The successor's bar survived the failure"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      successorNotification,
      "The successor still owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "The successor's bar is still tracked"
    );
    Assert.ok(
      InfoBar._observingWindowOpened,
      "The successor's new window observer is still registered"
    );
  } finally {
    sandbox.restore();
    removeByIdInWin(win1, successor.id);
    removeByIdInWin(win1, doomed.id);
    cleanupInfobars();
  }
});

add_task(async function superseded_show_does_not_leave_its_bar_behind() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const first = {
    id: "TEST_UNIVERSAL_SUPERSEDED_FIRST",
    content: { type: "universal", text: "first", buttons: [] },
  };
  const second = {
    id: "TEST_UNIVERSAL_SUPERSEDED_SECOND",
    content: {
      type: "universal",
      text: "second",
      buttons: [],
      canReplace: [first.id],
    },
  };

  try {
    const release = holdFirstAppend(sandbox, win1.gNotificationBox);

    const pending = InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      first,
      sandbox.stub()
    );

    const secondNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      second,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, second.id),
      "The replacement took the slot while the first was still appending"
    );

    release();
    await pending;

    Assert.ok(
      !getNotificationFromWin(win1, first.id),
      "The superseded show did not leave its bar behind"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      secondNotification,
      "The replacement still owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "Only the replacement's bar is tracked"
    );
  } finally {
    sandbox.restore();
    removeByIdInWin(win1, first.id);
    removeByIdInWin(win1, second.id);
    cleanupInfobars();
  }
});

add_task(async function stale_entries_are_pruned_on_unload() {
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_PRUNE_ON_UNLOAD",
    content: { type: "universal", text: "prune", buttons: [] },
  };

  try {
    // Shown from win2, so win2 owns the unload listener.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      () => {}
    );
    await TestUtils.waitForCondition(
      () => InfoBar._universalInfobars.length === 2,
      "One entry per window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "The closing window's entry was pruned"
    );
    Assert.ok(
      InfoBar._activeInfobar,
      "The notification stays active while a bar survives"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    cleanupInfobars();
  }
});

add_task(async function closing_the_last_universal_window_clears_active() {
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_LAST_WINDOW",
    content: { type: "universal", text: "last window", buttons: [] },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      () => {}
    );
    await TestUtils.waitForCondition(
      () => InfoBar._universalInfobars.length === 2,
      "One entry per window"
    );

    // Leave win2 as the only window still showing a bar, so its unload has to
    // recognize that nothing survives it.
    removeByIdInWin(win1, message.id);
    InfoBar._universalInfobars = InfoBar._universalInfobars.filter(
      entry => entry.win !== win1
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar,
      null,
      "The active infobar was released once no bar survived"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    cleanupInfobars();
  }
});

add_task(async function universal_reaches_a_window_still_loading() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_LOADING_WINDOW",
    content: { type: "universal", text: "loading window", buttons: [] },
  };

  // Open this window before showing the message so the new-window observer
  // cannot handle it.
  let win2 = win1.OpenBrowserWindow();
  const started = TestUtils.topicObserved(
    "browser-delayed-startup-finished",
    subject => subject === win2
  );
  try {
    await BrowserTestUtils.waitForEvent(win2, "DOMContentLoaded");
    Assert.equal(
      win2.document.readyState,
      "interactive",
      "The new window is still loading"
    );

    // Exclude the harness window so enumeration reaches win2 before yielding.
    // This models startup, when the loading window is the only eligible one.
    win1.document.documentElement.setAttribute("taskbartab", "true");
    try {
      await InfoBar.showInfoBarMessage(
        win2.gBrowser.selectedBrowser,
        message,
        sandbox.stub()
      );
    } finally {
      win1.document.documentElement.removeAttribute("taskbartab");
    }

    await started;
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Shown in the window once it loaded"
    );
  } finally {
    await started;
    removeByIdInWin(win1, message.id);
    removeByIdInWin(win2, message.id);
    await BrowserTestUtils.closeWindow(win2);
    sandbox.restore();
    cleanupInfobars();
  }
});
