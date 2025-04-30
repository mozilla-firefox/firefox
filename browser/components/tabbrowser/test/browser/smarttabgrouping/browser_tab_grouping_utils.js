/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Generates a dummy tab with title, url and description
 * @param {string} title
 * @param {string} url
 * @param {string} description
 */
function generateTabWithInfo({ title, url, description = "" }) {
  return {
    label: title,
    description,
    linkedBrowser: {
      currentURI: {
        spec: url,
      },
    },
  };
}

/**
 * Returns a list of dummy tab data from an existing filename
 * @param {string} filename path to local file
 * @return {Promise<object[]>} list of tabs
 */
async function prepareTabData(filename) {
  const ROOT_URL =
    "https://example.com/browser/browser/components/tabbrowser/test/browser/smarttabgrouping/data/";
  const rawLabels = await fetchFile(ROOT_URL, filename);
  const labels = await parseTsvStructured(rawLabels);
  return labels.map(l =>
    generateTabWithInfo({
      title: l.smart_group_label,
      description: `Random Description ${Math.floor(Math.random() * 100)}`,
      url: `https://example.com/${Math.floor(Math.random() * 100)}`,
    })
  );
}

add_task(
  async function test_default_tab_data_prep_should_not_have_description() {
    const smartTabGroupingManager = new SmartTabGroupingManager();
    const tabData = await prepareTabData("gen_set_2_labels.tsv");
    const preppedData = await smartTabGroupingManager._prepareTabData(tabData);
    for (let tab of preppedData) {
      Assert.ok(!tab.combined_text.includes(tab.description));
    }
  }
);

add_task(async function test_tab_data_prep_should_have_description_when_on() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const tabData = await prepareTabData("gen_set_2_labels.tsv");
  const preppedData = await smartTabGroupingManager._prepareTabData(
    tabData,
    true
  );
  for (let tab of preppedData) {
    Assert.ok(tab.combined_text.includes(tab.description));
  }
});

add_task(async function test_tabs_to_suggest_should_only_exclude_new_tab() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const allTabData = await prepareTabData("gen_set_2_labels.tsv");
  let tabData = [...allTabData.slice(0, 5)];
  let preppedData = await smartTabGroupingManager._prepareTabData(tabData);
  let tabsToSuggest = smartTabGroupingManager.getTabsToSuggest(
    preppedData,
    [0, 1],
    [3]
  );
  Assert.equal(
    tabsToSuggest.length,
    2,
    "only two tabs can be suggested from window"
  );
  Assert.deepEqual(
    tabsToSuggest,
    [2, 4],
    "only tabs at these indices can be suggested"
  );

  // about:newtab should be excluded, others like about:config should be included
  tabData = [
    ...allTabData.slice(0, 5),
    generateTabWithInfo({ title: "New Tab", url: "about:newtab" }),
    generateTabWithInfo({ title: "Config", url: "about:config" }),
    generateTabWithInfo({ title: "New Tab", url: "about:home" }),
  ];
  preppedData = await smartTabGroupingManager._prepareTabData(tabData);
  tabsToSuggest = smartTabGroupingManager.getTabsToSuggest(
    preppedData,
    [0, 1],
    [3]
  );
  Assert.equal(
    tabsToSuggest.length,
    3,
    "only three tabs can be suggested from window, with 'New Tab' excluded"
  );
  Assert.deepEqual(tabsToSuggest, [2, 4, 6], "about:newtab should be excluded");

  // about:newtab duplicates should be excluded, others like about:config should be included
  tabData = [
    ...allTabData.slice(0, 5),
    generateTabWithInfo({ title: "New Tab", url: "about:newtab" }),
    generateTabWithInfo({ title: "Config", url: "about:config" }),
    generateTabWithInfo({ title: "New Tab", url: "about:newtab" }),
    generateTabWithInfo({ title: "Config", url: "about:config" }),
    generateTabWithInfo({ title: "New Tab", url: "about:home" }),
    generateTabWithInfo({ title: "New Tab", url: "about:home" }),
  ];
  preppedData = await smartTabGroupingManager._prepareTabData(tabData);
  tabsToSuggest = smartTabGroupingManager.getTabsToSuggest(
    preppedData,
    [0, 1],
    [3]
  );
  Assert.equal(
    tabsToSuggest.length,
    4,
    "only three tabs can be suggested from window, with 'New Tab' excluded"
  );
  Assert.deepEqual(
    tabsToSuggest,
    [2, 4, 6, 8],
    "about:newtab should be excluded"
  );
});

add_task(async function test_tabs_to_suggest_should_exclude_firefox_view() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const allTabData = await prepareTabData("gen_set_2_labels.tsv");
  let tabData = [...allTabData.slice(0, 5)];
  let preppedData = await smartTabGroupingManager._prepareTabData(tabData);
  let tabsToSuggest = smartTabGroupingManager.getTabsToSuggest(
    preppedData,
    [0, 1],
    [3]
  );
  Assert.equal(
    tabsToSuggest.length,
    2,
    "only two tabs can be suggested from window"
  );
  Assert.deepEqual(
    tabsToSuggest,
    [2, 4],
    "only tabs at these indices can be suggested"
  );

  // about:firefoxview should be excluded, others like about:config should be included
  tabData = [
    ...allTabData.slice(0, 5),
    generateTabWithInfo({ title: "New Tab", url: "about:newtab" }),
    generateTabWithInfo({ title: "Config", url: "about:config" }),
    generateTabWithInfo({ title: "New Tab", url: "about:home" }),
    generateTabWithInfo({ title: "Firefox View", url: "about:firefoxview" }),
  ];
  preppedData = await smartTabGroupingManager._prepareTabData(tabData);
  tabsToSuggest = smartTabGroupingManager.getTabsToSuggest(
    preppedData,
    [0, 1],
    [3]
  );
  Assert.equal(
    tabsToSuggest.length,
    3,
    "only three tabs can be suggested from window, with 'Firefox View' excluded"
  );
  Assert.deepEqual(
    tabsToSuggest,
    [2, 4, 6],
    "about:firefoxview should be excluded"
  );
});
