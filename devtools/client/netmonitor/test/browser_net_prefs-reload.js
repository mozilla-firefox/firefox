/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the prefs that should survive across tool reloads work.
 */

add_task(async function () {
  let { monitor } = await initNetMonitor(SIMPLE_URL, { requestCount: 1 });
  const Actions = monitor.panelWin.windowRequire(
    "devtools/client/netmonitor/src/actions/index"
  );
  info("Starting test... ");

  // This test reopens the network monitor a bunch of times, for different
  // hosts (bottom, side, window). This seems to be slow on debug builds.
  requestLongerTimeout(3);

  // Use these getters instead of caching instances inside the panel win,
  // since the tool is reopened a bunch of times during this test
  // and the instances will differ.
  const getDoc = () => monitor.panelWin.document;
  const getPrefs = () =>
    monitor.panelWin.windowRequire("devtools/client/netmonitor/src/utils/prefs")
      .Prefs;
  const getStore = () => monitor.panelWin.store;
  const getState = () => getStore().getState();

  const prefsToCheck = {
    filters: {
      // A custom new value to be used for the verified preference.
      newValue: ["html", "css"],
      // Getter used to retrieve the current value from the frontend, in order
      // to verify that the pref was applied properly.
      validateValue: () =>
        Object.entries(getState().filters.requestFilterTypes)
          .filter(([, check]) => check)
          .map(([type]) => type),
      // Predicate used to modify the frontend when setting the new pref value,
      // before trying to validate the changes.
      modifyFrontend: value =>
        value.forEach(e =>
          getStore().dispatch(Actions.toggleRequestFilterType(e))
        ),
    },
    networkDetailsWidth: {
      newValue: ~~(Math.random() * 200 + 100),
      validateValue: () =>
        getDoc().querySelector(".monitor-panel .split-box .controlled")
          .clientWidth,
      async modifyFrontend(value) {
        const splitter = getDoc().querySelector(
          ".monitor-panel .split-box .splitter"
        );
        const controlled = getDoc().querySelector(
          ".monitor-panel .split-box .controlled"
        );
        resizeSplitter(
          splitter,
          // There must be some 2px margin/border that requires an extra pixel more of move
          controlled.getBoundingClientRect().width - value + 2
        );
      },
    },
    networkDetailsHeight: {
      newValue: ~~(Math.random() * 300 + 100),
      validateValue: () =>
        getDoc().querySelector(".monitor-panel .split-box .controlled")
          .clientHeight,
      modifyFrontend(value) {
        const splitter = getDoc().querySelector(
          ".monitor-panel .split-box .splitter"
        );
        const controlled = getDoc().querySelector(
          ".monitor-panel .split-box .controlled"
        );
        resizeSplitter(
          splitter,
          0,
          // There must be some 4px margin/border that requires an extra pixel more of move
          controlled.getBoundingClientRect().height - value + 4
        );
      },
    },
    /* add more prefs here... */
  };

  await testBottom();
  await testSide();
  await testWindow();

  info("Moving toolbox back to the bottom...");
  await monitor.toolbox.switchHost("bottom");
  return teardown(monitor);

  function storeFirstPrefValues() {
    info("Caching initial pref values.");

    for (const name in prefsToCheck) {
      const currentValue = getPrefs()[name];
      prefsToCheck[name].firstValue = currentValue;
    }
  }

  function validateFirstPrefValues(isVerticalSplitter) {
    info("Validating current pref values to the UI elements.");

    for (const name in prefsToCheck) {
      if (
        (isVerticalSplitter && name === "networkDetailsHeight") ||
        (!isVerticalSplitter && name === "networkDetailsWidth")
      ) {
        continue;
      }

      const currentValue = getPrefs()[name];
      const { firstValue, validateValue } = prefsToCheck[name];

      is(
        firstValue.toString(),
        currentValue.toString(),
        "Pref " + name + " should be equal to first value: " + currentValue
      );
      is(
        validateValue().toString(),
        currentValue.toString(),
        "Pref " + name + " should validate: " + currentValue
      );
    }
  }

  function modifyFrontend(isVerticalSplitter) {
    info("Modifying UI elements to the specified new values.");

    for (const name in prefsToCheck) {
      if (
        (isVerticalSplitter && name === "networkDetailsHeight") ||
        (!isVerticalSplitter && name === "networkDetailsWidth")
      ) {
        continue;
      }

      const currentValue = getPrefs()[name];
      const { firstValue, newValue, validateValue } = prefsToCheck[name];
      const modFrontend = prefsToCheck[name].modifyFrontend;

      modFrontend(newValue);
      info("Modified UI element affecting " + name + " to: " + newValue);

      is(
        firstValue.toString(),
        currentValue.toString(),
        "Pref " +
          name +
          " should still be equal to first value: " +
          currentValue
      );
      isnot(
        newValue.toString(),
        currentValue.toString(),
        "Pref " +
          name +
          " should't yet be equal to second value: " +
          currentValue
      );
      is(
        validateValue().toString(),
        newValue.toString(),
        "The UI element affecting " + name + " should validate: " + newValue
      );
    }
  }

  function validateNewPrefValues(isVerticalSplitter) {
    info("Invalidating old pref values to the modified UI elements.");

    for (const name in prefsToCheck) {
      if (
        (isVerticalSplitter && name === "networkDetailsHeight") ||
        (!isVerticalSplitter && name === "networkDetailsWidth")
      ) {
        continue;
      }

      const currentValue = getPrefs()[name];
      const { firstValue, newValue, validateValue } = prefsToCheck[name];

      isnot(
        firstValue.toString(),
        currentValue.toString(),
        "Pref " + name + " should't be equal to first value: " + currentValue
      );
      is(
        newValue.toString(),
        currentValue.toString(),
        "Pref " + name + " should now be equal to second value: " + currentValue
      );
      is(
        validateValue().toString(),
        newValue.toString(),
        "The UI element affecting " + name + " should validate: " + newValue
      );
    }
  }

  function resetFrontend(isVerticalSplitter) {
    info("Resetting UI elements to the cached initial pref values.");

    for (const name in prefsToCheck) {
      if (
        (isVerticalSplitter && name === "networkDetailsHeight") ||
        (!isVerticalSplitter && name === "networkDetailsWidth")
      ) {
        continue;
      }

      const currentValue = getPrefs()[name];
      const { firstValue, newValue, validateValue } = prefsToCheck[name];
      const modFrontend = prefsToCheck[name].modifyFrontend;

      modFrontend(firstValue);
      info("Modified UI element affecting " + name + " to: " + firstValue);

      isnot(
        firstValue.toString(),
        currentValue.toString(),
        "Pref " +
          name +
          " should't yet be equal to first value: " +
          currentValue
      );
      is(
        newValue.toString(),
        currentValue.toString(),
        "Pref " +
          name +
          " should still be equal to second value: " +
          currentValue
      );
      is(
        validateValue().toString(),
        firstValue.toString(),
        "The UI element affecting " + name + " should validate: " + firstValue
      );
    }
  }

  async function restartNetMonitorAndSetupEnv() {
    const newMonitor = await restartNetMonitor(monitor, { requestCount: 1 });
    monitor = newMonitor.monitor;

    const networkEvent = waitForNetworkEvents(monitor, 1);
    await reloadSelectedTab();
    await networkEvent;

    const onDomUpdated = waitForDOM(getDoc(), ".network-details-bar");
    getStore().dispatch(Actions.toggleNetworkDetails());
    await onDomUpdated;
  }

  async function testBottom() {
    await restartNetMonitorAndSetupEnv();

    info("Testing prefs reload for a bottom host.");
    storeFirstPrefValues();

    // Validate and modify while toolbox is on the bottom.
    validateFirstPrefValues(true);
    modifyFrontend(true);

    await restartNetMonitorAndSetupEnv();

    // Revalidate and reset frontend while toolbox is on the bottom.
    validateNewPrefValues(true);
    resetFrontend(true);

    await restartNetMonitorAndSetupEnv();

    // Revalidate.
    validateFirstPrefValues(true);
  }

  async function testSide() {
    await restartNetMonitorAndSetupEnv();

    info("Moving toolbox to the right...");

    await monitor.toolbox.switchHost("right");

    // Switching hosts is not correctly waiting when DevTools run in content frame
    // See Bug 1571421.
    await wait(1000);

    info("Testing prefs reload for a right host.");
    storeFirstPrefValues();

    // Validate and modify frontend while toolbox is on the side.
    validateFirstPrefValues(false);
    modifyFrontend(false);

    await restartNetMonitorAndSetupEnv();

    // Revalidate and reset frontend while toolbox is on the side.
    validateNewPrefValues(false);
    resetFrontend(false);

    await restartNetMonitorAndSetupEnv();

    // Revalidate.
    validateFirstPrefValues(false);
  }

  async function testWindow() {
    await restartNetMonitorAndSetupEnv();

    info("Moving toolbox into a window...");

    await monitor.toolbox.switchHost("window");

    // Switching hosts is not correctly waiting when DevTools run in content frame
    // See Bug 1571421.
    await wait(1000);

    info("Testing prefs reload for a window host.");
    storeFirstPrefValues();

    // Validate and modify frontend while toolbox is in a window.
    validateFirstPrefValues(true);
    modifyFrontend(true);

    await restartNetMonitorAndSetupEnv();

    // Revalidate and reset frontend while toolbox is in a window.
    validateNewPrefValues(true);
    resetFrontend(true);

    await restartNetMonitorAndSetupEnv();

    // Revalidate.
    validateFirstPrefValues(true);
  }
});

function resizeSplitter(splitterEl, diffX, diffY = 0) {
  EventUtils.synthesizeMouseAtCenter(
    splitterEl,
    { type: "mousedown" },
    splitterEl.documentGlobal
  );
  EventUtils.synthesizeMouseAtPoint(
    splitterEl.getBoundingClientRect().left + diffX,
    splitterEl.getBoundingClientRect().top + diffY,
    { type: "mousemove" },
    splitterEl.documentGlobal
  );
  EventUtils.synthesizeMouseAtCenter(
    splitterEl,
    { type: "mouseup" },
    splitterEl.documentGlobal
  );
}
