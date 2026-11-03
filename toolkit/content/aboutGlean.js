/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Docs: https://devdocs.io/d3~3/
Services.scriptloader.loadSubScript(
  "chrome://global/content/third_party/d3/d3.js"
);
const d3 = this.d3;

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const ENABLE_STORED_SUBMITTED_PINGS_PREF =
  "telemetry.fog.enable_store_submitted_pings";
const METRIC_DATA = {};
let MAPPED_METRIC_DATA = [];
let FILTERED_METRIC_DATA = [];
let LIMITED_METRIC_DATA = [];
let LIMIT_OFFSET = 0;
let LIMIT_COUNT = 200;
let METRIC_DATA_INITIALIZED = false;
let STORE_SUBMITTED_PINGS_ENABLED = false;
let SUBMITTED_PING = undefined;
const CAMEL_CASE_REGEX = /_(\w)/g;
const INITIAL_CASE_REGEX = /\b\w/g;
const INVALID_VALUE_REASONS = {
  DUAL_LABELED_METRIC: 0,
  UNKNOWN_METRIC: 1,
};
const SIMPLE_TYPES = {
  Boolean: "Boolean",
  String: "String",
  StringList: "StringList",
  Text: "Text",
  Counter: "Counter",
};
const SELECTED_METRICS = [];
let DOCUMENT_BODY_SEL = undefined;

function updatePrefsAndDefines() {
  let upload = Services.prefs.getBoolPref(
    "datareporting.healthreport.uploadEnabled"
  );
  document.l10n.setAttributes(
    document.querySelector("[data-l10n-id='about-glean-data-upload']"),
    "about-glean-data-upload",
    {
      "data-upload-pref-value": upload,
    }
  );
  let port = Services.prefs.getIntPref("telemetry.fog.test.localhost_port");
  document.l10n.setAttributes(
    document.querySelector("[data-l10n-id='about-glean-local-port']"),
    "about-glean-local-port",
    {
      "local-port-pref-value": port,
    }
  );
  document.l10n.setAttributes(
    document.querySelector("[data-l10n-id='about-glean-glean-android']"),
    "about-glean-glean-android",
    { "glean-android-define-value": AppConstants.MOZ_GLEAN_ANDROID }
  );
  document.l10n.setAttributes(
    document.querySelector("[data-l10n-id='about-glean-moz-official']"),
    "about-glean-moz-official",
    { "moz-official-define-value": AppConstants.MOZILLA_OFFICIAL }
  );

  // Knowing what we know, and copying logic from viaduct_uploader.rs,
  // (which is documented in Preferences and Defines),
  // tell the fine user whether and why upload is disabled.
  let uploadMessageEl = document.getElementById("upload-status");
  let uploadL10nId = "about-glean-upload-enabled";
  if (!upload) {
    uploadL10nId = "about-glean-upload-disabled";
  } else if (port < 0 || (port == 0 && !AppConstants.MOZILLA_OFFICIAL)) {
    uploadL10nId = "about-glean-upload-fake-enabled";
    // This message has a link to the Glean Debug Ping Viewer in it.
    // We must add the anchor element now so that Fluent can match it.
    let a = document.createElement("a");
    a.href = "https://debug-ping-preview.firebaseapp.com/";
    a.setAttribute("data-l10n-name", "glean-debug-ping-viewer");
    uploadMessageEl.appendChild(a);
  } else if (port > 0) {
    uploadL10nId = "about-glean-upload-enabled-local";
  }
  document.l10n.setAttributes(uploadMessageEl, uploadL10nId);

  STORE_SUBMITTED_PINGS_ENABLED = Services.prefs.getBoolPref(
    ENABLE_STORED_SUBMITTED_PINGS_PREF
  );
}

function camelToKebab(str) {
  let out = "";
  for (let i = 0; i < str.length; i++) {
    let c = str.charAt(i);
    if (c == c.toUpperCase()) {
      out += "-";
      c = c.toLowerCase();
    }
    out += c;
  }
  return out;
}

// I'm consciously omitting "deletion-request" until someone can come up with
// a use-case for sending it via about:glean.
const GLEAN_BUILTIN_PINGS = ["metrics", "events", "baseline"];
const NO_PING = "(don't submit any ping)";

function refillPingNames() {
  const builtInGroup = document.getElementById("builtin-pings");
  const customGroup = document.getElementById("custom-pings");

  // Add built-in ping options to the dropdown.
  GLEAN_BUILTIN_PINGS.forEach(id => {
    const option = document.createElement("option");
    option.value = id;
    option.textContent = camelToKebab(id);
    builtInGroup.appendChild(option);
  });

  // Add "(don't submit any ping)" as last built-in option in the dropdown.
  const noPingOption = document.createElement("option");
  noPingOption.value = NO_PING;
  document.l10n.setAttributes(noPingOption, "about-glean-no-ping-label");
  builtInGroup.appendChild(noPingOption);

  // Add alpha sorted custom ping options to the dropdown.
  Object.keys(GleanPings)
    .map(id => ({ id, label: camelToKebab(id) }))
    .sort((a, b) => a.label.localeCompare(b.label))
    .forEach(({ label }) => {
      const option = document.createElement("option");
      option.textContent = label;
      customGroup.appendChild(option);
    });
}

// If there's been a previous tag, use it.
// If not, be _slightly_ clever and derive a default one from the profile dir.
function fillDebugTag() {
  const DEBUG_TAG_PREF = "telemetry.fog.aboutGlean.debugTag";
  let debugTag;
  if (Services.prefs.prefHasUserValue(DEBUG_TAG_PREF)) {
    debugTag = Services.prefs.getStringPref(DEBUG_TAG_PREF);
  } else {
    const debugTagPrefix = "about-glean-";
    const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile).path;
    let charSum = Array.from(profileDir).reduce(
      (prev, cur) => prev + cur.charCodeAt(0),
      0
    );

    debugTag = debugTagPrefix + (charSum % 1000);
  }

  let tagInput = document.getElementById("tag-pings");
  tagInput.value = debugTag;
  const updateDebugTagValues = () => {
    document.l10n.setAttributes(
      document.querySelector(
        "[data-l10n-id='about-glean-label-for-controls-submit']"
      ),
      "about-glean-label-for-controls-submit",
      { "debug-tag": tagInput.value }
    );
    const GDPV_ROOT = "https://debug-ping-preview.firebaseapp.com/pings/";
    let gdpvLink = document.querySelector(
      "[data-l10n-name='gdpv-tagged-pings-link']"
    );
    gdpvLink.href = GDPV_ROOT + tagInput.value;
  };
  tagInput.addEventListener("change", () => {
    Services.prefs.setStringPref(DEBUG_TAG_PREF, tagInput.value);
    updateDebugTagValues();
  });
  updateDebugTagValues();
}

function showTab(button) {
  let current_tab = document.querySelector(".active");
  let category = button.getAttribute("view");
  let content = document.getElementById(category);
  if (current_tab == content) {
    return;
  }
  current_tab.classList.remove("active");
  current_tab.hidden = true;
  content.classList.add("active");
  content.hidden = false;

  if (category == "metrics-table") {
    // Init base level metric data
    initializeMetricData();

    const table = document.getElementById("metrics-table-instance");
    table.removeAttribute("hidden");

    // Map the metric data into a better defined type structure
    MAPPED_METRIC_DATA = Object.entries(METRIC_DATA).flatMap(
      ([category, metrics]) =>
        Object.entries(metrics).map(([name, metric]) => ({
          category,
          name,
          fullName: `${category}.${name}`,
          ...metric,
        }))
    );
    updateFilteredMetricData(
      document.getElementById("filter-metrics").value.toLowerCase()
    );
    updateTable();
  }
}

function handleRedesign() {
  /**
   * Handle metric filter input.
   *
   * This uses a timeout to debounce the events down to 200ms.
   * Instead of updating the DOM every time the input changes, it'll only update when the input hasn't changed in the last 200ms since it last changed.
   */
  let inputTimeout = undefined;
  document.getElementById("filter-metrics").addEventListener("input", e => {
    clearTimeout(inputTimeout);
    inputTimeout = setTimeout(() => {
      updateFilteredMetricData(e.target.value.toLowerCase() ?? "");
    }, 200);
  });

  // Handle loading all metric data
  document.getElementById("load-all").addEventListener("click", () => {
    MAPPED_METRIC_DATA.forEach(datum => {
      updateDatum(datum);
    });
    updateTable();
  });
}

function onLoad() {
  let menu = document.getElementById("categories");
  menu.addEventListener("click", function click(e) {
    if (e.target && e.target.parentNode == menu) {
      showTab(e.target);
    }
  });
  menu.addEventListener("keydown", function keypress(e) {
    if (
      e.target &&
      e.target.parentNode == menu &&
      (e.key == " " || e.key == "Enter")
    ) {
      showTab(e.target);
    }
  });
  showTab(document.querySelector("[view='about-glean']"));

  updatePrefsAndDefines();
  refillPingNames();
  fillDebugTag();
  document.getElementById("controls-submit").addEventListener("click", () => {
    let tag = document.getElementById("tag-pings").value;
    let log = document.getElementById("log-pings").checked;
    let ping = document.getElementById("ping-names").value;
    let feedbackToast = document.getElementById("feedback");

    Services.fog.setLogPings(log);
    Services.fog.setTagPings(tag);

    if (ping != NO_PING) {
      Services.fog.sendPing(ping);
      feedbackToast.setAttribute(
        "data-l10n-id",
        "about-glean-feedback-settings-and-ping"
      );
    } else {
      feedbackToast.setAttribute(
        "data-l10n-id",
        "about-glean-feedback-settings-only"
      );
    }

    feedbackToast.style.visibility = "visible";
    setTimeout(() => {
      feedbackToast.style.visibility = "hidden";
    }, 3000);
    loadStoredSubmittedPings();
  });

  DOCUMENT_BODY_SEL = d3.select(document.body);

  handleRedesign();

  document
    .getElementById("metrics-table-settings-button")
    .addEventListener("click", () => {
      const settingsDiv = document.getElementById("metrics-table-settings");
      if (settingsDiv.getAttribute("hidden")) {
        settingsDiv.removeAttribute("hidden");
      } else {
        settingsDiv.setAttribute("hidden", true);
      }
      settingsChanged();
    });
  initMetricsSettings();

  document.getElementById("export-data").addEventListener("click", () => {
    exportData();
  });

  // Initialize storing of submitted pings
  const storeSubmittedPingsEl = document.querySelector(
    `[data-form-control='storeSubmittedPings']`
  );
  storeSubmittedPingsEl.pressed = STORE_SUBMITTED_PINGS_ENABLED;
  storeSubmittedPingsEl.addEventListener("toggle", e => {
    const enabled = e.target.pressed;
    Services.prefs.setBoolPref(ENABLE_STORED_SUBMITTED_PINGS_PREF, enabled);
    Services.fog.setStoreSubmittedPingsEnabled(enabled);
    if (!enabled) {
      Services.fog.clearStoredSubmittedPings();
      document
        .querySelectorAll("#submitted-pings-select>optgroup")
        .forEach(el => el.remove());
    }
  });

  loadStoredSubmittedPings();
}

/**
 * Loads and parses stored submitted pings
 */
function loadStoredSubmittedPings() {
  if (STORE_SUBMITTED_PINGS_ENABLED) {
    const selectEl = document.getElementById("submitted-pings-select");
    selectEl.querySelectorAll("optgroup").forEach(og => og.remove());

    const allPings = Services.fog.getAllStoredSubmittedPings().map(ping => {
      ping.submittedDate = new Date(ping.submittedDate).toISOString();
      return ping;
    });
    const pingsObj = allPings.reduce((acc, curr) => {
      if (!acc[curr.ping]) {
        acc[curr.ping] = [];
      }
      acc[curr.ping].push(curr);
      return acc;
    }, {});
    Object.keys(pingsObj).forEach(ping => {
      pingsObj[ping].toSorted((a, b) => {
        d3.descending(a.submittedDate, b.submittedDate);
      });
    });
    for (let ping of Object.keys(pingsObj).toSorted((a, b) =>
      d3.ascending(a, b)
    )) {
      const optgroupEl = document.createElement("optgroup");
      optgroupEl.label = ping;

      for (let p of pingsObj[ping]) {
        const el = document.createElement("option");
        el.innerText = `${p.ping} (${p.submittedDate})`;
        el.value = p.documentId;
        optgroupEl.appendChild(el);
      }
      selectEl.appendChild(optgroupEl);
    }

    selectEl.addEventListener("input", e => {
      if (e.target.value !== "") {
        SUBMITTED_PING = allPings.find(
          ping => ping.documentId === e.target.value
        );
        if (SUBMITTED_PING !== undefined) {
          const allMetrics = Object.keys(
            SUBMITTED_PING.payload.metrics ?? {}
          ).reduce((acc, currKey) => {
            const curr = (SUBMITTED_PING.payload.metrics ?? {})[currKey];
            const type = getTypeFromPingKey(currKey);
            return {
              ...acc,
              ...Object.keys(curr).reduce((acc, key) => {
                let split = key.split(".");
                const metric = split
                  .at(-1)
                  .replace(CAMEL_CASE_REGEX, (_, char) => char.toUpperCase());
                split = split.slice(0, -1);
                const category = split
                  .map((v, i) =>
                    i === 0
                      ? v.replace(CAMEL_CASE_REGEX, (_, char) =>
                          char.toUpperCase()
                        )
                      : v
                          .replace(INITIAL_CASE_REGEX, char =>
                            char.toUpperCase()
                          )
                          .replace(CAMEL_CASE_REGEX, (_, char) =>
                            char.toUpperCase()
                          )
                  )
                  .join("");
                acc[`${category}.${metric}`] = curr[key];

                const datum = MAPPED_METRIC_DATA.find(
                  d => d.category === category && d.name === metric
                );
                if (datum) {
                  datum.storedValue = curr[key];
                  datum.loaded = true;
                } else {
                  MAPPED_METRIC_DATA.push({
                    category,
                    fullName: `${category}.${metric}`,
                    loaded: true,
                    metric: undefined,
                    name: metric,
                    storedValue: curr[key],
                    type,
                    value: undefined,
                  });
                }

                return acc;
              }, {}),
            };
          }, {});

          const allEvents = (SUBMITTED_PING.payload.events ?? []).reduce(
            (acc, curr) => {
              let split = curr.category.split(".");
              const metric = curr.name.replace(CAMEL_CASE_REGEX, (_, char) =>
                char.toUpperCase()
              );
              const category = split
                .map((v, i) =>
                  i === 0
                    ? v.replace(CAMEL_CASE_REGEX, (_, char) =>
                        char.toUpperCase()
                      )
                    : v
                        .replace(INITIAL_CASE_REGEX, char => char.toUpperCase())
                        .replace(CAMEL_CASE_REGEX, (_, char) =>
                          char.toUpperCase()
                        )
                )
                .join("");
              if (acc[`${category}.${metric}`]) {
                acc[`${category}.${metric}`].push(curr);
              } else {
                acc[`${category}.${metric}`] = [curr];
              }
              return acc;
            },
            {}
          );
          Object.entries(allEvents).forEach(([key, value]) => {
            const [category, metric] = key.split(".");
            const datum = MAPPED_METRIC_DATA.find(
              d => d.category === category && d.name === metric
            );
            if (datum) {
              datum.storedValue = value;
              datum.loaded = true;
            } else {
              MAPPED_METRIC_DATA.push({
                category,
                fullName: `${category}.${metric}`,
                loaded: true,
                metric: undefined,
                name: metric,
                storedValue: value,
                type: "Event",
                value: undefined,
              });
            }
          });
          SUBMITTED_PING.payload.allMetrics = {
            ...allMetrics,
            ...allEvents,
          };
          document.getElementById("submitted-ping-info").hidden = false;
          document.getElementById("submitted-ping-client-info").textContent =
            prettyPrint(SUBMITTED_PING.payload.client_info);
          document.getElementById("submitted-ping-ping-info").textContent =
            prettyPrint(SUBMITTED_PING.payload.ping_info);
        } else {
          SUBMITTED_PING = undefined;
          MAPPED_METRIC_DATA.forEach(datum => {
            datum.storedValue = undefined;
          });
          document.getElementById("submitted-ping-info").hidden = true;
        }
      } else {
        SUBMITTED_PING = undefined;
        MAPPED_METRIC_DATA.forEach(datum => {
          datum.storedValue = undefined;
        });
        document.getElementById("submitted-ping-info").hidden = true;
      }
      updateFilteredMetricData(
        document.getElementById("filter-metrics").value.toLowerCase()
      );
    });
  }
}

function getTypeFromPingKey(key) {
  let type = "";
  switch (key) {
    case "boolean":
      type = "Boolean";
      break;
    case "counter":
      type = "Counter";
      break;
    case "custom_distribution":
      type = "CustomDistribution";
      break;
    case "datetime":
      type = "Datetime";
      break;
    case "dual_labeled_counter":
      type = "DualLabeledCounter";
      break;
    case "labeled_boolean":
      type = "LabeledBoolean";
      break;
    case "labeled_counter":
      type = "LabeledCounter";
      break;
    case "labeled_custom_distribution":
      type = "LabeledCustomDistribution";
      break;
    case "labeled_memory_distribution":
      type = "LabeledMemoryDistribution";
      break;
    case "labeled_quantity":
      type = "LabeledQuantity";
      break;
    case "labeled_timing_distribution":
      type = "LabeledTimingDistribution";
      break;
    case "memory_distribution":
      type = "MemoryDistribution";
      break;
    case "object":
      type = "Object";
      break;
    case "quantity":
      type = "Quantity";
      break;
    case "rate":
      type = "Rate";
      break;
    case "string":
      type = "String";
      break;
    case "string_list":
      type = "StringList";
      break;
    case "timespan":
      type = "Timespan";
      break;
    case "timing_distribution":
      type = "TimingDistribution";
      break;
    case "url":
      type = "Url";
      break;
    case "uuid":
      type = "Uuid";
      break;
  }
  return type;
}

/**
 * Initializes the base level metric data.
 *
 * Should only be able to be called once.
 */
function initializeMetricData() {
  if (METRIC_DATA_INITIALIZED) {
    return;
  }
  for (let [category, metrics] of Object.entries(Glean)) {
    for (let [metricName, metric] of Object.entries(metrics)) {
      // Trim "Glean" from the constructor names (e.g. "GleanBoolean" -> "Boolean").
      let constructorName = metric.constructor.name.replace("Glean", "");
      // For labeled metrics, get their submetrics' constructor names and append it
      if (constructorName == "Labeled") {
        constructorName += metric.__other__.constructor.name.replace(
          "Glean",
          ""
        );
      }
      if (!METRIC_DATA[category]) {
        METRIC_DATA[category] = {};
      }
      METRIC_DATA[category][metricName] = {
        type: constructorName,
        value: undefined,
        storedValue: undefined,
        metric,
      };
    }
  }
  METRIC_DATA_INITIALIZED = true;
}

function updateButtonsSelection(selection) {
  selection.attr("data-l10n-id", d =>
    d.watching ? "about-glean-button-unwatch" : "about-glean-button-watch"
  );
}

function createOrUpdateLabeledHistogram(selection, datum) {
  const keysAndValues = Object.entries(
    (datum.storedValue ?? datum.value) || {}
  ).map(([key, value]) => [
    key,
    {
      fullName: datum.fullName + `.${key}`,
      value,
    },
  ]);

  selection.selectAll("*").remove();
  if (keysAndValues.length === 0) {
    selection
      .append("p")
      .attr("data-l10n-id", "about-glean-no-data-to-display");
    return;
  }
  for (let entry of keysAndValues.toSorted((a, b) =>
    d3.ascending(a[1].fullName, b[1].fullName)
  )) {
    const div = selection
      .append("div")
      .classed({ "keyed-chart": true })
      .attr("id", `${entry[0]}-container`)
      .attr("data-d3-key", entry[0]);
    div.append("h2").text(entry[0]);
    const svgContainer = div.append("div");
    createOrUpdateHistogram(svgContainer, entry[1]);
  }
}

function createOrUpdateHistogram(selection, datum) {
  const values = Object.entries(
    (datum.storedValue ?? datum.value)?.values || {}
  ).map((d, i) => [...d, i]);

  if (!values || values.length === 0) {
    selection.select("p")?.remove();
    selection
      .append("p")
      .attr("data-l10n-id", "about-glean-no-data-to-display");
    selection.select("svg")?.remove();
    return;
  }
  selection.select("p")?.remove();

  const chartSettings = {
    boxPadding: 5,
    chartMax: 150,
    leftPadding: 20,
    chartPadding: 50,
    scaledMax: 110,
    ...metricsTableSettings.histograms,
  };
  const max = values.map(d => d[1]).sort((a, b) => b - a)[0],
    keyMax = values.map(d => d[0]).sort((a, b) => b - a)[0],
    boxWidth = Math.max(`${Math.max(max, keyMax)}`.length * 10, 30),
    denom = max / chartSettings.scaledMax;

  let hist = selection.select(`svg[data-d3-datum='${datum.fullName}']`);
  if (hist.empty()) {
    hist = selection
      .append("svg")
      .classed({ histogram: true })
      .attr("data-d3-datum", datum.fullName)
      .attr(
        "width",
        values.length * (boxWidth + chartSettings.boxPadding) +
          chartSettings.chartPadding +
          chartSettings.leftPadding
      )
      .attr("height", chartSettings.chartMax + chartSettings.chartPadding);
  }

  let boxesContainer = hist.select("g.boxes");
  if (boxesContainer.empty()) {
    boxesContainer = hist.append("g").classed({ boxes: true });
  }

  const boxes = boxesContainer.selectAll("g").data(values, d => d);

  const newBoxes = boxes.enter().append("g").attr("tabindex", 0);
  newBoxes.append("rect").attr("width", boxWidth);
  newBoxes.append("text").attr("data-d3-role", "y");
  newBoxes.append("text").attr("data-d3-role", "x");

  const xFn = index =>
    boxWidth * index +
    chartSettings.boxPadding * index +
    chartSettings.leftPadding;
  const yFn = yv =>
    Math.abs(Math.max(yv / denom, 1) - chartSettings.scaledMax) +
    (chartSettings.chartMax - chartSettings.scaledMax);

  boxes
    .selectAll("rect")
    .attr("height", d => Math.max(d[1] / denom, 1))
    .attr("x", (_, __, i) => xFn(i))
    .attr("y", d => yFn(d[1]));
  boxes
    .selectAll("text[data-d3-role=y]")
    .attr("x", (_, __, i) => xFn(i))
    .attr("y", d => yFn(d[1]) - 5)
    .text(d => d[1]);
  boxes
    .selectAll("text[data-d3-role=x]")
    .attr("x", d => xFn(d[2]))
    .attr("y", chartSettings.chartMax + 20)
    .text(d => d[0]);

  function focusStart(_) {
    this.classList.add("hovered");
  }

  function focusEnd(_) {
    this.classList.remove("hovered");
  }

  boxes
    .attr("data-d3-box", d => d[0])
    .on("focusin", focusStart)
    .on("mouseover", focusStart)
    .on("focusout", focusEnd)
    .on("mouseout", focusEnd);

  boxes.exit().remove();
}

function createOrUpdateEventChart(selection, datum) {
  const values = ((datum.storedValue ?? datum.value) || []).map((d, i) => ({
    ...d,
    index: i,
    fullName: datum.fullName,
  }));

  if (!values || values.length === 0) {
    selection.select("p")?.remove();
    selection
      .append("p")
      .attr("data-l10n-id", "about-glean-no-data-to-display");
    selection.select("svg")?.remove();
    return;
  }
  selection.select("p")?.remove();

  const chartSettings = {
    height: 75,
    width: 500,
    chartPadding: 50,
    circleRadius: 6,
    verticalLineXOffset: 10,
    verticalLineYOffset: 10,
    ...metricsTableSettings.timelines,
  };
  const max = values.map(d => d.timestamp).sort((a, b) => b - a)[0],
    min = values.map(d => d.timestamp).sort((a, b) => a - b)[0];

  let diagram = selection.select(`svg[data-d3-datum='${datum.fullName}']`);
  if (diagram.empty()) {
    diagram = selection
      .append("svg")
      .attr("data-d3-datum", datum.fullName)
      .classed({ timeline: true });
  }
  diagram
    .attr("width", chartSettings.width)
    .attr("height", chartSettings.height);

  let lineAcross = diagram.select("line[data-d3-role='across']");
  if (lineAcross.empty()) {
    lineAcross = diagram.append("line").attr("data-d3-role", "across");
  }
  lineAcross
    .attr("x1", chartSettings.chartPadding)
    .attr("y1", chartSettings.height / 2)
    .attr("x2", chartSettings.width - chartSettings.chartPadding)
    .attr("y2", chartSettings.height / 2);

  let leftLineThrough = diagram.select("line[data-d3-role='left-through']");
  if (leftLineThrough.empty()) {
    leftLineThrough = diagram
      .append("line")
      .attr("data-d3-role", "left-through");
  }
  leftLineThrough
    .attr("x1", chartSettings.chartPadding + chartSettings.verticalLineXOffset)
    .attr("y1", chartSettings.height / 2 - chartSettings.verticalLineYOffset)
    .attr("x2", chartSettings.chartPadding + chartSettings.verticalLineXOffset)
    .attr("y2", chartSettings.height / 2 + chartSettings.verticalLineYOffset);

  let rightLineThrough = diagram.select("line[data-d3-role='right-through']");
  if (rightLineThrough.empty()) {
    rightLineThrough = diagram
      .append("line")
      .attr("data-d3-role", "right-through");
  }
  rightLineThrough
    .attr(
      "x1",
      chartSettings.width -
        chartSettings.chartPadding -
        chartSettings.verticalLineXOffset
    )
    .attr("y1", chartSettings.height / 2 - chartSettings.verticalLineYOffset)
    .attr(
      "x2",
      chartSettings.width -
        chartSettings.chartPadding -
        chartSettings.verticalLineXOffset
    )
    .attr("y2", chartSettings.height / 2 + chartSettings.verticalLineYOffset);

  let code = selection.select("pre");
  if (code.empty()) {
    code = selection.append("pre").classed({ withChart: true }).append("code");
  } else {
    code = code.select("code");
  }

  const xFn = d3.scale
    .linear()
    .domain([min, max])
    .range([
      chartSettings.verticalLineXOffset + chartSettings.chartPadding,
      chartSettings.width -
        chartSettings.chartPadding -
        chartSettings.verticalLineXOffset,
    ]);

  let eventsContainer = diagram.select("g.events");
  if (eventsContainer.empty()) {
    eventsContainer = diagram.append("g").classed({ events: true });
  }

  const events = eventsContainer
    .selectAll("g.event")
    .data(values, d => `${d.fullName}-${d.index}-${d.timestamp}`);

  const newEvents = events
    .enter()
    .append("g")
    .classed({ event: true })
    .attr("tabindex", 0);

  newEvents.append("circle");

  function focusStart(_) {
    this.classList.add("hovered");
  }

  function focusEnd(_) {
    this.classList.remove("hovered");
  }

  function select(_) {
    const dataPoint = this.__data__;
    code.text(prettyPrint(dataPoint.extra));

    document.querySelectorAll("g.event.selected").forEach(el => {
      el.classList.remove("selected");
      el.querySelector("text")?.remove();
    });
    this.classList.add("selected");

    const text = this.appendChild(
      document.createElementNS("http://www.w3.org/2000/svg", "text")
    );
    text.setAttribute("y", chartSettings.height / 2 + 25);
    text.setAttribute(
      "x",
      xFn(dataPoint.timestamp) - `${dataPoint.timestamp}`.length * 4.5
    );
    text.textContent = dataPoint.timestamp;
  }

  events.attr("data-d3-datum", d => `${d.fullName}-${d.index}-${d.timestamp}`);

  events
    .selectAll("circle")
    .attr("cy", chartSettings.height / 2)
    .attr("cx", d => xFn(d.timestamp))
    .attr("r", chartSettings.circleRadius);

  events
    .on("focusin", select)
    .on("mouseover", focusStart)
    .on("focusout", focusEnd)
    .on("mouseout", focusEnd);

  events.exit().remove();
}

const METRICS_TABLE_SETTINGS_KEY = "about-glean-metrics-table-settings";
/**
 * When adding new fields to the metrics table settings,
 * a corresponding element matching the query selector
 * `[data-form-control=<key>]` must be present in the DOM.
 */
const metricsTableSettings = {
  hideEmptyValueRows: false,
  histograms: {
    boxPadding: 5,
    chartMax: 150,
    leftPadding: 20,
    chartPadding: 50,
    scaledMax: 110,
  },
  timelines: {
    height: 75,
    width: 500,
    chartPadding: 50,
    circleRadius: 6,
    verticalLineXOffset: 10,
    verticalLineYOffset: 10,
  },
};

function initMetricsSettings() {
  const handleSetting = (obj, key, parent) => {
    let element = parent.querySelector(`[data-form-control='${key}']`);
    let valueFn = e => e.target.value;
    if (!element && typeof obj[key] !== "object") {
      console.error(
        new Error(
          `Unable to find form control with key '${key}' in the parent element`
        ),
        parent
      );
      return;
    }
    switch (typeof obj[key]) {
      case "boolean":
        valueFn = e => e.target.checked;
        obj[key] = valueFn({ target: element });
        break;
      case "object":
        element = parent.querySelector(`[data-form-group='${key}']`);
        if (!element) {
          console.error(
            new Error(
              `Unable to find form control with key '${key}' in the parent element`
            ),
            parent
          );
          return;
        }
        for (const subKey of Object.keys(obj[key])) {
          handleSetting(obj[key], subKey, element);
        }
        break;
      case "number":
        valueFn = e => parseInt(e.target.value);
      // eslint-disable-next-line no-fallthrough
      default:
        if (element.type !== typeof obj[key]) {
          console.warn(
            new Error(
              `Form control input type does not match JavaScript value type ${typeof obj[key]}`
            )
          );
        }
        if (valueFn({ target: element })) {
          obj[key] = valueFn({ target: element });
        } else {
          element.value = obj[key];
        }
    }
    element.addEventListener("input", handleSettingChange(obj, valueFn));
  };

  for (const key of Object.keys(metricsTableSettings)) {
    handleSetting(
      metricsTableSettings,
      key,
      document.getElementById("metrics-table-settings")
    );
  }
}

function handleSettingChange(obj, valueFn) {
  return e => {
    obj[e.target.getAttribute("data-form-control")] = valueFn(e);
    settingsChanged();
  };
}

function settingsChanged() {
  createOrUpdateHistogram(
    d3.select("#metrics-table-settings-histogram-example"),
    {
      fullName: "histogram-example",
      value: {
        values: {
          0: 1,
          1: 5,
          2: 4,
          3: 0,
        },
      },
    }
  );

  createOrUpdateEventChart(
    d3.select("#metrics-table-settings-timeline-example"),
    {
      fullName: "timeline-example",
      value: [
        {
          timestamp: 0,
          extra: {
            value: 1,
          },
        },
        {
          timestamp: 1,
          extra: {
            value: 2,
          },
        },
        {
          timestamp: 4,
          extra: {
            value: 3,
          },
        },
        {
          timestamp: 8,
          extra: {
            value: 4,
          },
        },
      ],
    }
  );

  updateTable();
}

function updateValueSelection(selection) {
  // Set the `data-l10n-id` attribute to the appropriate warning if the value is invalid, otherwise
  // unset it by returning `null`.
  selection
    ?.attr("data-l10n-id", d => {
      switch (d.invalidValue) {
        case INVALID_VALUE_REASONS.DUAL_LABELED_METRIC:
          return "about-glean-dual-labeled-metric-warning";
        case INVALID_VALUE_REASONS.UNKNOWN_METRIC:
          return "about-glean-unknown-metric-type-warning";
        default:
          return null;
      }
    })
    ?.each(function (datum) {
      if (datum.loaded) {
        let codeSelection = d3.select(this).select("pre>code");
        switch (datum.type) {
          case "Event":
            createOrUpdateEventChart(d3.select(this), datum);
            break;
          case "CustomDistribution":
          case "MemoryDistribution":
          case "TimingDistribution":
            createOrUpdateHistogram(d3.select(this), datum);
            break;
          case "LabeledCustomDistribution":
          case "LabeledMemoryDistribution":
          case "LabeledTimingDistribution":
            createOrUpdateLabeledHistogram(d3.select(this), datum);
            break;
          default:
            if (codeSelection.empty()) {
              codeSelection = d3.select(this).append("pre").append("code");
            }
            codeSelection.text(prettyPrint(datum.storedValue ?? datum.value));
        }
      }
    });
}

/**
 * Updates a datum object with its value from `testGetValue`.
 *
 * @param {*} datum the datum object to update
 * @param {*} update update the table after updating the datum (defaults to `true`)
 */
function updateDatum(datum) {
  if (typeof datum.metric.testGetValue == "function") {
    try {
      datum.value = datum.metric.testGetValue();
      datum.error = undefined;
    } catch (e) {
      datum.error = e;
    }
    datum.loaded = true;
    datum.invalidValue = undefined;
  } else if (datum.type.includes("DualLabeled")) {
    datum.invalidValue = INVALID_VALUE_REASONS.DUAL_LABELED_METRIC;
  } else {
    datum.invalidValue = INVALID_VALUE_REASONS.UNKNOWN_METRIC;
  }

  updateValueSelection(
    DOCUMENT_BODY_SEL.select(
      `tr[data-d3-row="${datum.fullName}"]>td[data-d3-cell=value]`
    )
  );
}

/**
 * Prettifies a JSON value to make it render more nicely in the table.
 *
 * @param {*} jsonValue the JSON value to prettify
 * @returns a string containing the prettified JSON value in a pre+code
 */
function prettyPrint(jsonValue) {
  if (typeof jsonValue == "object") {
    jsonValue = Object.keys(jsonValue ?? {})
      .sort()
      .reduce((obj, key) => {
        obj[key] = jsonValue[key];
        return obj;
      }, {});
  }
  // from devtools/client/jsonview/json-viewer.mjs
  const pretty = JSON.stringify(
    jsonValue,
    (key, value) => {
      if (value?.type === Symbol("JSON_NUMBER")) {
        return JSON.rawJSON(value.source);
      }

      // By default, -0 will be stringified as `0`, so we need to handle it
      if (Object.is(value, -0)) {
        return JSON.rawJSON("-0");
      }

      return value;
    },
    "  "
  );
  return pretty;
}

function getDocsURL(datum) {
  const upperRegExp = /[A-Z]/;
  const app = "firefox_desktop";
  let category = datum.category;
  let index = category.search(upperRegExp);
  while (index != -1) {
    category = category.replace(
      upperRegExp,
      "_" + category[index].toLowerCase()
    );
    index = category.search(upperRegExp);
  }

  let name = datum.name;
  index = name.search(upperRegExp);
  while (index != -1) {
    name = name.replace(upperRegExp, "_" + name[index].toLowerCase());
    index = name.search(upperRegExp);
  }
  return `https://dictionary.telemetry.mozilla.org/apps/${app}/metrics/${category}_${name}`;
}

/**
 * Updates the `about:glean` metrics table body based on the data points in FILTERED_METRIC_DATA.
 */
function updateTable() {
  LIMITED_METRIC_DATA = FILTERED_METRIC_DATA.toSorted((a, b) =>
    d3.ascending(a.fullName, b.fullName)
  )
    // Filter out rows whose datum elements have either a) been loaded and have a value, or b) have not yet been loaded.
    .filter(d =>
      metricsTableSettings.hideEmptyValueRows
        ? (d.value !== undefined && d.value !== null) || !d.loaded
        : true
    )
    // Filter down to only the datum elements whose indexes fall in the limit offset+count.
    .filter((_, i) => i >= LIMIT_OFFSET && i < LIMIT_COUNT + LIMIT_OFFSET);

  // Let's talk about d3.js
  // `d3.select` is a rough equivalent to `document.querySelector`, but the resulting object(s) are things d3 knows how to manipulate.
  const tbody = DOCUMENT_BODY_SEL.select("#metrics-table-body");
  // Select all the `tr` elements within the previously selected `tbody` element.
  const rows = tbody
    .selectAll("tr")
    // Set the data for the `tr` elements to be the FILTERED_METRIC_DATA, keyed off the data element's full name
    .data(LIMITED_METRIC_DATA, d => d.fullName);

  // `.enter()` means this section determines how we handle new data elements in the array.
  // We class them and insert the appropriate data cells
  let newRows = rows
    .enter()
    .append("tr")
    .attr("data-d3-row", d => d.fullName)
    .classed({ "metric-row": true });

  // Set the HTML content for the `category` and `name` cells, and store the name cells in-scope so we can
  // append our buttons to them.
  newRows
    .append("td")
    .attr("data-d3-cell", "category")
    .append("pre")
    .text(d => d.category);
  newRows
    .append("td")
    .attr("data-d3-cell", "name")
    .append("pre")
    .text(d => d.name);
  // Handle displaying the metric type.
  newRows
    .append("td")
    .attr("data-d3-cell", "type")
    .text(d => d.type);
  const actions = newRows
    .append("td")
    .attr("data-d3-cell", "actions")
    .append("div");
  newRows.append("td").attr("data-d3-cell", "value");

  actions
    .append("button")
    .attr("data-l10n-id", "about-glean-button-dictionary-link")
    // On click, rewrite the metric category+name to snake-case, so we can link to the Glean dictionary.
    // TODO: add canonical_name field to metrics https://bugzilla.mozilla.org/show_bug.cgi?id=1983630
    .on("click", datum => {
      const url = getDocsURL(datum);
      window.open(url, "_blank").focus();
    });
  actions
    .append("button")
    .attr("data-l10n-id", "about-glean-button-load-value")
    .on("click", datum => updateDatum(datum));

  // Since `.enter` has been called on `rows` and we've handled new data points, everything
  // that touches `rows` from here on out will affect ALL elements, old and new.

  updateButtonsSelection(
    rows.selectAll("td[data-d3-cell=actions] button[data-d3-button=watch]")
  );
  // Handle the metric's value.
  updateValueSelection(rows.selectAll("td[data-d3-cell=value]"));

  // Sort the `tr` elements by full metric category+name.
  rows.sort((a, b) => d3.ascending(a.fullName, b.fullName));

  // Handle exiting data points by removing their elements.
  rows.exit().remove();

  // Manually trigger translation on the table, as DOM updates after the first application of the `data-l10n-id` will not translate.
  document.l10n.translateFragment(
    document.querySelector("#metrics-table-body")
  );
}

/**
 * Updates the FILTERED_METRIC_DATA value based on the provided `searchString`.
 *
 * @param {*} searchString the string by which the metric data will be filtered
 */
function updateFilteredMetricData(searchString) {
  if (!searchString && !SUBMITTED_PING) {
    FILTERED_METRIC_DATA = MAPPED_METRIC_DATA;
  } else {
    const simpleTypeValueSearch = datum => {
      if (!Object.values(SIMPLE_TYPES).includes(datum.type)) {
        return false;
      }
      switch (datum.type) {
        case SIMPLE_TYPES.Boolean:
          if (searchString == "true") {
            return (datum.storedValue ?? datum.value) === true;
          } else if (searchString == "false") {
            return (datum.storedValue ?? datum.value) === false;
          }
          return false;
        default:
          return false;
      }
    };

    const submittedPingIncludesMetric = datum =>
      Object.keys(SUBMITTED_PING.payload.allMetrics).find(
        metric => metric === `${datum.category}.${datum.name}`
      ) !== undefined;

    FILTERED_METRIC_DATA = MAPPED_METRIC_DATA.filter(
      datum =>
        (datum.category.toLowerCase().includes(searchString) ||
          datum.name.toLowerCase().includes(searchString) ||
          datum.type.toLowerCase().includes(searchString) ||
          simpleTypeValueSearch(datum)) &&
        (SUBMITTED_PING === undefined || submittedPingIncludesMetric(datum))
    );
  }

  if (FILTERED_METRIC_DATA.length > LIMIT_COUNT + LIMIT_OFFSET) {
    const table = document.getElementById("metrics-table-instance");
    let scrollTimeout,
      scrollTimeoutIsCleared = true;
    table.addEventListener("scroll", el => {
      if (scrollTimeoutIsCleared) {
        scrollTimeout = setTimeout(
          ({ target }) => {
            clearTimeout(scrollTimeout);
            scrollTimeoutIsCleared = true;
            let changes = false;
            if (target.scrollTop < 1500) {
              if (LIMIT_COUNT >= 500 && LIMIT_OFFSET > 0) {
                LIMIT_OFFSET = LIMIT_OFFSET - 100 < 0 ? 0 : LIMIT_OFFSET - 100;
                changes = true;
              }
            } else if (target.scrollHeight - target.scrollTop < 1500) {
              if (LIMIT_COUNT >= 500) {
                if (
                  LIMIT_OFFSET + LIMIT_COUNT + 100 >
                  FILTERED_METRIC_DATA.length
                ) {
                  LIMIT_OFFSET = Math.max(
                    0,
                    FILTERED_METRIC_DATA.length - LIMIT_COUNT
                  );
                } else if (
                  LIMIT_OFFSET + LIMIT_COUNT <
                  FILTERED_METRIC_DATA.length - 100
                ) {
                  LIMIT_OFFSET += 100;
                }
              } else {
                LIMIT_COUNT += 100;
              }
              changes = true;
            }
            if (changes) {
              updateTable();
            }
          },
          10,
          el
        );
        scrollTimeoutIsCleared = false;
      }
    });
  } else {
    LIMIT_COUNT = 200;
    LIMIT_OFFSET = 0;
  }

  updateTable();
}

function exportData() {
  const data = MAPPED_METRIC_DATA.toSorted((a, b) =>
    d3.ascending(a.fullName, b.fullName)
  ).reduce(
    (accumulator, datum) => [
      ...accumulator,
      {
        name: `${datum.fullName}`,
        docs: getDocsURL(datum),
        value: datum.storedValue ?? datum.value,
      },
    ],
    []
  );
  const href = `data:text/json;charset=utf-8,${encodeURIComponent(JSON.stringify(data))}`;
  const anchor = document.createElement("a");
  anchor.setAttribute("href", href);
  anchor.setAttribute(
    "download",
    `about-glean-export-${new Date().toJSON()}.json`
  );
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
}

window.addEventListener("load", onLoad);
