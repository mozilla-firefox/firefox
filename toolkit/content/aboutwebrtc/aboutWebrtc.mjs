/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GraphImpl } from "chrome://global/content/aboutwebrtc/graph.mjs";
import { GraphDb } from "chrome://global/content/aboutwebrtc/graphdb.mjs";
import { Disclosure } from "chrome://global/content/aboutwebrtc/disclosure.mjs";
import { ConfigurationList } from "chrome://global/content/aboutwebrtc/configurationList.mjs";
import { CopyButton } from "./copyButton.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

function makeFilePickerService() {
  const fpContractID = "@mozilla.org/filepicker;1";
  const fpIID = Ci.nsIFilePicker;
  return Cc[fpContractID].createInstance(fpIID);
}

const WGI = WebrtcGlobalInformation;

const LOGFILE_NAME_DEFAULT = "aboutWebrtc.html";

class Renderer {
  // Long function names preserved until code can be uniformly moved to new names
  renderElement(eleName, options, l10n_id, l10n_args) {
    let elem = Object.assign(document.createElement(eleName), options);
    if (l10n_id) {
      document.l10n.setAttributes(elem, l10n_id, l10n_args);
    }
    return elem;
  }
  elem() {
    return this.renderElement(...arguments);
  }
  text(eleName, textContent, options) {
    return this.renderElement(eleName, { textContent, ...options });
  }
  renderElements(eleName, options, list) {
    const element = renderElement(eleName, options);
    element.append(...list);
    return element;
  }
  elems() {
    return this.renderElements(...arguments);
  }
}

// Proxies a Renderer instance to provide some meta programming methods to make
// adding elements more readable, e.g. elemRenderer.elem_h4(...) instead of
// elemRenderer.elem("h4", ...).
const elemRenderer = new Proxy(new Renderer(), {
  get(target, prop) {
    // Function prefixes to proxy.
    const proxied = {
      elem_: (...args) => target.elem(...args),
      elems_: (...args) => target.elems(...args),
      text_: (...args) => target.text(...args),
    };
    for (let [prefix, func] of Object.entries(proxied)) {
      if (prop.startsWith(prefix) && prop.length > prefix.length) {
        return (...args) => func(prop.substring(prefix.length), ...args);
      }
    }
    // Pass non-matches to the base object
    return Reflect.get(...arguments);
  },
});

let graphData = [];
let mostRecentReports = {};
let sdpHistories = [];
let historyTsMemoForPcid = {};
let sdpHistoryTsMemoForPcid = {};

function clearStatsHistory() {
  graphData = [];
  mostRecentReports = {};
  sdpHistories = [];
  historyTsMemoForPcid = {};
  sdpHistoryTsMemoForPcid = {};
}

function appendReportToHistory(report) {
  appendSdpHistory(report);
  mostRecentReports[report.pcid] = report;
  if (graphData[report.pcid] === undefined) {
    graphData[report.pcid] ??= new GraphDb(report);
  } else {
    graphData[report.pcid].insertReportData(report);
  }
}

function appendSdpHistory({ pcid, sdpHistory: newHistory }) {
  sdpHistories[pcid] ??= [];
  let storedHistory = sdpHistories[pcid];
  newHistory.forEach(entry => {
    const { timestamp } = entry;
    if (!storedHistory.length || storedHistory.at(-1).timestamp < timestamp) {
      storedHistory.push(entry);
      sdpHistoryTsMemoForPcid[pcid] = timestamp;
    }
  });
}

function recentStats() {
  return Object.values(mostRecentReports);
}

// Returns the sdpHistory for a given stats report
function getSdpHistory({ pcid, timestamp: a }) {
  sdpHistories[pcid] ??= [];
  return sdpHistories[pcid].filter(({ timestamp: b }) => a >= b);
}

function appendStats(allStats) {
  allStats.forEach(appendReportToHistory);
}

function getAndUpdateStatsTsMemoForPcid(pcid) {
  historyTsMemoForPcid[pcid] = mostRecentReports[pcid]?.timestamp;
  return historyTsMemoForPcid[pcid] || null;
}

function getSdpTsMemoForPcid(pcid) {
  return sdpHistoryTsMemoForPcid[pcid] || null;
}

const REQUEST_FULL_REFRESH = true;
const REQUEST_UPDATE_ONLY_REFRESH = false;

async function getStats(requestFullRefresh) {
  if (
    requestFullRefresh ||
    !Services.prefs.getBoolPref("media.aboutwebrtc.hist.enabled")
  ) {
    // Upon clearing the history we need to get all the stats to rebuild what
    // will become the skeleton of the page.hg wip
    const { reports } = await new Promise(r => WGI.getAllStats(r));
    appendStats(reports);
    return reports.sort((a, b) => b.timestamp - a.timestamp);
  }
  const pcids = await new Promise(r => WGI.getStatsHistoryPcIds(r));
  await Promise.all(
    [...pcids].map(pcid =>
      new Promise(r =>
        WGI.getStatsHistorySince(
          r,
          pcid,
          getAndUpdateStatsTsMemoForPcid(pcid),
          getSdpTsMemoForPcid(pcid)
        )
      ).then(r => {
        appendStats(r.reports);
        r.sdpHistories.forEach(hist => appendSdpHistory(hist));
      })
    )
  );
  let recent = recentStats();
  return recent.sort((a, b) => b.timestamp - a.timestamp);
}

const renderElement = (eleName, options, l10n_id, l10n_args) =>
  elemRenderer.elem(eleName, options, l10n_id, l10n_args);

const renderText = (eleName, textContent, options) =>
  elemRenderer.text(eleName, textContent, options);

const renderElements = (eleName, options, list) =>
  elemRenderer.elems(eleName, options, list);

// Button control classes

class Control {
  label = null;
  message = null;
  messageArgs = null;
  messageHeader = null;

  render() {
    this.ctrl = renderElement(
      "button",
      { onclick: () => this.onClick() },
      this.label
    );
    this.msg = renderElement("p");
    this.update();
    return [this.ctrl, this.msg];
  }

  update() {
    document.l10n.setAttributes(this.ctrl, this.label);
    this.msg.textContent = "";
    if (this.message) {
      this.msg.append(
        renderElement(
          "span",
          {
            className: "info-label",
          },
          this.messageHeader
        ),
        renderElement(
          "span",
          {
            className: "info-body",
          },
          this.message,
          this.messageArgs
        )
      );
    }
  }
}

class SavePage extends Control {
  constructor() {
    super();
    this.messageHeader = "about-webrtc-save-page-label";
    this.label = "about-webrtc-save-page-label";
  }

  async onClick() {
    FoldEffect.expandAll();
    let [dialogTitle] = await document.l10n.formatValues([
      { id: "about-webrtc-save-page-dialog-title" },
    ]);
    let FilePicker = makeFilePickerService();
    const lazyFileUtils = lazy.FileUtils;
    FilePicker.init(window.browsingContext, dialogTitle, FilePicker.modeSave);
    FilePicker.defaultString = LOGFILE_NAME_DEFAULT;
    const rv = await new Promise(r => FilePicker.open(r));
    if (rv != FilePicker.returnOK && rv != FilePicker.returnReplace) {
      return;
    }
    const fout = lazyFileUtils.openAtomicFileOutputStream(
      FilePicker.file,
      lazyFileUtils.MODE_WRONLY | lazyFileUtils.MODE_CREATE
    );
    const content = document.querySelector("#content");
    const noPrintList = [...content.querySelectorAll(".no-print")];
    for (const node of noPrintList) {
      node.style.setProperty("display", "none");
    }
    try {
      fout.write(content.outerHTML, content.outerHTML.length);
    } finally {
      lazyFileUtils.closeAtomicFileOutputStream(fout);
      for (const node of noPrintList) {
        node.style.removeProperty("display");
      }
    }
    this.message = "about-webrtc-save-page-complete-msg";
    this.messageArgs = { path: FilePicker.file.path };
    this.update();
  }
}

class EnableLogging extends Control {
  constructor() {
    super();
    this.label = "about-webrtc-enable-logging-label";
    this.message = null;
  }

  onClick() {
    this.update();
    window.open("about:logging?preset=webrtc");
  }
}

class AecLogging extends Control {
  constructor() {
    super();
    this.messageHeader = "about-webrtc-aec-logging-msg-label";

    if (WGI.aecDebug) {
      this.setState(true);
    } else {
      this.label = "about-webrtc-aec-logging-off-state-label";
      this.message = null;
    }
  }

  setState(state) {
    this.label = state
      ? "about-webrtc-aec-logging-on-state-label"
      : "about-webrtc-aec-logging-off-state-label";
    try {
      if (!state) {
        const file = WGI.aecDebugLogDir;
        this.message = "about-webrtc-aec-logging-toggled-off-state-msg";
        this.messageArgs = { path: file };
      } else {
        this.message = "about-webrtc-aec-logging-toggled-on-state-msg";
      }
    } catch (e) {
      this.message = null;
    }
  }

  onClick() {
    if (Services.env.get("MOZ_DISABLE_CONTENT_SANDBOX") != "1") {
      this.message = "about-webrtc-aec-logging-unavailable-sandbox";
    } else {
      this.setState((WGI.aecDebug = !WGI.aecDebug));
    }
    this.update();
  }
}

class ShowTab extends Control {
  constructor(browserId) {
    super();
    this.label = "about-webrtc-show-tab-label";
    this.message = null;
    this.browserId = browserId;
  }

  onClick() {
    const globalBrowser = window.browsingContext.topChromeWindow.gBrowser;
    for (const tab of globalBrowser.visibleTabs) {
      if (tab.linkedBrowser && tab.linkedBrowser.browserId == this.browserId) {
        globalBrowser.selectedTab = tab;
        return;
      }
    }
    this.ctrl.disabled = true;
  }
}

(async () => {
  // Setup. Retrieve reports & log while page loads.

  const primarySections = [];
  let peerConnections = renderElement("div");
  let connectionLog = renderElement("div");
  let userModifiedConfigView = renderElement("div");

  const content = document.querySelector("#content");
  content.append(peerConnections, connectionLog, userModifiedConfigView);
  await new Promise(r => (window.onload = r));
  {
    const ctrl = renderElement("div", { className: "control" });
    const msg = renderElement("div", { className: "message" });
    const add = ([control, message]) => {
      ctrl.appendChild(control);
      msg.appendChild(message);
    };
    add(new SavePage().render());
    add(new EnableLogging().render());
    add(new AecLogging().render());

    const ctrls = document.querySelector("#controls");
    ctrls.append(renderElements("div", { className: "controls" }, [ctrl, msg]));

    const mediactx = document.querySelector("#mediactx");
    const mediaCtxSection = await renderMediaCtx(elemRenderer);
    primarySections.push(mediaCtxSection);
    mediactx.append(mediaCtxSection.view());
  }

  // This does not handle the auto-refresh, only the manual refreshes needed
  // for certain user actions, and the initial population of the data
  async function refresh() {
    const pcSection = await renderPeerConnectionSection();
    primarySections.push(pcSection);
    const pcDiv = pcSection.view();
    const connectionLogSection = await renderConnectionLog();
    primarySections.push(connectionLogSection);
    const logDiv = connectionLogSection.view();

    // Replace previous info
    peerConnections.replaceWith(pcDiv);
    connectionLog.replaceWith(logDiv);
    const userModifiedConfigSection = await renderUserPrefSection();
    primarySections.push(userModifiedConfigSection);
    userModifiedConfigView.replaceWith(userModifiedConfigSection.view());
    peerConnections = pcDiv;
    connectionLog = logDiv;
  }
  refresh();

  const INTERVAL_MS = 250;
  const HALF_INTERVAL_MS = INTERVAL_MS / 2;
  // This handles autorefresh and forced refresh, not initial document loading
  async function autorefresh() {
    const startTime = performance.now();
    await Promise.all(primarySections.map(s => s.autoUpdate()));
    const elapsed = performance.now() - startTime;
    // Using half the refresh interval as
    const timeout =
      elapsed > HALF_INTERVAL_MS ? INTERVAL_MS : INTERVAL_MS - elapsed;
    return timeout;
  }
  let timeout = INTERVAL_MS;
  while (true) {
    timeout = await autorefresh();
    await new Promise(r => setTimeout(r, timeout));
  }
})();

const peerConnectionAutoRefreshState = {
  /** @type HTMLInputElement */
  primaryCheckbox: undefined,
  /** @type [HTMLInputElement] */
  secondaryCheckboxes: [],

  secondaryClicked() {
    const { checkedBoxes, uncheckedBoxes } = this.secondaryCheckboxes
      .filter(cb => !cb.hidden)
      .reduce(
        (sums, { checked }) => {
          if (checked) {
            sums.checkedBoxes += 1;
          } else {
            sums.uncheckedBoxes += 1;
          }
          return sums;
        },
        {
          checkedBoxes: 0,
          uncheckedBoxes: 0,
        }
      );
    // Stay checked unless all secondary boxes are unchecked
    this.primaryCheckbox.checked = checkedBoxes > 0;
    // Display an indeterminate state when there are both checked and unchecked boxes
    this.primaryCheckbox.indeterminate = checkedBoxes && uncheckedBoxes;
  },
  primaryClicked() {
    for (const cb of this.secondaryCheckboxes.filter(c => !c.hidden)) {
      cb.checked = this.primaryCheckbox.checked;
    }
    this.primaryCheckbox.indeterminate = false;
  },
};

function renderCopyTextToClipboardButton(rndr, id, l10n_id, getTextFn) {
  return rndr.elem_button(
    {
      id: `copytextbutton-${id}`,
      onclick() {
        navigator.clipboard.writeText(getTextFn());
      },
    },
    l10n_id
  );
}

async function renderPeerConnectionSection() {
  // Render pcs and log
  let reports = await getStats();
  let needsFullUpdate = REQUEST_UPDATE_ONLY_REFRESH;
  reports.sort((a, b) => a.browserId - b.browserId);

  // Used by the renderTransportStats function to calculate stat deltas
  const hist = {};

  // Adding a pcid to this list will cause the stats for that list to be refreshed
  // on the next update interval. This is useful for one time refreshes like the
  // "Refresh" button. The list is cleared at the end of each refresh interval.
  const forceRefreshList = [];

  const openPeerConnectionReports = reports.filter(r => !r.closed);
  const closedPeerConnectionReports = reports.filter(r => r.closed);
  const closedPCSection = document.createElement("div");
  if (closedPeerConnectionReports.length) {
    const closedPeerConnectionDisclosure = renderFoldableSection(
      closedPCSection,
      {
        showMsg: "about-webrtc-closed-peerconnection-disclosure-show-msg",
        hideMsg: "about-webrtc-closed-peerconnection-disclosure-hide-msg",
        startsCollapsed: [...openPeerConnectionReports].size,
      }
    );
    closedPCSection.append(closedPeerConnectionDisclosure);
    closedPeerConnectionDisclosure.append(
      ...closedPeerConnectionReports.map(r =>
        renderPeerConnection(r, () => forceRefreshList.push(r.pcid))
      )
    );
  }

  const primarySection = await PrimarySection.make({
    headingL10nId: "about-webrtc-peerconnections-section-heading",
    disclosureShowL10nId: "about-webrtc-peerconnections-section-show-msg",
    disclosureHideL10nId: "about-webrtc-peerconnections-section-hide-msg",
    autoRefreshPref: "media.aboutwebrtc.auto_refresh.peerconnection_section",
    renderFn: async () => {
      const body = document.createElement("div");
      body.append(
        ...openPeerConnectionReports.map(r =>
          renderPeerConnection(r, () => forceRefreshList.push(r.pcid))
        ),
        closedPCSection
      );
      return body;
    },
    // Creates the filling for the disclosure
    updateFn: async () => {
      let statsReports = await getStats(needsFullUpdate);
      needsFullUpdate = REQUEST_UPDATE_ONLY_REFRESH;

      async function translate(element) {
        const frag = document.createDocumentFragment();
        frag.append(element);
        await document.l10n.translateFragment(frag);
        return frag;
      }

      const translateSection = async (report, id, renderFunc) => {
        const element = document.getElementById(`${id}: ${report.pcid}`);
        const result =
          element && (await translate(renderFunc(elemRenderer, report, hist)));
        return { element, translated: result };
      };

      const sections = (
        await Promise.all(
          // Add filter to check the refreshEnabledPcids
          statsReports
            .filter(
              ({ pcid }) =>
                document.getElementById(`autorefresh-${pcid}`)?.checked ||
                forceRefreshList.includes(pcid)
            )
            .flatMap(report => [
              translateSection(
                report,
                "pc-heading",
                renderPeerConnectionHeading
              ),
              translateSection(report, "ice-stats", renderICEStats),
              translateSection(
                report,
                "ice-raw-stats-fold",
                renderRawICEStatsFold
              ),
              translateSection(report, "rtp-stats", renderRTPStats),
              translateSection(report, "sdp-stats", renderSDPStats),
              translateSection(report, "bandwidth-stats", renderBandwidthStats),
              translateSection(report, "frame-stats", renderFrameRateStats),
            ])
        )
      ).filter(({ element }) => element);
      document.l10n.pauseObserving();
      for (const { element, translated } of sections) {
        element.replaceWith(translated);
      }
      document.l10n.resumeObserving();
      while (forceRefreshList.length) {
        forceRefreshList.pop();
      }
    },
    // Updates the contents.
    headerElementsFn: async () => {
      const clearStatsButton = document.createElement("button");
      Object.assign(clearStatsButton, {
        className: "no-print",
        onclick: async () => {
          WGI.clearAllStats();
          clearStatsHistory();
          needsFullUpdate = REQUEST_FULL_REFRESH;
          primarySection.updateFn();
        },
      });
      document.l10n.setAttributes(clearStatsButton, "about-webrtc-stats-clear");
      return [clearStatsButton];
    },
  });
  peerConnectionAutoRefreshState.primaryCheckbox = primarySection.autorefresh;
  let originalOnChange = primarySection.autorefresh.onchange;
  primarySection.autorefresh.onchange = () => {
    originalOnChange();
    peerConnectionAutoRefreshState.primaryClicked();
  };
  return primarySection;
}

function renderSubsectionHeading(l10n_id, copyFunc) {
  const heading = document.createElement("div");
  heading.className = "subsection-heading";
  const h4 = document.createElement("h4");
  if (copyFunc != undefined) {
    const copyButton = new CopyButton(copyFunc);
    h4.appendChild(copyButton.element);
  }
  const text = document.createElement("span");
  document.l10n.setAttributes(text, l10n_id);
  h4.appendChild(text);
  heading.appendChild(h4);
  return heading;
}

function renderPeerConnection(report, forceRefreshFn) {
  const rndr = elemRenderer;
  const { pcid, configuration } = report;
  const pcStats = report.peerConnectionStats[0];

  const pcDiv = renderElement("div", { className: "peer-connection" });
  pcDiv.append(renderPeerConnectionTools(rndr, report, forceRefreshFn));
  {
    const section = renderFoldableSection(pcDiv);
    section.append(
      renderElements("div", {}, [
        renderElement(
          "span",
          {
            className: "info-label",
          },
          "about-webrtc-peerconnection-id-label"
        ),
        renderText("span", pcid, { className: "info-body" }),
        rndr.elems_p({}, [
          rndr.elem_span(
            { className: "info-label" },
            "about-webrtc-data-channels-opened-label"
          ),
          rndr.text_span(pcStats.dataChannelsOpened, {
            className: "info-body",
          }),
        ]),
        rndr.elems_p({}, [
          rndr.elem_span(
            { className: "info-label" },
            "about-webrtc-data-channels-closed-label"
          ),
          rndr.text_span(pcStats.dataChannelsClosed, {
            className: "info-body",
          }),
        ]),
        renderConfiguration(rndr, configuration),
      ]),
      renderRTPStats(rndr, report),
      renderICEStats(rndr, report),
      renderRawICEStats(rndr, report),
      renderSDPStats(rndr, report),
      renderBandwidthStats(rndr, report),
      renderFrameRateStats(rndr, report)
    );
    pcDiv.append(section);
  }
  return pcDiv;
}

function renderPeerConnectionMediaSummary(rndr, report) {
  // Takes a codecId value and returns a corresponding codec stats object
  const getCodecById = aId => report.codecStats.find(({ id }) => id == aId);

  // Find all the codecs used by send streams
  const sendCodecs = new Set(
    [...report.outboundRtpStreamStats]
      .filter(({ codecId }) => codecId)
      .map(({ codecId }) => getCodecById(codecId).mimeType)
      .sort()
  );

  // Find all the codecs used by receive streams
  const recvCodecs = new Set(
    [...report.inboundRtpStreamStats]
      .filter(({ codecId }) => codecId)
      .map(({ codecId }) => getCodecById(codecId).mimeType)
      .sort()
  );

  // Take all the codecs that appear in both the send and receive codec lists
  const sendRecvCodecs = new Set(
    [...sendCodecs, ...recvCodecs].filter(
      c => sendCodecs.has(c) && recvCodecs.has(c)
    )
  );

  // Remove the common codecs from the send and receive codec lists.
  // sendCodecs will now contain send only codecs
  // receiveCodecs will now contain receive only codecs
  sendRecvCodecs.forEach(c => {
    sendCodecs.delete(c);
    recvCodecs.delete(c);
  });

  const formatter = new Intl.ListFormat("en", {
    style: "short",
    type: "conjunction",
  });

  // Create a label with the codecs common to send and receive streams
  const sendRecvSpan = sendRecvCodecs.size
    ? [
        rndr.elem_span({}, "about-webrtc-short-send-receive-direction", {
          codecs: formatter.format(sendRecvCodecs),
        }),
      ]
    : [];

  // Do the same for send only codecs
  const sendSpan = sendCodecs.size
    ? [
        rndr.elem_span({}, "about-webrtc-short-send-direction", {
          codecs: formatter.format(sendCodecs),
        }),
      ]
    : [];

  // Do the same for receive only codecs
  const recvSpan = recvCodecs.size
    ? [
        rndr.elem_span({}, "about-webrtc-short-receive-direction", {
          codecs: formatter.format(recvCodecs),
        }),
      ]
    : [];

  return [...sendRecvSpan, ...sendSpan, ...recvSpan];
}

function renderPeerConnectionHeading(rndr, report) {
  const { pcid, timestamp, closed: isClosed, browserId } = report;
  const id = pcid.match(/id=(\S+)/)[1];
  const url = pcid.match(/url=([^)]+)/)[1];
  const now = new Date(timestamp);
  return isClosed
    ? rndr.elems_div(
        {
          id: `pc-heading: ${pcid}`,
          class: "pc-heading",
        },
        [
          rndr.elems_h3({}, [
            rndr.elem_span({}, "about-webrtc-connection-closed", {
              "browser-id": browserId,
              id,
              url,
              now,
            }),
            ...renderPeerConnectionMediaSummary(rndr, report),
          ]),
        ]
      )
    : rndr.elems_div(
        {
          id: `pc-heading: ${pcid}`,
          class: "pc-heading",
        },
        [
          rndr.elems_h3({}, [
            rndr.elem_span({}, "about-webrtc-connection-open", {
              "browser-id": browserId,
              id,
              url,
              now,
            }),
            ...renderPeerConnectionMediaSummary(rndr, report),
          ]),
        ]
      );
}

function renderPeerConnectionTools(rndr, report, forceRefreshFn) {
  const { pcid, browserId } = report;
  const id = pcid.match(/id=(\S+)/)[1];
  const copyHistButton = !Services.prefs.getBoolPref(
    "media.aboutwebrtc.hist.enabled"
  )
    ? []
    : [
        rndr.elem_button(
          {
            id: `copytextbutton-hist-${id}`,
            onclick() {
              WGI.getStatsHistorySince(
                hist =>
                  navigator.clipboard.writeText(JSON.stringify(hist, null, 2)),
                pcid
              );
            },
          },
          "about-webrtc-copy-report-history-button"
        ),
      ];
  const autorefreshButton = rndr.elem_input({
    id: `autorefresh-${pcid}`,
    className: "autorefresh",
    type: "checkbox",
    hidden: report.closed,
    checked: Services.prefs.getBoolPref(
      "media.aboutwebrtc.auto_refresh.peerconnection_section"
    ),
    onchange: () => peerConnectionAutoRefreshState.secondaryClicked(),
  });
  peerConnectionAutoRefreshState.secondaryCheckboxes.push(autorefreshButton);
  const forceRefreshButton = rndr.elem_button(
    {
      id: `force-refresh-pc-${id}`,
      onclick() {
        forceRefreshFn();
      },
    },
    "about-webrtc-force-refresh-button"
  );
  const autorefreshLabel = rndr.elem_label(
    {
      className: "autorefresh",
      hidden: autorefreshButton.hidden,
    },
    "about-webrtc-auto-refresh-label"
  );
  return renderElements("div", { id: "pc-tools: " + pcid }, [
    renderPeerConnectionHeading(rndr, report),
    new ShowTab(browserId).render()[0],
    renderCopyTextToClipboardButton(
      rndr,
      report.pcid,
      "about-webrtc-copy-report-button",
      () => JSON.stringify({ ...report }, null, 2)
    ),
    ...copyHistButton,
    forceRefreshButton,
    autorefreshButton,
    autorefreshLabel,
  ]);
}

const trimNewlines = sdp => sdp.replaceAll("\r\n", "\n");

const tabElementProps = (element, elemSubId, pcid) => ({
  className:
    elemSubId != "answer"
      ? `tab-${element}`
      : `tab-${element} active-tab-${element}`,
  id: `tab_${element}_${elemSubId}_${pcid}`,
});

const renderSDPTab = (rndr, sdp, props) =>
  rndr.elems("div", props, [rndr.text("pre", trimNewlines(sdp))]);

const renderSDPHistoryTab = (rndr, hist, props) => {
  // All SDP in sequential order. Add onclick handler to scroll the associated
  // SDP into view below.
  let first = Math.min(...hist.map(({ timestamp }) => timestamp));
  const parts = hist.map(({ isLocal, timestamp, sdp, errors: errs }) => {
    let errorsSubSect = () => [
      rndr.elem_h5({}, "about-webrtc-sdp-parsing-errors-heading"),
      ...errs.map(({ lineNumber: n, error: e }) => rndr.text_br(`${n}: ${e}`)),
    ];

    let sdpSection = [
      rndr.elem_h5({}, "about-webrtc-sdp-set-timestamp", {
        timestamp,
        "relative-timestamp": timestamp - first,
      }),
      ...(errs && errs.length ? errorsSubSect() : []),
      rndr.text_pre(trimNewlines(sdp)),
    ];

    return {
      link: rndr.elems_div({}, [
        rndr.elem_h5(
          {
            className: "sdp-history-link",
            onclick: () => sdpSection[0].scrollIntoView(),
          },
          isLocal
            ? "about-webrtc-sdp-set-at-timestamp-local"
            : "about-webrtc-sdp-set-at-timestamp-remote",
          { timestamp }
        ),
      ]),
      ...(isLocal ? { local: sdpSection } : { remote: sdpSection }),
    };
  });

  return rndr.elems_div(props, [
    // Render the links
    rndr.elems_div(
      {},
      parts.map(({ link }) => link)
    ),
    rndr.elems_div({ className: "sdp-history" }, [
      // Render the SDP into separate columns for local and remote.
      rndr.elems_div({}, [
        rndr.elem_h4({}, "about-webrtc-local-sdp-heading"),
        ...parts.filter(({ local }) => local).flatMap(({ local }) => local),
      ]),
      rndr.elems_div({}, [
        rndr.elem_h4({}, "about-webrtc-remote-sdp-heading"),
        ...parts.filter(({ remote }) => remote).flatMap(({ remote }) => remote),
      ]),
    ]),
  ]);
};

function renderSDPStats(rndr, { offerer, pcid, timestamp }) {
  // Get the most recent (as of timestamp) local and remote SDPs from the
  // history
  const sdpEntries = getSdpHistory({ pcid, timestamp });
  const localSdp = sdpEntries.findLast(({ isLocal }) => isLocal)?.sdp || "";
  const remoteSdp = sdpEntries.findLast(({ isLocal }) => !isLocal)?.sdp || "";

  const sdps = offerer
    ? { offer: localSdp, answer: remoteSdp }
    : { offer: remoteSdp, answer: localSdp };

  const sdpLabels = offerer
    ? { offer: "local", answer: "remote" }
    : { offer: "remote", answer: "local" };

  sdpLabels.l10n = {
    offer: offerer
      ? "about-webrtc-local-sdp-heading-offer"
      : "about-webrtc-remote-sdp-heading-offer",
    answer: offerer
      ? "about-webrtc-remote-sdp-heading-answer"
      : "about-webrtc-local-sdp-heading-answer",
    history: "about-webrtc-sdp-history-heading",
  };

  const tabPaneProps = elemSubId => tabElementProps("pane", elemSubId, pcid);

  const panes = {
    answer: renderSDPTab(rndr, sdps.answer, tabPaneProps("answer")),
    offer: renderSDPTab(rndr, sdps.offer, tabPaneProps("offer")),
    history: renderSDPHistoryTab(
      rndr,
      getSdpHistory({ pcid, timestamp }),
      tabPaneProps("history")
    ),
  };

  // Creates the properties and l10n label for tab buttons
  const tabButtonProps = (elemSubId, pane) => [
    {
      ...tabElementProps("button", elemSubId, pcid),
      onclick({ currentTarget: t }) {
        const flipPane = c => c.classList.toggle("active-tab-pane", c == pane);
        Object.values(panes).forEach(flipPane);
        const selButton = c => c.classList.toggle("active-tab-button", c == t);
        [...t.parentElement.children].forEach(selButton);
      },
    },
    sdpLabels.l10n[elemSubId],
  ];

  const sdpDiv = renderSubsectionHeading("about-webrtc-sdp-heading", () =>
    JSON.stringify(
      {
        offer: {
          side: sdpLabels.offer,
          sdp: sdps.offer.split("\r\n"),
        },
        answer: {
          side: sdpLabels.answer,
          sdp: sdps.answer.split("\r\n"),
        },
      },
      null,
      2
    )
  );
  const outer = document.createElement("div", { id: "sdp-stats" + pcid });
  outer.appendChild(sdpDiv);
  let foldSection = renderFoldableSection(outer, {
    showMsg: "about-webrtc-show-msg-sdp",
    hideMsg: "about-webrtc-hide-msg-sdp",
  });
  foldSection.append(
    rndr.elems_div({ className: "tab-buttons" }, [
      ...Object.entries(panes).map(([elemSubId, pane]) =>
        rndr.elem_button(...tabButtonProps(elemSubId, pane))
      ),
      ...Object.values(panes),
    ])
  );
  outer.append(foldSection);
  return outer;
}

function renderBandwidthStats(rndr, report) {
  const statsDiv = renderElement("div", {
    id: "bandwidth-stats: " + report.pcid,
  });
  const table = renderSimpleTable(
    "",
    [
      "about-webrtc-track-identifier",
      "about-webrtc-send-bandwidth-bytes-sec",
      "about-webrtc-receive-bandwidth-bytes-sec",
      "about-webrtc-max-padding-bytes-sec",
      "about-webrtc-pacer-delay-ms",
      "about-webrtc-round-trip-time-ms",
    ],
    report.bandwidthEstimations.map(stat => [
      stat.trackIdentifier,
      stat.sendBandwidthBps,
      stat.receiveBandwidthBps,
      stat.maxPaddingBps,
      stat.pacerDelayMs,
      stat.rttMs,
    ])
  );
  statsDiv.append(
    renderElement("h4", {}, "about-webrtc-bandwidth-stats-heading"),
    table
  );
  return statsDiv;
}

function renderFrameRateStats(rndr, report) {
  const statsDiv = renderElement("div", { id: "frame-stats: " + report.pcid });
  report.videoFrameHistories.forEach(hist => {
    const stats = hist.entries.map(stat => {
      stat.elapsed = stat.lastFrameTimestamp - stat.firstFrameTimestamp;
      if (stat.elapsed < 1) {
        stat.elapsed = "0.00";
      }
      stat.elapsed = (stat.elapsed / 1_000).toFixed(3);
      if (stat.elapsed && stat.consecutiveFrames) {
        stat.avgFramerate = (stat.consecutiveFrames / stat.elapsed).toFixed(2);
      } else {
        stat.avgFramerate = "0.00";
      }
      return stat;
    });

    const table = renderSimpleTable(
      "",
      [
        "about-webrtc-width-px",
        "about-webrtc-height-px",
        "about-webrtc-consecutive-frames",
        "about-webrtc-time-elapsed",
        "about-webrtc-estimated-framerate",
        "about-webrtc-rotation-degrees",
        "about-webrtc-first-frame-timestamp",
        "about-webrtc-last-frame-timestamp",
        "about-webrtc-local-receive-ssrc",
        "about-webrtc-remote-send-ssrc",
      ],
      stats.map(stat =>
        [
          stat.width,
          stat.height,
          stat.consecutiveFrames,
          stat.elapsed,
          stat.avgFramerate,
          stat.rotationAngle,
          stat.firstFrameTimestamp,
          stat.lastFrameTimestamp,
          stat.localSsrc,
          stat.remoteSsrc || "?",
        ].map(entry => (Object.is(entry, undefined) ? "<<undefined>>" : entry))
      )
    );

    statsDiv.append(
      renderElement("h4", {}, "about-webrtc-frame-stats-heading", {
        "track-identifier": hist.trackIdentifier,
      }),
      table
    );
  });

  return statsDiv;
}

function renderRTPStats(rndr, report, hist) {
  const rtpStats = [
    ...(report.inboundRtpStreamStats || []),
    ...(report.outboundRtpStreamStats || []),
  ];
  const remoteRtpStats = [
    ...(report.remoteInboundRtpStreamStats || []),
    ...(report.remoteOutboundRtpStreamStats || []),
  ];

  // Generate an id-to-streamStat index for each remote streamStat. This will
  // be used next to link the remote to its local side.
  const remoteRtpStatsMap = {};
  for (const stat of remoteRtpStats) {
    remoteRtpStatsMap[stat.id] = stat;
  }

  // If a streamStat has a remoteId attribute, create a remoteRtpStats
  // attribute that references the remote streamStat entry directly.
  // That is, the index generated above is merged into the returned list.
  for (const stat of rtpStats.filter(s => "remoteId" in s)) {
    stat.remoteRtpStats = remoteRtpStatsMap[stat.remoteId];
  }
  for (const stat of rtpStats.filter(s => "codecId" in s)) {
    stat.codecStat = report.codecStats.find(({ id }) => id == stat.codecId);
  }
  const graphsByStat = stat =>
    (graphData[report.pcid]?.getGraphDataById(stat.id) || []).map(gd => {
      // For some (remote) graphs data comes in slowly.
      // Those graphs can be larger to show trends.
      const histSecs = gd.getConfig().histSecs;
      const width = (histSecs > 30 ? histSecs / 3 : 15) * 20;
      const height = 100;
      const graph = new GraphImpl(width, height);
      graph.startTime = () => stat.timestamp - histSecs * 1000;
      graph.stopTime = () => stat.timestamp;
      if (gd.subKey == "packetsLost") {
        const oldMaxColor = graph.maxColor;
        graph.maxColor = data => (data.value == 0 ? "red" : oldMaxColor(data));
      }
      // Get a bit more history for averages (20%)
      const dataSet = gd.getDataSetSince(
        graph.startTime() - histSecs * 0.2 * 1000
      );
      return graph.drawSparseValues(dataSet, gd.subKey, gd.getConfig());
    });
  // Render stats set
  return renderElements(
    "div",
    { id: "rtp-stats: " + report.pcid, className: "rtp-stats" },
    [
      renderSubsectionHeading("about-webrtc-rtp-stats-heading", () =>
        JSON.stringify([...rtpStats, ...remoteRtpStats], null, 2)
      ),
      ...rtpStats.map(stat => {
        const { ssrc, remoteId, remoteRtpStats: rtcpStats } = stat;
        const remoteGraphs = rtcpStats
          ? [
              rndr.elems_div({}, [
                rndr.text_h6(rtcpStats.type),
                ...graphsByStat(rtcpStats),
              ]),
            ]
          : [];
        const mime = stat?.codecStat?.mimeType?.concat(" - ") || "";
        const div = renderElements("div", {}, [
          rndr.text_h5(`${mime}SSRC ${ssrc}`),
          rndr.elems_div({}, [rndr.text_h6(stat.type), ...graphsByStat(stat)]),
          ...remoteGraphs,
          renderCodecStats(stat),
          renderTransportStats(stat, true, hist),
        ]);
        if (remoteId && rtcpStats) {
          div.append(renderTransportStats(rtcpStats, false));
        }
        return div;
      }),
    ]
  );
}

function renderCodecStats({
  codecStat,
  framesEncoded,
  framesDecoded,
  framesDropped,
  discardedPackets,
  packetsReceived,
}) {
  let elements = [];

  if (codecStat) {
    elements.push(
      renderText("span", `${codecStat.payloadType} ${codecStat.mimeType}`, {})
    );
    if (framesEncoded !== undefined || framesDecoded !== undefined) {
      elements.push(
        renderElement(
          "span",
          { className: "stat-label" },
          "about-webrtc-frames",
          {
            frames: framesEncoded || framesDecoded || 0,
          }
        )
      );
    }
    if (codecStat.channels !== undefined) {
      elements.push(
        renderElement(
          "span",
          { className: "stat-label" },
          "about-webrtc-channels",
          {
            channels: codecStat.channels,
          }
        )
      );
    }
    elements.push(
      renderText(
        "span",
        ` ${codecStat.clockRate} ${codecStat.sdpFmtpLine || ""}`,
        {}
      )
    );
  }
  if (framesDropped !== undefined) {
    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-dropped-frames-label"
      )
    );
    elements.push(renderText("span", ` ${framesDropped}`, {}));
  }
  if (discardedPackets !== undefined) {
    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-discarded-packets-label"
      )
    );
    elements.push(renderText("span", ` ${discardedPackets}`, {}));
  }
  if (elements.length) {
    if (packetsReceived !== undefined) {
      elements.unshift(
        renderElement("span", {}, "about-webrtc-decoder-label"),
        renderText("span", ": ")
      );
    } else {
      elements.unshift(
        renderElement("span", {}, "about-webrtc-encoder-label"),
        renderText("span", ": ")
      );
    }
  }
  return renderElements("div", {}, elements);
}

function renderTransportStats(
  {
    id,
    timestamp,
    type,
    packetsReceived,
    bytesReceived,
    packetsLost,
    jitter,
    roundTripTime,
    packetsSent,
    bytesSent,
  },
  local,
  hist
) {
  if (hist) {
    if (hist[id] === undefined) {
      hist[id] = {};
    }
  }

  const estimateKBps = (curTimestamp, lastTimestamp, bytes, lastBytes) => {
    if (!curTimestamp || !lastTimestamp || !bytes || !lastBytes) {
      return "0.0";
    }
    const elapsedTime = curTimestamp - lastTimestamp;
    if (elapsedTime <= 0) {
      return "0.0";
    }
    return ((bytes - lastBytes) / elapsedTime).toFixed(1);
  };

  let elements = [];

  if (local) {
    elements.push(
      renderElement("span", {}, "about-webrtc-type-local"),
      renderText("span", ": ")
    );
  } else {
    elements.push(
      renderElement("span", {}, "about-webrtc-type-remote"),
      renderText("span", ": ")
    );
  }

  const time = new Date(timestamp).toTimeString();
  elements.push(renderText("span", `${time} ${type}`));

  if (packetsReceived) {
    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-received-label",
        {
          packets: packetsReceived,
        }
      )
    );

    if (bytesReceived) {
      let s = ` (${(bytesReceived / 1024).toFixed(2)} Kb`;
      if (local && hist) {
        s += ` , ${estimateKBps(
          timestamp,
          hist[id].lastTimestamp,
          bytesReceived,
          hist[id].lastBytesReceived
        )} KBps`;
      }
      s += ")";
      elements.push(renderText("span", s));
    }

    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-lost-label",
        {
          packets: packetsLost,
        }
      )
    );
    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-jitter-label",
        {
          jitter,
        }
      )
    );

    if (roundTripTime !== undefined) {
      elements.push(renderText("span", ` RTT: ${roundTripTime * 1000} ms`));
    }
  } else if (packetsSent) {
    elements.push(
      renderElement(
        "span",
        { className: "stat-label" },
        "about-webrtc-sent-label",
        {
          packets: packetsSent,
        }
      )
    );
    if (bytesSent) {
      let s = ` (${(bytesSent / 1024).toFixed(2)} Kb`;
      if (local && hist) {
        s += `, ${estimateKBps(
          timestamp,
          hist[id].lastTimestamp,
          bytesSent,
          hist[id].lastBytesSent
        )} KBps`;
      }
      s += ")";
      elements.push(renderText("span", s));
    }
  }

  // Update history
  if (hist) {
    hist[id].lastBytesReceived = bytesReceived;
    hist[id].lastBytesSent = bytesSent;
    hist[id].lastTimestamp = timestamp;
  }

  return renderElements("div", {}, elements);
}

function renderRawIceTable(caption, candidates) {
  const table = renderSimpleTable(
    "",
    [caption],
    [...new Set(candidates.sort())].filter(i => i).map(i => [i])
  );
  table.className = "raw-candidate";
  return table;
}

function renderConfiguration(rndr, c) {
  const provided = "about-webrtc-configuration-element-provided";
  const notProvided = "about-webrtc-configuration-element-not-provided";

  // Create the text for a configuration field
  const cfg = (obj, key) => [
    renderElement("br"),
    `${key}: `,
    key in obj ? obj[key] : renderElement("i", {}, notProvided),
  ];

  // Create the text for a fooProvided configuration field
  const pro = (obj, key) => [
    renderElement("br"),
    `${key}(`,
    renderElement("i", {}, provided),
    `/`,
    renderElement("i", {}, notProvided),
    `): `,
    renderElement("i", {}, obj[`${key}Provided`] ? provided : notProvided),
  ];

  const confDiv = rndr.elem_div({ display: "contents" });
  let disclosure = renderFoldableSection(confDiv, {
    showMsg: "about-webrtc-pc-configuration-show-msg",
    hideMsg: "about-webrtc-pc-configuration-hide-msg",
  });
  disclosure.append(
    rndr.elems_div({ classList: "peer-connection-config" }, [
      "RTCConfiguration",
      ...cfg(c, "bundlePolicy"),
      ...cfg(c, "iceTransportPolicy"),
      ...pro(c, "peerIdentity"),
      ...cfg(c, "sdpSemantics"),
      renderElement("br"),
      "iceServers: ",
      ...(!c.iceServers
        ? [renderElement("i", {}, notProvided)]
        : c.iceServers.map(i =>
            renderElements("div", {}, [
              `urls: ${JSON.stringify(i.urls)}`,
              ...pro(i, "credential"),
              ...pro(i, "userName"),
            ])
          )),
    ])
  );
  confDiv.append(disclosure);
  return confDiv;
}

function renderICEStats(rndr, report) {
  const iceDiv = renderElements("div", { id: "ice-stats: " + report.pcid }, [
    renderSubsectionHeading("about-webrtc-ice-stats-heading", () =>
      JSON.stringify(
        [...report.iceCandidateStats, ...report.iceCandidatePairStats],
        null,
        2
      )
    ),
  ]);

  // Render ICECandidate table
  {
    const caption = renderElement(
      "caption",
      { className: "no-print" },
      "about-webrtc-trickle-caption-msg"
    );

    // Generate ICE stats
    const stats = [];
    {
      // Create an index based on candidate ID for each element in the
      // iceCandidateStats array.
      const candidates = {};
      for (const candidate of report.iceCandidateStats) {
        candidates[candidate.id] = candidate;
      }

      // a method to see if a given candidate id is in the array of tickled
      // candidates.
      const isTrickled = candidateId =>
        report.trickledIceCandidateStats.some(({ id }) => id == candidateId);

      // A component may have a remote or local candidate address or both.
      // Combine those with both; these will be the peer candidates.
      const matched = {};

      for (const {
        localCandidateId,
        remoteCandidateId,
        componentId,
        state,
        priority,
        nominated,
        selected,
        bytesSent,
        bytesReceived,
      } of report.iceCandidatePairStats) {
        const local = candidates[localCandidateId];
        if (local) {
          const stat = {
            ["local-candidate"]: candidateToString(local),
            componentId,
            state,
            priority,
            nominated,
            selected,
            bytesSent,
            bytesReceived,
          };
          matched[local.id] = true;
          if (isTrickled(local.id)) {
            stat["local-trickled"] = true;
          }

          const remote = candidates[remoteCandidateId];
          if (remote) {
            stat["remote-candidate"] = candidateToString(remote);
            matched[remote.id] = true;
            if (isTrickled(remote.id)) {
              stat["remote-trickled"] = true;
            }
          }
          stats.push(stat);
        }
      }

      // sort (group by) componentId first, then bytesSent if available, else by
      // priority
      stats.sort((a, b) => {
        if (a.componentId != b.componentId) {
          return a.componentId - b.componentId;
        }
        return b.bytesSent
          ? b.bytesSent - (a.bytesSent || 0)
          : (b.priority || 0) - (a.priority || 0);
      });
    }
    // Render ICE stats
    // don't use |stat.x || ""| here because it hides 0 values
    const statsTable = renderSimpleTable(
      caption,
      [
        "about-webrtc-ice-state",
        "about-webrtc-nominated",
        "about-webrtc-selected",
        "about-webrtc-local-candidate",
        "about-webrtc-remote-candidate",
        "about-webrtc-ice-component-id",
        "about-webrtc-priority",
        "about-webrtc-ice-pair-bytes-sent",
        "about-webrtc-ice-pair-bytes-received",
      ],
      stats.map(stat =>
        [
          stat.state,
          stat.nominated,
          stat.selected,
          stat["local-candidate"],
          stat["remote-candidate"],
          stat.componentId,
          stat.priority,
          stat.bytesSent,
          stat.bytesReceived,
        ].map(entry => (Object.is(entry, undefined) ? "" : entry))
      )
    );

    // after rendering the table, we need to change the class name for each
    // candidate pair's local or remote candidate if it was trickled.
    let index = 0;
    for (const {
      state,
      nominated,
      selected,
      "local-trickled": localTrickled,
      "remote-trickled": remoteTrickled,
    } of stats) {
      // look at statsTable row index + 1 to skip column headers
      const { cells } = statsTable.rows[++index];
      cells[0].className = `ice-${state}`;
      if (nominated) {
        cells[1].className = "ice-succeeded";
      }
      if (selected) {
        cells[2].className = "ice-succeeded";
      }
      if (localTrickled) {
        cells[3].className = "ice-trickled";
      }
      if (remoteTrickled) {
        cells[4].className = "ice-trickled";
      }
    }

    // if the current row's component id changes, mark the bottom of the
    // previous row with a thin, black border to differentiate the
    // component id grouping.
    let previousRow;
    for (const row of statsTable.rows) {
      if (previousRow) {
        if (previousRow.cells[5].innerHTML != row.cells[5].innerHTML) {
          previousRow.className = "bottom-border";
        }
      }
      previousRow = row;
    }
    iceDiv.append(statsTable);
  }
  // restart/rollback counts.
  iceDiv.append(
    renderIceMetric("about-webrtc-ice-restart-count-label", report.iceRestarts),
    renderIceMetric(
      "about-webrtc-ice-rollback-count-label",
      report.iceRollbacks
    )
  );
  return iceDiv;
}

function renderRawICEStats(rndr, report) {
  const iceDiv = renderElements("div", { id: "ice-stats: " + report.pcid }, [
    renderSubsectionHeading("about-webrtc-raw-candidates-heading", () =>
      JSON.stringify(
        [...report.rawLocalCandidates, ...report.rawRemoteCandidates],
        null,
        2
      )
    ),
  ]);
  // Render raw ICECandidate section
  {
    const foldSection = renderFoldableSection(iceDiv, {
      showMsg: "about-webrtc-raw-cand-section-show-msg",
      hideMsg: "about-webrtc-raw-cand-section-hide-msg",
    });

    // render raw candidates
    foldSection.append(renderRawICEStatsFold(rndr, report));
    iceDiv.append(foldSection);
  }
  return iceDiv;
}

function renderRawICEStatsFold(rndr, report) {
  return renderElements("div", { id: "ice-raw-stats-fold: " + report.pcid }, [
    renderRawIceTable(
      "about-webrtc-raw-local-candidate",
      report.rawLocalCandidates
    ),
    renderRawIceTable(
      "about-webrtc-raw-remote-candidate",
      report.rawRemoteCandidates
    ),
  ]);
}

function renderIceMetric(label, value) {
  return renderElements("div", {}, [
    renderElement("span", { className: "info-label" }, label),
    renderText("span", value, { className: "info-body" }),
  ]);
}

function candidateToString({
  type,
  address,
  port,
  protocol,
  candidateType,
  relayProtocol,
  proxied,
} = {}) {
  if (!type) {
    return "*";
  }
  if (relayProtocol) {
    candidateType = `${candidateType}-${relayProtocol}`;
  }
  proxied = type == "local-candidate" ? ` [${proxied}]` : "";
  return `${address}:${port}/${protocol}(${candidateType})${proxied}`;
}

async function renderConnectionLog() {
  const getLog = () => new Promise(r => WGI.getLogging("", r));
  const logView = document.createElement("div");
  const displayLogs = logLines => {
    logView.replaceChildren();
    logView.append(
      ...logLines.map(line => {
        const e = document.createElement("p");
        e.textContent = line;
        return e;
      })
    );
  };
  const clearLogsButton = document.createElement("button");

  Object.assign(clearLogsButton, {
    className: "no-print",
    onclick: async () => {
      await WGI.clearLogging();
      displayLogs(await getLog());
    },
  });
  document.l10n.setAttributes(clearLogsButton, "about-webrtc-log-clear");
  return PrimarySection.make({
    headingL10nId: "about-webrtc-log-heading",
    disclosureShowL10nId: "about-webrtc-log-section-show-msg",
    disclosureHideL10nId: "about-webrtc-log-section-hide-msg",
    autoRefreshPref: "media.aboutwebrtc.auto_refresh.connection_log_section",
    renderFn: async () => {
      displayLogs(await getLog());
      return logView;
    },
    updateFn: async () => {
      displayLogs(await getLog());
    },
    headerElementsFn: async () => [clearLogsButton],
  });
}

const PREFERENCES = {
  branches: [
    "media.aboutwebrtc",
    "media.peerconnection",
    "media.navigator",
    "media.getusermedia",
    "media.gmp-gmpopenh264.enabled",
  ],
  hidden: [
    "media.aboutwebrtc.auto_refresh.peerconnection_section",
    "media.aboutwebrtc.auto_refresh.connection_log_section",
    "media.aboutwebrtc.auto_refresh.user_modified_config_section",
    "media.aboutwebrtc.auto_refresh.media_ctx_section",
  ],
};

async function renderUserPrefSection() {
  const getConfigPaths = () => {
    return PREFERENCES.branches
      .flatMap(Services.prefs.getChildList)
      .filter(Services.prefs.prefHasUserValue)
      .filter(p => !PREFERENCES.hidden.includes(p));
  };
  const prefList = new ConfigurationList(getConfigPaths());
  return PrimarySection.make({
    headingL10nId: "about-webrtc-user-modified-configuration-heading",
    disclosureShowL10nId: "about-webrtc-user-modified-configuration-show-msg",
    disclosureHideL10nId: "about-webrtc-user-modified-configuration-hide-msg",
    autoRefreshPref:
      "media.aboutwebrtc.auto_refresh.user_modified_config_section",
    renderFn: () => prefList.view(),
    updateFn: () => {
      prefList.setPrefPaths(getConfigPaths());
      prefList.update();
    },
  });
}

function renderFoldableSection(parentElem, options = {}) {
  const section = renderElement("div");
  if (parentElem) {
    const ctrl = renderElements("div", { className: "section-ctrl no-print" }, [
      new FoldEffect(section, options).render(),
    ]);
    parentElem.append(ctrl);
  }
  return section;
}

function renderSimpleTable(caption, headings, data) {
  const heads = headings.map(text => renderElement("th", {}, text));
  const renderCell = text => renderText("td", text);

  return renderElements("table", {}, [
    caption,
    renderElements("tr", {}, heads),
    ...data.map(line => renderElements("tr", {}, line.map(renderCell))),
  ]);
}

class FoldEffect {
  constructor(
    target,
    {
      showMsg = "about-webrtc-fold-default-show-msg",
      hideMsg = "about-webrtc-fold-default-hide-msg",
      startsCollapsed = true,
    } = {}
  ) {
    Object.assign(this, { target, showMsg, hideMsg, startsCollapsed });
  }

  render() {
    this.target.classList.add("fold-target");
    this.trigger = renderElement("div", { className: "fold-trigger" });
    this.trigger.classList.add("heading-medium", this.showMsg, this.hideMsg);
    if (this.startsCollapsed) {
      this.collapse();
    }
    this.trigger.onclick = () => {
      if (this.target.classList.contains("fold-closed")) {
        this.expand();
      } else {
        this.collapse();
      }
    };
    return this.trigger;
  }

  expand() {
    this.target.classList.remove("fold-closed");
    document.l10n.setAttributes(this.trigger, this.hideMsg);
  }

  collapse() {
    this.target.classList.add("fold-closed");
    document.l10n.setAttributes(this.trigger, this.showMsg);
  }

  static expandAll() {
    for (const target of document.getElementsByClassName("fold-closed")) {
      target.classList.remove("fold-closed");
    }
    for (const trigger of document.getElementsByClassName("fold-trigger")) {
      const hideMsg = trigger.classList[2];
      document.l10n.setAttributes(trigger, hideMsg);
    }
  }

  static collapseAll() {
    for (const target of document.getElementsByClassName("fold-target")) {
      target.classList.add("fold-closed");
    }
    for (const trigger of document.getElementsByClassName("fold-trigger")) {
      const showMsg = trigger.classList[1];
      document.l10n.setAttributes(trigger, showMsg);
    }
  }
}

class PrimarySection {
  /** @returns {Promise<PrimarySection>} */
  static async make({
    headingL10nId,
    disclosureShowL10nId,
    disclosureHideL10nId,
    autoRefreshPref,
    renderFn = async () => {}, // Creates the filling for the disclosure
    updateFn = async () => {}, // Updates the contents.
    headerElementsFn = async () => [], // Accessory elements for the heading
  }) {
    const newSect = new PrimarySection();
    Object.assign(newSect, {
      autoRefreshPref,
      renderFn,
      updateFn,
      headerElementsFn,
    });

    // Top level of the section
    const sectionContainer = document.createElement("div");
    // Section heading is always visible and contains the disclosure control,
    // the section title, the autorefresh button, and any accessory elements.
    const sectionHeading = document.createElement("div");
    sectionHeading.className = "section-heading";
    sectionContainer.appendChild(sectionHeading);
    // The section body is the portion that contains the disclosure body
    // container.
    const sectionBody = document.createElement("div");
    sectionBody.className = "section-body";
    sectionContainer.appendChild(sectionBody);

    const disclosure = new Disclosure({
      showMsg: disclosureShowL10nId,
      hideMsg: disclosureHideL10nId,
    });
    sectionHeading.appendChild(disclosure.control());

    const heading = document.createElement("h3");
    document.l10n.setAttributes(heading, headingL10nId);
    sectionHeading.append(heading);

    const autorefresh = document.createElement("input");
    Object.assign(autorefresh, {
      type: "checkbox",
      class: "autorefresh",
      id: autoRefreshPref,
      checked: Services.prefs.getBoolPref(autoRefreshPref),
      onchange: () =>
        Services.prefs.setBoolPref(autoRefreshPref, autorefresh.checked),
    });
    newSect.autorefresh = autorefresh;
    newSect.autorefreshPrefState = newSect.autorefresh.checked;
    const autorefreshLabel = document.createElement("label");
    autorefreshLabel.className = "autorefresh";
    autorefreshLabel.htmlFor = autorefresh.id;
    document.l10n.setAttributes(
      autorefreshLabel,
      "about-webrtc-auto-refresh-label"
    );
    sectionHeading.append(autorefresh, autorefreshLabel);

    let rendered = await renderFn();
    if (rendered) {
      disclosure.view().appendChild(rendered);
    }
    sectionBody.append(disclosure.view());

    let headerElements = (await newSect.headerElementsFn(newSect)) || [];
    sectionHeading.append(...headerElements);

    newSect.section = sectionContainer;
    return newSect;
  }
  view() {
    return this.section;
  }
  async update() {
    return this.updateFn(this);
  }
  async autoUpdate() {
    let prefState = Services.prefs.getBoolPref(this.autoRefreshPref);
    if (prefState != this.autorefreshPrefState) {
      this.autorefreshPrefState = prefState;
      this.autorefresh.checked = prefState;
    }
    if (this.autorefresh.checked || this.autorefresh.indeterminate) {
      return this.updateFn(this);
    }
    return null;
  }
}

async function renderMediaCtx(rndr) {
  const ctx = WGI.getMediaContext();
  const prefs = [
    "media.peerconnection.video.vp9_enabled",
    "media.peerconnection.video.vp9_preferred",
    "media.navigator.video.h264.level",
    "media.navigator.video.h264.max_mbps",
    "media.navigator.video.h264.max_mbps",
    "media.navigator.video.max_fs",
    "media.navigator.video.max_fr",
    "media.navigator.video.use_tmmbr",
    "media.navigator.video.use_remb",
    "media.navigator.video.use_transport_cc",
    "media.navigator.audio.use_fec",
    "media.navigator.video.red_ulpfec_enabled",
    "media.webrtc.codec.video.av1.enabled",
    "media.webrtc.codec.video.av1.experimental_preferred",
  ];

  const confList = new ConfigurationList(prefs);
  const hasH264Hardware = rndr.text_p(
    `hasH264Hardware: ${ctx.hasH264Hardware}`
  );
  hasH264Hardware.dataset.value = ctx.hasH264Hardware;
  const hasAv1 = rndr.text_p(`hasAv1: ${ctx.hasAv1}`);
  hasAv1.dataset.value = ctx.hasAv1;
  const renderFn = async () =>
    rndr.elems_div({}, [
      hasH264Hardware,
      hasAv1,
      rndr.elem_hr(),
      confList.view(),
    ]);
  const updateFn = async () => {
    const newCtx = WGI.getMediaContext();
    if (hasH264Hardware.dataset.value != newCtx.hasH264Hardware) {
      hasH264Hardware.dataset.value = newCtx.hasH264Hardware;
      hasH264Hardware.textContent = `hasH264Hardware: ${newCtx.hasH264Hardware}`;
    }
    if (hasAv1.dataset.value != newCtx.hasAv1) {
      hasAv1.dataset.value = newCtx.hasAv1;
      hasAv1.textContent = `hasAv1: ${newCtx.hasAv1}`;
    }
    confList.update();
  };

  return PrimarySection.make({
    headingL10nId: "about-webrtc-media-context-heading",
    disclosureShowL10nId: "about-webrtc-media-context-show-msg",
    disclosureHideL10nId: "about-webrtc-media-context-hide-msg",
    autoRefreshPref: "media.aboutwebrtc.auto_refresh.media_ctx_section",
    renderFn,
    updateFn,
  });
}
