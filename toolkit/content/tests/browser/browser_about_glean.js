/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_about_glean_redesign_views_hidden_behind_pref() {
  await BrowserTestUtils.withNewTab("about:glean", async function (browser) {
    ok(!browser.isRemoteBrowser, "Browser should not be remote.");
    await SpecialPowers.spawn(browser, [], async function () {
      let metrics_table_category_button = content.document.getElementById(
        "category-metrics-table-button"
      );
      Assert.notEqual(
        metrics_table_category_button,
        null,
        "Metrics table category button should not be null"
      );
    });
  });
});

add_task(async function test_about_glean_metrics_table_loads_dynamically() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("about.glean.redesign.enabled");
  });
  Services.prefs.setBoolPref("about.glean.redesign.enabled", true);

  await BrowserTestUtils.withNewTab("about:glean", async function (browser) {
    ok(!browser.isRemoteBrowser, "Browser should not be remote.");
    await SpecialPowers.spawn(browser, [], async function () {
      const { TestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/TestUtils.sys.mjs"
      );
      content.document.getElementById("category-metrics-table-button").click();

      let tableContainer, tableBody;
      const fetchTableBody = () => {
        tableContainer = content.document.getElementById(
          "metrics-table-instance"
        );
        tableBody = content.document.getElementById("metrics-table-body");
      };
      fetchTableBody();

      let currentChildrenLength = tableBody.children.length;
      const tableChildrenLengthChanged = () => {
        fetchTableBody();
        if (currentChildrenLength != tableBody.children.length) {
          currentChildrenLength = tableBody.children.length;
          return true;
        }
        return false;
      };
      let currentFirstChild =
        tableBody.children[0].attributes["data-d3-row"].value;
      const tableFirstChildChanged = () => {
        fetchTableBody();
        if (
          currentFirstChild !=
          tableBody.children[0].attributes["data-d3-row"].value
        ) {
          currentFirstChild =
            tableBody.children[0].attributes["data-d3-row"].value;
          return true;
        }
        return false;
      };

      Assert.equal(
        currentChildrenLength,
        200,
        "Table should start with 200 elements in it"
      );

      // Scroll and extend the count to 300
      tableContainer.scrollTo({
        top: tableContainer.scrollHeight - 1000,
        behavior: "instant",
      });

      await TestUtils.waitForCondition(
        tableChildrenLengthChanged,
        "Wait for table children length to change",
        200,
        5
      );

      Assert.equal(
        tableBody.children.length,
        300,
        "Table should now have 300 elements in it"
      );

      // Scroll and extend the count to 400
      tableContainer.scrollTo({
        top: tableContainer.scrollHeight - 1000,
        behavior: "instant",
      });

      await TestUtils.waitForCondition(
        tableChildrenLengthChanged,
        "Wait for table children length to change",
        200,
        5
      );

      Assert.equal(
        tableBody.children.length,
        400,
        "Table should now have 400 elements in it"
      );

      // Scroll and extend the count to 500
      tableContainer.scrollTo({
        top: tableContainer.scrollHeight - 1000,
        behavior: "instant",
      });

      await TestUtils.waitForCondition(
        tableChildrenLengthChanged,
        "Wait for table children length to change",
        200,
        5
      );

      Assert.equal(
        tableBody.children.length,
        500,
        "Table should now have 500 elements in it"
      );

      // Scroll offset the metrics by 100
      tableContainer.scrollTo({
        top: tableContainer.scrollHeight - 1000,
        behavior: "instant",
      });

      await TestUtils.waitForCondition(
        tableFirstChildChanged,
        "Wait for the table's first child to change",
        100,
        3
      );

      Assert.equal(
        tableBody.children.length,
        500,
        "Table should still have 500 elements in it"
      );
    });
  });
});

add_task(async function test_about_glean_histogram() {
  Services.fog.testResetFOG();
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("about.glean.redesign.enabled");
  });
  Services.prefs.setBoolPref("about.glean.redesign.enabled", true);

  await BrowserTestUtils.withNewTab("about:glean", async function (browser) {
    ok(!browser.isRemoteBrowser, "Browser should not be remote.");
    await SpecialPowers.spawn(browser, [], async function () {
      const { TestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/TestUtils.sys.mjs"
      );
      content.document.getElementById("category-metrics-table-button").click();

      let tableBody;
      const fetchTableBody = () => {
        tableBody = content.document.getElementById("metrics-table-body");
      };
      fetchTableBody();
      let currentFirstChild =
        tableBody.children[0].attributes["data-d3-row"].value;

      const tableFirstChildChanged = () => {
        fetchTableBody();
        if (
          currentFirstChild !=
          tableBody.children[0].attributes["data-d3-row"].value
        ) {
          currentFirstChild =
            tableBody.children[0].attributes["data-d3-row"].value;
          return true;
        }
        return false;
      };

      const input = content.document.getElementById("filter-metrics");
      input.value = "aCustomDist";
      input.dispatchEvent(new content.Event("input"));

      await TestUtils.waitForCondition(
        tableFirstChildChanged,
        "Wait for the table's first child to change",
        100,
        3
      );
    });

    Glean.testOnlyIpc.aCustomDist.accumulateSamples([0, 0, 1, 1, 1]);

    await SpecialPowers.spawn(browser, [], async function () {
      content.document
        .querySelector(
          "[data-d3-row='testOnlyIpc.aCustomDist'] button[data-l10n-id='about-glean-button-load-value']"
        )
        .click();

      let valueCell;
      const getValueCell = () => {
        valueCell = content.document.querySelector(
          "[data-d3-row='testOnlyIpc.aCustomDist'] [data-d3-cell='value']"
        );
      };
      getValueCell();

      Assert.equal(valueCell.childElementCount, 1);
      Assert.equal(valueCell.firstChild.tagName, "svg");

      //            td        svg        g.boxes    g*n     >rect+text+text
      const boxes = valueCell.firstChild.firstChild.children;

      Assert.equal(boxes.length, 2, "Bar graphs for bucket 0 and 1 shown");
      Assert.equal(boxes[0].children[1].textContent, "2");
      Assert.equal(boxes[0].children[2].textContent, "0");
      Assert.equal(boxes[1].children[1].textContent, "3");
      Assert.equal(boxes[1].children[2].textContent, "1");
    });

    Services.fog.testResetFOG();

    await SpecialPowers.spawn(browser, [], async function () {
      content.document
        .querySelector(
          "[data-d3-row='testOnlyIpc.aCustomDist'] button[data-l10n-id='about-glean-button-load-value']"
        )
        .click();

      const valueCell = content.document.querySelector(
        "[data-d3-row='testOnlyIpc.aCustomDist'] [data-d3-cell='value']"
      );

      Assert.equal(valueCell.childElementCount, 1);
      Assert.equal(valueCell.firstChild.tagName, "P");
      Assert.equal(
        valueCell.firstChild.getAttribute("data-l10n-id"),
        "about-glean-no-data-to-display"
      );
    });
  });
});

add_task(async function test_about_glean_event_timeline() {
  Services.fog.testResetFOG();
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("about.glean.redesign.enabled");
  });
  Services.prefs.setBoolPref("about.glean.redesign.enabled", true);

  await BrowserTestUtils.withNewTab("about:glean", async function (browser) {
    ok(!browser.isRemoteBrowser, "Browser should not be remote.");
    await SpecialPowers.spawn(browser, [], async function () {
      const { TestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/TestUtils.sys.mjs"
      );
      content.document.getElementById("category-metrics-table-button").click();

      let tableBody;
      const fetchTableBody = () => {
        tableBody = content.document.getElementById("metrics-table-body");
      };
      fetchTableBody();
      let currentFirstChild =
        tableBody.children[0].attributes["data-d3-row"].value;

      const tableFirstChildChanged = () => {
        fetchTableBody();
        if (
          currentFirstChild !=
          tableBody.children[0].attributes["data-d3-row"].value
        ) {
          currentFirstChild =
            tableBody.children[0].attributes["data-d3-row"].value;
          return true;
        }
        return false;
      };

      const input = content.document.getElementById("filter-metrics");
      input.value = "anEvent";
      input.dispatchEvent(new content.Event("input"));

      await TestUtils.waitForCondition(
        tableFirstChildChanged,
        "Wait for the table's first child to change",
        100,
        3
      );
    });

    let extra = {
      value: "a value for Telemetry",
      extra1: "can set extras",
      extra2: "passing more data",
    };
    Glean.testOnlyIpc.anEvent.record(extra);
    Glean.testOnlyIpc.anEvent.record();
    Glean.testOnlyIpc.anEvent.record();

    await SpecialPowers.spawn(
      browser,
      [extra.value],
      async function (extraValue) {
        const { TestUtils } = ChromeUtils.importESModule(
          "resource://testing-common/TestUtils.sys.mjs"
        );

        content.document
          .querySelector(
            "[data-d3-row='testOnlyIpc.anEvent'] button[data-l10n-id='about-glean-button-load-value']"
          )
          .click();

        let valueCell;
        const getValueCell = () => {
          valueCell = content.document.querySelector(
            "[data-d3-row='testOnlyIpc.anEvent'] [data-d3-cell='value']"
          );
        };
        getValueCell();

        Assert.equal(valueCell.childElementCount, 2);
        Assert.equal(valueCell.firstChild.tagName, "svg");

        let code = content.document.querySelector(
          `[data-d3-row='testOnlyIpc.anEvent'] pre>code`
        ).textContent;
        const codeChanged = () => {
          const newCode = content.document.querySelector(
            `[data-d3-row='testOnlyIpc.anEvent'] pre>code`
          ).textContent;
          if (newCode != code) {
            code = newCode;
            return true;
          }
          return false;
        };

        content.document
          .querySelector(`[data-d3-row='testOnlyIpc.anEvent'] g.event`)
          .focus();

        await TestUtils.waitForCondition(
          codeChanged,
          "Wait for the table row's code textContent to change",
          100,
          3
        );

        getValueCell();
        Assert.equal(valueCell.childElementCount, 2);
        Assert.equal(valueCell.lastChild.tagName, "PRE");
        Assert.equal(
          valueCell.lastChild.firstChild.textContent.includes(extraValue),
          true
        );
      }
    );
  });
});

add_task(async function test_about_glean_ping_groups_and_none_label() {
  await BrowserTestUtils.withNewTab("about:glean", async browser => {
    await SpecialPowers.spawn(browser, [], async function () {
      const { TestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/TestUtils.sys.mjs"
      );
      const { Assert } = ChromeUtils.importESModule(
        "resource://testing-common/Assert.sys.mjs"
      );

      // Wait for the select and optgroups to exist and be populated.
      await TestUtils.waitForCondition(() => {
        const doc = content.document;
        const select = doc.getElementById("ping-names");
        const builtin = doc.getElementById("builtin-pings");
        const custom = doc.getElementById("custom-pings");
        return select && builtin && custom && builtin.children.length >= 1;
      }, "Wait for ping select to be populated");

      const doc = content.document;
      const builtin = doc.getElementById("builtin-pings");
      const custom = doc.getElementById("custom-pings");

      // Built-in group should contain metrics/events/baseline and end with the localized 'none' option.
      const builtinOptions = Array.from(builtin.children);
      Assert.strictEqual(
        builtinOptions.length,
        4,
        "Built-in group has 4 options (incl. none)"
      );

      Assert.equal(
        builtinOptions[0].textContent,
        "metrics",
        "Built-ins include 'metrics'"
      );

      Assert.equal(
        builtinOptions[1].textContent,
        "events",
        "Built-ins include 'events'"
      );

      Assert.equal(
        builtinOptions[2].textContent,
        "baseline",
        "Built-ins include 'baseline'"
      );

      // Note - We're checking the value instead of textContent
      // for the last built in option, as textContent may be localized.
      Assert.equal(
        builtinOptions[3].value,
        "(don't submit any ping)",
        "Built-ins include '(don't submit any ping)'"
      );

      // Custom group should be alphabetically sorted by displayed label.
      const customLabels = Array.from(custom.children).map(o => o.textContent);
      if (customLabels.length >= 2) {
        const sorted = customLabels.slice().sort((a, b) => a.localeCompare(b));
        Assert.deepEqual(
          customLabels,
          sorted,
          "Custom ping options are alphabetically sorted"
        );
      }
    });
  });
});

add_task(async function test_about_glean_metrics_table_settings() {
  Services.fog.testResetFOG();
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("about.glean.redesign.enabled");
  });
  Services.prefs.setBoolPref("about.glean.redesign.enabled", true);

  await BrowserTestUtils.withNewTab("about:glean", async browser => {
    await SpecialPowers.spawn(browser, [], async function () {
      const { TestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/TestUtils.sys.mjs"
      );
      const { Assert } = ChromeUtils.importESModule(
        "resource://testing-common/Assert.sys.mjs"
      );

      content.document.getElementById("category-metrics-table-button").click();

      let tableBody;
      const fetchTableBody = () => {
        tableBody = content.document.getElementById("metrics-table-body");
      };
      fetchTableBody();
      let currentFirstChild =
        tableBody.children[0].attributes["data-d3-row"].value;

      const tableFirstChildChanged = () => {
        fetchTableBody();
        if (
          currentFirstChild !=
          tableBody.children[0].attributes["data-d3-row"].value
        ) {
          currentFirstChild =
            tableBody.children[0].attributes["data-d3-row"].value;
          return true;
        }
        return false;
      };

      const input = content.document.getElementById("filter-metrics");
      input.value = "aBool";
      input.dispatchEvent(new content.Event("input"));

      await TestUtils.waitForCondition(
        tableFirstChildChanged,
        "Wait for the table's first child to change",
        100,
        3
      );

      content.document
        .querySelector(
          "[data-d3-row='testOnlyIpc.aBool'] button[data-l10n-id='about-glean-button-load-value']"
        )
        .click();

      content.document.getElementById("metrics-table-settings-button").click();

      const checkbox = content.document.getElementById(
        "settings-hide-empty-value-rows"
      );
      checkbox.checked = true;
      checkbox.dispatchEvent(new content.Event("input"));

      const tableNoChildren = () => {
        fetchTableBody();
        if (tableBody.children.length === 0) {
          return true;
        }
        return false;
      };

      await TestUtils.waitForCondition(
        tableNoChildren,
        "Wait for the table's children to be empty",
        100,
        3
      );

      Assert.equal(
        content.document.querySelector("[data-d3-row='testOnlyIpc.aBool']"),
        null,
        "Data row for `testOnlyIpc.aBool` should not exist"
      );
    });
  });
});

add_task(
  {
    skip_if: () => true,
  },
  async function test_about_glean_export_button() {
    Services.fog.testResetFOG();
    registerCleanupFunction(() => {
      Services.prefs.clearUserPref("about.glean.redesign.enabled");
    });
    Services.prefs.setBoolPref("about.glean.redesign.enabled", true);

    await BrowserTestUtils.withNewTab("about:glean", async browser => {
      await SpecialPowers.spawn(browser, [], async function () {
        const { TestUtils } = ChromeUtils.importESModule(
          "resource://testing-common/TestUtils.sys.mjs"
        );

        content.document
          .getElementById("category-metrics-table-button")
          .click();

        let tableBody;
        const fetchTableBody = () => {
          tableBody = content.document.getElementById("metrics-table-body");
        };
        fetchTableBody();
        let currentFirstChild =
          tableBody.children[0].attributes["data-d3-row"].value;

        const tableFirstChildChanged = () => {
          fetchTableBody();
          if (
            currentFirstChild !=
            tableBody.children[0].attributes["data-d3-row"].value
          ) {
            currentFirstChild =
              tableBody.children[0].attributes["data-d3-row"].value;
            return true;
          }
          return false;
        };

        const input = content.document.getElementById("filter-metrics");
        input.value = "anEvent";
        input.dispatchEvent(new content.Event("input"));

        await TestUtils.waitForCondition(
          tableFirstChildChanged,
          "Wait for the table's first child to change",
          100,
          3
        );
      });

      const date = new Date();
      const extra = {
        value: "a value for Telemetry",
        extra1: "can set extras",
        extra2: "passing more data",
      };
      Glean.testOnlyIpc.anEvent.record(extra);

      await SpecialPowers.spawn(
        browser,
        [date.getTime()],
        async function (dateTime) {
          const { TestUtils } = ChromeUtils.importESModule(
            "resource://testing-common/TestUtils.sys.mjs"
          );
          const { Assert } = ChromeUtils.importESModule(
            "resource://testing-common/Assert.sys.mjs"
          );

          content.document
            .querySelector(
              "[data-d3-row='testOnlyIpc.anEvent'] button[data-l10n-id='about-glean-button-load-value']"
            )
            .click();

          content.document.getElementById("export-data").click();

          const date = new Date(dateTime);
          let downloads = undefined,
            newDownloads = undefined;
          const downloadDir = Services.dirsvc.get("DfltDwnld", Ci.nsIFile).path;
          const downloadRegex = /about-glean-export-([\d-\.TZ_:]+)\.json/;

          const downloadsContainsGleanData = () => {
            IOUtils.getChildren(downloadDir).then(
              v => {
                downloads = v;
                newDownloads = v
                  .filter(d => downloadRegex.test(d))
                  .map(d => [
                    d,
                    new Date(d.match(downloadRegex)[1].replaceAll("_", ":")),
                  ])
                  .filter(([_, fileDate]) => fileDate - date >= 0);
              },
              reason => {
                ok(false, reason + (reason.stack ? "\n" + reason.stack : ""));
              }
            );
            return newDownloads !== undefined && !!newDownloads.length;
          };

          await TestUtils.waitForCondition(
            downloadsContainsGleanData,
            "Wait for the downloads to contain new Glean data",
            100,
            50
          );

          Assert.equal(
            1,
            newDownloads.length,
            `New downloads has an unexpected value in list of downloads: ${JSON.stringify(downloads.filter(d => d.includes("glean")))}`
          );

          const data = await IOUtils.readJSON(newDownloads[0][0]);
          // Clean up downloaded file
          await IOUtils.remove(newDownloads[0][0]);
          const index = data.findIndex(d => d.name === "testOnlyIpc.anEvent");
          Assert.greaterOrEqual(index, 0);
          Assert.notEqual(undefined, data[index].value);
        }
      );
    });
    DownloadsPanel.hidePanel();
  }
);
