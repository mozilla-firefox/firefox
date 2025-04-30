/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { SidebarState } = ChromeUtils.importESModule(
  "resource:///modules/SidebarState.sys.mjs"
);

const mockElement = {
  setAttribute(name, value) {
    this[name] = value;
  },
  style: { width: "200px" },
  toggleAttribute: sinon.stub(),
};
const mockGlobal = {
  document: { getElementById: () => mockElement },
  gBrowser: { tabContainer: mockElement },
};
const mockController = {
  _box: mockElement,
  hide: sinon.stub(),
  showInitially: sinon.stub(),
  sidebarContainer: { ownerGlobal: mockGlobal },
  sidebarMain: mockElement,
  sidebarRevampEnabled: true,
  sidebarRevampVisibility: "always-show",
  sidebars: new Set(["viewBookmarksSidebar"]),
  updateToolbarButton: sinon.stub(),
  SidebarManager: { hasSidebarLauncherBeenVisible: false },
};

add_task(async function test_load_legacy_session_restore_data() {
  const sidebarState = new SidebarState(mockController);

  sidebarState.loadInitialState({
    width: "300px",
    command: "viewBookmarksSidebar",
    expanded: true,
    hidden: false,
  });

  const props = sidebarState.getProperties();
  Assert.equal(props.panelWidth, 300, "The panel was resized.");
  Assert.equal(props.launcherExpanded, true, "The launcher is expanded.");
  Assert.equal(props.launcherVisible, true, "The launcher is visible.");
  Assert.ok(
    mockController.showInitially.calledWith("viewBookmarksSidebar"),
    "Bookmarks panel was shown."
  );
});

add_task(async function test_load_prerevamp_session_restore_data() {
  const sidebarState = new SidebarState(mockController);

  sidebarState.loadInitialState({
    command: "viewBookmarksSidebar",
  });

  const props = sidebarState.getProperties();
  Assert.ok(props.panelOpen, "The panel is marked as open.");
  Assert.equal(props.launcherVisible, true, "The launcher is visible.");
  Assert.equal(props.command, "viewBookmarksSidebar", "The command matches.");
  Assert.ok(
    mockController.showInitially.calledWith("viewBookmarksSidebar"),
    "Bookmarks panel was shown."
  );
});

add_task(async function test_load_hidden_panel_state() {
  const sidebarState = new SidebarState(mockController);

  sidebarState.loadInitialState({
    command: "viewBookmarksSidebar",
    panelOpen: false,
    launcherVisible: true,
  });

  const props = sidebarState.getProperties();
  Assert.ok(!props.panelOpen, "The panel is marked as closed.");
  Assert.equal(props.launcherVisible, true, "The launcher is visible.");
  Assert.equal(props.command, "viewBookmarksSidebar", "The command matches.");
});
