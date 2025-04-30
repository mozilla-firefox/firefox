/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

registerCleanupFunction(() =>
  Services.prefs.clearUserPref(POSITION_SETTING_PREF)
);

add_task(async function test_sidebar_position_start() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;
  const sidebar = document.getElementById("sidebar-main");
  const sidebarBox = document.getElementById("sidebar-box");
  ok(sidebar, "Sidebar is shown.");
  await sidebar.updateComplete;

  is(sidebar.style.order, "1", "Sidebar is shown at the start");
  is(sidebarBox.style.order, "3", "Sidebar is shown at the start");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_sidebar_position_end() {
  Services.prefs.setBoolPref(POSITION_SETTING_PREF, false);

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;
  const sidebar = document.getElementById("sidebar-main");
  const sidebarBox = document.getElementById("sidebar-box");
  ok(sidebar, "Sidebar is shown.");
  await sidebar.updateComplete;

  is(sidebar.style.order, "5", "Sidebar is shown at the end");
  is(sidebarBox.style.order, "3", "Sidebar is shown at the end");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_sidebar_position_end_new_window() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;
  const sidebar = document.getElementById("sidebar-main");
  const sidebarBox = document.getElementById("sidebar-box");
  ok(sidebar, "Sidebar is shown.");
  await sidebar.updateComplete;

  is(sidebar.style.order, "5", "Sidebar is shown at the end");
  is(sidebarBox.style.order, "3", "Sidebar is shown at the end");

  await BrowserTestUtils.closeWindow(win);
});
