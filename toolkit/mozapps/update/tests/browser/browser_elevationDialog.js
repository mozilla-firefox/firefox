/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function elevation_dialog() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_APP_UPDATE_DISABLEDFORTESTING, false]],
  });

  // Ensure the update service is initialized before we start opening
  // elevation dialogs. Otherwise, the dialog's onLoad calling
  // getReadyUpdate() would trigger a full initialization (including
  // another #asyncInit) that opens an extra dialog.
  await initUpdateService();

  // Create a mock of nsIAppStartup's quit method so clicking the restart button
  // won't restart the application.
  let { startup } = Services;
  let appStartup = {
    QueryInterface: ChromeUtils.generateQI(["nsIAppStartup"]),
    canQuit() {
      return true;
    },
    quit(_mode) {
      if (elevationDialog) {
        elevationDialog.close();
        elevationDialog = null;
      }
    },
  };
  Services.startup = appStartup;
  registerCleanupFunction(() => {
    Services.startup = startup;
  });

  registerCleanupFunction(async () => {
    let win = Services.wm.getMostRecentWindow("Update:Elevation");
    if (win) {
      win.close();
      await TestUtils.waitForCondition(
        () => !Services.wm.getMostRecentWindow("Update:Elevation"),
        "The Update Elevation dialog should have closed"
      );
    }
  });

  // Test clicking the "Restart Later" button
  let elevationDialog = await waitForElevationDialog();
  await TestUtils.waitForTick();
  elevationDialog.document.getElementById("elevateExtra2").click();
  await TestUtils.waitForCondition(
    () => !Services.wm.getMostRecentWindow("Update:Elevation"),
    "The Update Elevation dialog should have closed"
  );
  let readyUpdate = await gUpdateManager.getReadyUpdate();
  ok(!!readyUpdate, "There should be a ready update");
  is(
    readyUpdate.state,
    STATE_PENDING_ELEVATE,
    "The ready update state should equal " + STATE_PENDING_ELEVATE
  );
  is(
    readStatusFile(),
    STATE_PENDING_ELEVATE,
    "The status file state should equal " + STATE_PENDING_ELEVATE
  );

  // Test clicking the "No Thanks" button
  elevationDialog = await waitForElevationDialog();
  await TestUtils.waitForTick();
  elevationDialog.document.getElementById("elevateExtra1").click();
  await TestUtils.waitForCondition(
    () => !Services.wm.getMostRecentWindow("Update:Elevation"),
    "The Update Elevation dialog should have closed"
  );
  readyUpdate = await gUpdateManager.getReadyUpdate();
  ok(!readyUpdate, "There should not be a ready update");
  is(
    readStatusFile(),
    STATE_NONE,
    "The status file state should equal " + STATE_NONE
  );

  // Test clicking the "Restart <brandShortName>" button
  elevationDialog = await waitForElevationDialog();
  await TestUtils.waitForTick();
  elevationDialog.document.getElementById("elevateAccept").click();
  await TestUtils.waitForCondition(
    () => !Services.wm.getMostRecentWindow("Update:Elevation"),
    "The Update Elevation dialog should have closed"
  );
  readyUpdate = await gUpdateManager.getReadyUpdate();
  ok(!!readyUpdate, "There should be a ready update");
  is(
    readyUpdate.state,
    STATE_PENDING_ELEVATE,
    "The active update state should equal " + STATE_PENDING_ELEVATE
  );
  is(
    readStatusFile(),
    STATE_PENDING,
    "The status file state should equal " + STATE_PENDING
  );
});

/**
 * Waits for the Update Elevation Dialog to load.
 *
 * @return A promise that returns the domWindow for the Update Elevation Dialog
 *         and resolves when the Update Elevation Dialog loads.
 */
async function waitForElevationDialog() {
  let elevationDialogLoadedPromise = new Promise(resolve => {
    var listener = {
      onOpenWindow: aXULWindow => {
        debugDump("Update Elevation dialog shown...");
        Services.wm.removeListener(listener);

        async function elevationDialogOnLoad() {
          domwindow.removeEventListener("load", elevationDialogOnLoad, true);
          let chromeURI =
            "chrome://mozapps/content/update/updateElevation.xhtml";
          is(
            domwindow.document.location.href,
            chromeURI,
            "Update Elevation appeared"
          );
          resolve(domwindow);
        }

        var domwindow = aXULWindow.docShell.domWindow;
        domwindow.addEventListener("load", elevationDialogOnLoad, true);
      },
      onCloseWindow: _aXULWindow => {},
    };

    Services.wm.addListener(listener);
  });
  // Add the active-update.xml and update.status files used for these tests,
  // reload the update manager, and then simulate startup so the Update
  // Elevation Dialog is opened.
  let patchProps = { state: STATE_PENDING_ELEVATE };
  let patches = getLocalPatchString(patchProps);
  let updateProps = { checkInterval: "1" };
  let updates = getLocalUpdateString(updateProps, patches);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);
  writeStatusFile(STATE_PENDING_ELEVATE);

  // Write a dummy MAR to make this look like a valid startup state.
  const readyMarFile = getUpdateDirFile(FILE_UPDATE_MAR, DIR_PATCH);
  writeFile(readyMarFile, "test mar contents");
  Assert.ok(readyMarFile.exists(), `exists: ${readyMarFile.path}`);

  await reloadUpdateManagerData();
  await testPostUpdateProcessing();

  return elevationDialogLoadedPromise;
}
