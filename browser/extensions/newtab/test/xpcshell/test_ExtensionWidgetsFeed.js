/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionWidgetsFeed:
    "resource://newtab/lib/Widgets/ExtensionWidgetsFeed.sys.mjs",
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  getWidgetOrder: "resource://newtab/common/WidgetsRegistry.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const EXTENSION_ID = "fixture-widget@mozilla.org";
const OTHER_ID = "other-widget@mozilla.org";

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

function removeWidgets(...ids) {
  for (const id of ids) {
    WidgetManager.unregister(id);
    WidgetManager.cleanupPrefs(id);
  }
}

function initFeed(order = "", otherValues = {}) {
  const feed = new ExtensionWidgetsFeed();
  const values = { "widgets.order": order, ...otherValues };
  feed.store = {
    dispatch: sinon.spy(action => {
      if (action.type === actionTypes.SET_PREF) {
        values[action.data.name] = action.data.value;
      }
    }),
    getState: () => ({ Prefs: { values } }),
  };
  feed.onAction({ type: actionTypes.INIT });
  return feed;
}

function actionsOfType(feed, type) {
  return feed.store.dispatch.args
    .map(([action]) => action)
    .filter(action => action.type === type);
}

function lastExtensionWidgets(feed) {
  return actionsOfType(feed, actionTypes.WIDGETS_EXTENSIONS_UPDATE).at(-1).data;
}

function getOrder(feed) {
  return feed.store.getState().Prefs.values["widgets.order"];
}

add_setup(() => {
  WidgetManager.init();
});

add_task(function test_init_and_unregister() {
  registerWidget(EXTENSION_ID);

  const feed = initFeed();
  Assert.ok(
    actionsOfType(feed, actionTypes.WIDGETS_EXTENSIONS_UPDATE).every(
      action => action.meta.isStartup
    ),
    "INIT broadcasts are startup actions"
  );
  Assert.deepEqual(lastExtensionWidgets(feed), {
    [EXTENSION_ID]: {
      id: EXTENSION_ID,
      extensionID: EXTENSION_ID,
      src: SRC,
      name: "Tally",
      validSizes: ["medium"],
      enabled: false,
      size: "medium",
      telemetryName: EXTENSION_ID,
    },
  });

  WidgetManager.unregister(EXTENSION_ID);
  Assert.deepEqual(
    lastExtensionWidgets(feed),
    {},
    "Unregistered widget removed"
  );
  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID);
});

add_task(function test_publishes_every_widget() {
  const feed = initFeed();

  registerWidget(EXTENSION_ID);
  registerWidget(OTHER_ID);
  Assert.deepEqual(
    Object.keys(lastExtensionWidgets(feed)),
    [EXTENSION_ID, OTHER_ID],
    "Both widgets published"
  );

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID, OTHER_ID);
});

add_task(function test_generated_widget() {
  registerWidget(EXTENSION_ID, {
    generated: true,
    sizes: ["small", "medium"],
  });

  const feed = initFeed();
  Assert.deepEqual(lastExtensionWidgets(feed), {
    [EXTENSION_ID]: {
      id: EXTENSION_ID,
      extensionID: EXTENSION_ID,
      src: SRC,
      name: "Tally",
      validSizes: ["small", "medium"],
      enabled: false,
      size: "small",
      telemetryName: "generated",
      generated: true,
      systemEnabledPref: "widgets.generated.enabled",
    },
  });

  const widget = WidgetManager.getWidgetByID(EXTENSION_ID);
  widget.enabled = true;
  widget.size = "medium";
  const { enabled, size } = lastExtensionWidgets(feed)[EXTENSION_ID];
  Assert.ok(enabled, "Enabled state followed");
  Assert.equal(size, "medium", "Size followed");

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID);
});

add_task(function test_new_widget_added_to_start_of_order() {
  registerWidget(EXTENSION_ID, { generated: true });

  const feed = initFeed("lists,timer");
  Assert.equal(
    getOrder(feed),
    `${EXTENSION_ID},lists,timer`,
    "Widget added at INIT"
  );

  registerWidget(OTHER_ID, { generated: true });
  Assert.equal(
    getOrder(feed),
    `${OTHER_ID},${EXTENSION_ID},lists,timer`,
    "Widget added on change"
  );

  feed.store.dispatch.resetHistory();
  WidgetManager.getWidgetByID(EXTENSION_ID).enabled = true;
  Assert.deepEqual(actionsOfType(feed, actionTypes.SET_PREF), [], "Order kept");

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID, OTHER_ID);
});

add_task(function test_empty_order_starts_from_effective_order() {
  registerWidget(EXTENSION_ID, { generated: true });

  let feed = initFeed();
  Assert.equal(
    getOrder(feed),
    [EXTENSION_ID, ...getWidgetOrder(null)].join(","),
    "Registry order kept"
  );
  feed.onAction({ type: actionTypes.UNINIT });

  feed = initFeed("", {
    trainhopConfig: { widgets: { order: "timer,lists" } },
  });
  Assert.equal(
    getOrder(feed),
    [EXTENSION_ID, ...getWidgetOrder("timer,lists")].join(","),
    "Trainhop order kept"
  );

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID);
});

add_task(function test_other_widget_not_added_to_order() {
  registerWidget(EXTENSION_ID);

  const feed = initFeed();
  Assert.deepEqual(
    actionsOfType(feed, actionTypes.SET_PREF),
    [],
    "Order not written"
  );

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID);
});

add_task(function test_uninstall_removes_widget_from_order() {
  registerWidget(EXTENSION_ID);
  const feed = initFeed("lists");

  removeWidgets(EXTENSION_ID);
  Assert.equal(getOrder(feed), "lists", "Widget removed from order");

  feed.onAction({ type: actionTypes.UNINIT });
});

add_task(function test_set_writes_through_widget_manager() {
  registerWidget(EXTENSION_ID, { sizes: ["small", "medium"] });
  const feed = initFeed();
  const widget = WidgetManager.getWidgetByID(EXTENSION_ID);

  feed.onAction({
    type: actionTypes.WIDGETS_EXTENSIONS_SET,
    data: { id: EXTENSION_ID, enabled: true },
  });
  Assert.ok(widget.enabled, "Enabled state set");
  Assert.equal(widget.size, "small", "Size unchanged");

  feed.onAction({
    type: actionTypes.WIDGETS_EXTENSIONS_SET,
    data: { id: EXTENSION_ID, size: "medium" },
  });
  Assert.equal(widget.size, "medium", "Size set");
  Assert.ok(widget.enabled, "Enabled state unchanged");
  Assert.deepEqual(
    lastExtensionWidgets(feed)[EXTENSION_ID],
    {
      id: EXTENSION_ID,
      extensionID: EXTENSION_ID,
      src: SRC,
      name: "Tally",
      validSizes: ["small", "medium"],
      enabled: true,
      size: "medium",
      telemetryName: EXTENSION_ID,
    },
    "State rebroadcast"
  );

  feed.onAction({
    type: actionTypes.WIDGETS_EXTENSIONS_SET,
    data: { id: "missing@mozilla.org", enabled: true },
  });

  feed.onAction({ type: actionTypes.UNINIT });
  removeWidgets(EXTENSION_ID);
});
