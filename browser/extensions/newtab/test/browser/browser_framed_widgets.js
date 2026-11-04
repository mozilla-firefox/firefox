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
async function setUpFramedWidgets(pushPrefs, disabledId) {
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
      widgets: WIDGETS.map(({ id }) => ({
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
    [`${PREF_BRANCH}widgets.order`, WIDGETS.map(w => w.id).join(",")],
    ...WIDGETS.flatMap(w => [
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
        frame.getAttribute("src").endsWith(`/blue_page.html?widget=${id}`),
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
