"use strict";

/* global ContentTaskUtils */

const { FRAMED_WIDGETS } = ChromeUtils.importESModule(
  "resource://newtab/common/WidgetsRegistry.mjs"
);

// Widgets drawn by a registry `card` (e.g. FramedWidgetCard) render a moz-widget
// instead of their own iframe, and have their own tests.
const WIDGETS = FRAMED_WIDGETS.filter(w => !w.card);
const PREF_BRANCH = "browser.newtabpage.activity-stream.";
const PAGE_URL =
  "https://example.com/browser/browser/extensions/newtab/test/browser/blue_page.html";

let sandbox;
let restoreFeed;

// Undoes the loadFrames stub and puts back the feed's real manifest, so the
// stubbed entries don't leak into the next task or later tests.
function restoreFeedState() {
  sandbox?.restore();
  sandbox = null;
  restoreFeed?.();
  restoreFeed = null;
}
registerCleanupFunction(restoreFeedState);

// Serves a manifest entry for every widget from the framed widgets feed instead
// of fetching from Merino, and turns every widget on. `disabledId` gets
// `enabled: false`.
async function setUpFramedWidgets(pushPrefs, disabledId, widgets = WIDGETS) {
  const feed = AboutNewTab.activityStream.store.feeds.get(
    "feeds.framedwidgetsfeed"
  );
  restoreFeedState();
  const { manifest, loaded } = feed;
  restoreFeed = () => {
    feed.manifest = manifest;
    feed.loaded = loaded;
    feed.update();
  };
  sandbox = sinon.createSandbox();
  sandbox.stub(feed, "loadFrames").callsFake(async () => {
    feed.manifest = {
      widgets: widgets.map(({ id }) => ({
        id,
        enabled: id !== disabledId,
        url: `${PAGE_URL}?widget=${id}`,
      })),
    };
    feed.update();
    feed.loaded = true;
  });
  await pushPrefs(
    [`${PREF_BRANCH}widgets.framed.allowedHostSuffixes`, "example.com"],
    [
      `${PREF_BRANCH}discoverystream.config`,
      JSON.stringify({ collapsible: true, enabled: true }),
    ],
    [`${PREF_BRANCH}widgets.system.enabled`, true],
    [`${PREF_BRANCH}widgets.enabled`, true],
    [`${PREF_BRANCH}widgets.order`, widgets.map(w => w.id).join(",")],
    ...widgets.flatMap(w => [
      [`${PREF_BRANCH}${w.systemEnabledPref}`, true],
      [`${PREF_BRANCH}${w.enabledPref}`, true],
    ])
  );
  await feed.loadFrames();
}

add_setup(() => {
  Assert.greater(WIDGETS.length, 1, "The registry has framed widgets to test");
});

test_newtab({
  async before({ pushPrefs }) {
    await setUpFramedWidgets(pushPrefs);
    return WIDGETS.map(w => w.id);
  },

  test: async function test_framed_widgets_load_manifest_urls(ids) {
    for (const id of ids) {
      const frame = await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(`iframe.${id}-frame`),
        `Wait for the ${id} frame`
      );
      Assert.ok(
        // Travel adds the home region in the fragment.
        frame
          .getAttribute("src")
          .split("#")[0]
          .endsWith(`/blue_page.html?widget=${id}`),
        `The ${id} frame loads its manifest URL`
      );
    }
  },

  after() {
    restoreFeedState();
  },
});

test_newtab({
  async before({ pushPrefs }) {
    const disabledId = WIDGETS.at(-1).id;
    await setUpFramedWidgets(pushPrefs, disabledId);
    return {
      shownIds: WIDGETS.map(w => w.id).filter(id => id !== disabledId),
      disabledId,
    };
  },

  test: async function test_framed_widget_hidden_when_disabled({
    shownIds,
    disabledId,
  }) {
    for (const id of shownIds) {
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(`iframe.${id}-frame`),
        `Wait for the ${id} frame`
      );
    }
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(`article.${disabledId}`),
      `${disabledId} is not rendered when the manifest disables it`
    );
  },

  after() {
    restoreFeedState();
  },
});

// A preloaded newtab renders while hidden, possibly long before the user opens
// it. A framed widget's document reads its state when it loads, so loading it
// then would show the state from preload time (bug 2068016). The frames wait
// until the page is shown.
add_task(async function test_framed_widgets_wait_for_preloaded_tab_to_show() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtab.preload", true]],
  });
  await setUpFramedWidgets(
    (...prefs) => SpecialPowers.pushPrefEnv({ set: prefs }),
    null,
    FRAMED_WIDGETS
  );

  NewTabPagePreloading.removePreloadedBrowser(window);
  NewTabPagePreloading.maybeCreatePreloadedBrowser(window);
  const preloaded = gBrowser.preloadedBrowser;
  Assert.ok(preloaded, "A newtab is preloaded");
  await waitForPreloaded(preloaded);

  const ids = FRAMED_WIDGETS.map(w => w.id);
  // The src of each widget's frame, whether the widget draws its own iframe or
  // a moz-widget does.
  const getFrameSrcs = browser =>
    SpecialPowers.spawn(browser, [ids], async widgetIds => {
      const srcs = { visibilityState: content.document.visibilityState };
      for (const id of widgetIds) {
        const container = await ContentTaskUtils.waitForCondition(
          () =>
            content.document.querySelector(`article.${id}`) ??
            content.document.querySelector(`moz-widget[widget-id="${id}"]`),
          `Wait for the ${id} widget`
        );
        const frame =
          container.localName === "moz-widget"
            ? container
            : container.querySelector("iframe");
        srcs[id] = frame?.getAttribute("src") ?? null;
      }
      return srcs;
    });

  const hiddenSrcs = await getFrameSrcs(preloaded);
  Assert.equal(
    hiddenSrcs.visibilityState,
    "hidden",
    "The preloaded newtab is hidden"
  );
  for (const id of ids) {
    Assert.ok(
      !hiddenSrcs[id],
      `The ${id} frame loads nothing while the newtab is preloaded`
    );
  }

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );
  Assert.equal(tab.linkedBrowser, preloaded, "The preloaded newtab is used");
  await TestUtils.waitForCondition(async () => {
    const srcs = await getFrameSrcs(tab.linkedBrowser);
    return ids.every(id => srcs[id]);
  }, "Wait for every frame to load once the newtab is shown");
  const shownSrcs = await getFrameSrcs(tab.linkedBrowser);
  for (const id of ids) {
    Assert.ok(
      shownSrcs[id].split("#")[0].endsWith(`/blue_page.html?widget=${id}`),
      `The ${id} frame loads its manifest URL once the newtab is shown`
    );
  }

  BrowserTestUtils.removeTab(tab);
  NewTabPagePreloading.removePreloadedBrowser(window);
  restoreFeedState();
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});
