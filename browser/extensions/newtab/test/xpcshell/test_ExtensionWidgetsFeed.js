/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionWidgetsFeed:
    "resource://newtab/lib/Widgets/ExtensionWidgetsFeed.sys.mjs",
  WIDGET_REGISTRY: "resource://newtab/common/WidgetsRegistry.mjs",
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const EXTENSION_ID = "fixture-widget@mozilla.org";

const SRC = "moz-extension://11111111-2222-3333-4444-555555555555/widget.html";

function registerWidget(id, options = {}) {
  WidgetManager.register({
    id,
    src: SRC,
    name: "Tally",
    sizes: ["medium"],
    ...options,
  });
}

function initFeed() {
  const feed = new ExtensionWidgetsFeed();
  feed.store = { dispatch: sinon.spy() };
  feed.onAction({ type: actionTypes.INIT });
  return feed;
}

function lastAction(feed, type) {
  return feed.store.dispatch.args
    .map(([action]) => action)
    .findLast(action => action.type === type);
}

function lastExtensionWidgets(feed) {
  return lastAction(feed, actionTypes.WIDGETS_EXTENSIONS_UPDATE).data;
}

add_setup(() => {
  WidgetManager.init();
  const widget = { id: "fixtureWidget", extensionID: EXTENSION_ID };
  WIDGET_REGISTRY.push(widget);
  registerCleanupFunction(() => {
    WIDGET_REGISTRY.splice(WIDGET_REGISTRY.indexOf(widget), 1);
  });
});

add_task(function test_init_and_unregister() {
  registerWidget(EXTENSION_ID);

  const feed = initFeed();
  Assert.ok(
    feed.store.dispatch.args.every(([action]) => action.meta.isStartup),
    "INIT broadcasts are startup actions"
  );
  Assert.deepEqual(lastExtensionWidgets(feed), {
    [EXTENSION_ID]: {
      src: SRC,
      name: "Tally",
      telemetryName: EXTENSION_ID,
      validSizes: ["medium"],
    },
  });

  WidgetManager.unregister(EXTENSION_ID);
  Assert.deepEqual(
    lastExtensionWidgets(feed),
    {},
    "Unregistered widget removed"
  );
  feed.onAction({ type: actionTypes.UNINIT });
});

add_task(function test_generated_widget() {
  registerWidget(EXTENSION_ID, { generated: true });

  const feed = initFeed();
  Assert.deepEqual(lastExtensionWidgets(feed), {
    [EXTENSION_ID]: {
      src: SRC,
      name: "Tally",
      telemetryName: EXTENSION_ID,
      validSizes: ["medium"],
      generated: true,
    },
  });

  WidgetManager.unregister(EXTENSION_ID);
  feed.onAction({ type: actionTypes.UNINIT });
});

add_task(function test_ignores_widget_not_in_registry() {
  const feed = initFeed();
  feed.store.dispatch.resetHistory();

  registerWidget("other@mozilla.org");
  WidgetManager.unregister("other@mozilla.org");
  Assert.ok(feed.store.dispatch.notCalled, "No broadcast");

  registerWidget(EXTENSION_ID);
  Assert.deepEqual(
    Object.keys(lastExtensionWidgets(feed)),
    [EXTENSION_ID],
    "Registry widget added"
  );

  WidgetManager.unregister(EXTENSION_ID);
  feed.onAction({ type: actionTypes.UNINIT });
});
