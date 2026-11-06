/* eslint-disable complexity */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const Actions = require("resource://devtools/client/netmonitor/src/actions/index.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  getFormattedIPAndPort,
  getFormattedSize,
  getRequestPriorityAsText,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");
const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const {
  getHeadersURL,
  getTrackingProtectionURL,
  getHTTPStatusCodeURL,
} = require("resource://devtools/client/netmonitor/src/utils/doc-utils.js");
const {
  fetchNetworkUpdatePacket,
  writeHeaderText,
  getRequestHeadersRawText,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const {
  HeadersProvider,
  HeaderList,
} = require("resource://devtools/client/netmonitor/src/utils/headers-provider.js");
const {
  FILTER_SEARCH_DELAY,
} = require("resource://devtools/client/netmonitor/src/constants.js");
// Components
const PropertiesView = createFactory(
  require("resource://devtools/client/netmonitor/src/components/request-details/PropertiesView.js")
);
const SearchBox = createFactory(
  require("resource://devtools/client/shared/components/SearchBox.js")
);
const Accordion = createFactory(
  require("resource://devtools/client/shared/components/Accordion.js")
);
const UrlPreview = createFactory(
  require("resource://devtools/client/netmonitor/src/components/previews/UrlPreview.js")
);
const HeadersPanelContextMenu = require("resource://devtools/client/netmonitor/src/widgets/HeadersPanelContextMenu.js");
const StatusCode = createFactory(
  require("resource://devtools/client/netmonitor/src/components/StatusCode.js")
);

loader.lazyGetter(this, "MDNLink", function () {
  return createFactory(
    require("resource://devtools/client/shared/components/MdnLink.js")
  );
});
loader.lazyGetter(this, "Rep", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/client/shared/components/reps/index.mjs"
  ).REPS.Rep;
});
loader.lazyGetter(this, "MODE", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/client/shared/components/reps/index.mjs"
  ).MODE;
});
loader.lazyGetter(this, "TreeRow", function () {
  return createFactory(
    ChromeUtils.importESModule(
      "resource://devtools/client/shared/components/tree/TreeRow.mjs",
      { global: "current" }
    ).default
  );
});
loader.lazyRequireGetter(
  this,
  "showMenu",
  "resource://devtools/client/shared/components/menu/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "openContentLink",
  "resource://devtools/client/shared/link.js",
  true
);

loader.lazyGetter(this, "HEADERS_PROXY_STATUS", function () {
  return L10N.getStr("netmonitor.headers.proxyStatus");
});

loader.lazyGetter(this, "HEADERS_PROXY_VERSION", function () {
  return L10N.getStr("netmonitor.headers.proxyVersion");
});

const { div, input, label, span, textarea, tr, td, button } = dom;

const RESEND = L10N.getStr("netmonitor.context.resend.label");
const EDIT_AND_RESEND = L10N.getStr("netmonitor.summary.editAndResend");
const RAW_HEADERS = L10N.getStr("netmonitor.headers.raw");
const HEADERS_EMPTY_TEXT = L10N.getStr("headersEmptyText");
const HEADERS_FILTER_TEXT = L10N.getStr("headersFilterText");
const HEADERS_STATUS = L10N.getStr("netmonitor.headers.status");
const HEADERS_EARLYHINT_STATUS = L10N.getStr(
  "netmonitor.headers.earlyHintsStatus"
);
const HEADERS_VERSION = L10N.getStr("netmonitor.headers.version");
const HEADERS_TRANSFERRED = L10N.getStr("netmonitor.toolbar.transferred");
const SUMMARY_STATUS_LEARN_MORE = L10N.getStr("netmonitor.summary.learnMore");
const SUMMARY_ETP_LEARN_MORE = L10N.getStr(
  "netmonitor.enhancedTrackingProtection.learnMore"
);
const HEADERS_REFERRER = L10N.getStr("netmonitor.headers.referrerPolicy");
const HEADERS_CONTENT_BLOCKING = L10N.getStr(
  "netmonitor.headers.contentBlocking"
);
const HEADERS_ETP = L10N.getStr(
  "netmonitor.trackingResource.enhancedTrackingProtection"
);
const HEADERS_PRIORITY = L10N.getStr("netmonitor.headers.requestPriority");
const HEADERS_DNS = L10N.getStr("netmonitor.headers.dns");

// Order is as displayed
const HEADERS_CONFIG = {
  earlyHintsResponseHeaders: {
    // Key for fetching the data from the backend
    fetchKey: "earlyHintsResponseHeaders",
    title: L10N.getStr("earlyHintsResponseHeaders"),
    // Gets the content to be displayed when switched to the raw headers view
    rawHeaderValue: ({ headerData }) => {
      const preHeaderText = headerData.rawHeaders.split("\r\n")[0];
      return writeHeaderText(headerData.headers, preHeaderText).trim();
    },
    // Gets the full display text to be displayed in the header title bar(before the raw toggle button)
    displayTitle: ({ headerData }) => {
      const title = HEADERS_CONFIG.earlyHintsResponseHeaders.title;
      if (headerData.headersSize) {
        return `${title} (${getFormattedSize(headerData.headersSize, 3)})`;
      }
      const rawHeaderValue =
        HEADERS_CONFIG.earlyHintsResponseHeaders.rawHeaderValue({ headerData });
      return `${title} (${getFormattedSize(rawHeaderValue.length, 3)})`;
    },
  },
  responseHeaders: {
    // Key for fetching the data from the backend
    fetchKey: "responseHeaders",
    title: L10N.getStr("responseHeaders"),
    // Gets the content to be displayed when switched to the raw headers view
    rawHeaderValue: ({ status, statusText, httpVersion, headerData }) => {
      const preHeaderText = `${httpVersion} ${status} ${statusText}`;
      return writeHeaderText(headerData.headers, preHeaderText).trim();
    },
    // Gets the full display text to be displayed in the header title bar(before the raw toggle button)
    displayTitle: ({ status, statusText, httpVersion, headerData }) => {
      const title = HEADERS_CONFIG.responseHeaders.title;
      if (headerData.headersSize) {
        return `${title} (${getFormattedSize(headerData.headersSize, 3)})`;
      }
      const rawHeaderValue = HEADERS_CONFIG.responseHeaders.rawHeaderValue({
        httpVersion,
        status,
        statusText,
        headerData,
      });
      return `${title} (${getFormattedSize(rawHeaderValue.length, 3)})`;
    },
  },
  requestHeaders: {
    // See comment above
    fetchKey: "requestHeaders",
    title: L10N.getStr("requestHeaders"),
    // See comment above
    rawHeaderValue: ({ method, httpVersion, headerData, urlDetails }) => {
      return getRequestHeadersRawText(
        method,
        httpVersion,
        headerData,
        urlDetails
      );
    },
    // See comment above
    displayTitle: ({ method, httpVersion, headerData, urlDetails }) => {
      const title = HEADERS_CONFIG.requestHeaders.title;
      if (headerData.headersSize) {
        return `${title} (${getFormattedSize(headerData.headersSize, 3)})`;
      }
      const rawHeaderValue = HEADERS_CONFIG.requestHeaders.rawHeaderValue({
        method,
        httpVersion,
        headerData,
        urlDetails,
      });
      return `${title} (${getFormattedSize(rawHeaderValue.length, 3)})`;
    },
  },
  requestHeadersFromUploadStream: {
    // See comment above
    fetchKey: "requestPostData",
    title: L10N.getStr("requestHeadersFromUpload"),
    // See comment above
    rawHeaderValue: ({ headerData, preHeaderText = "" }) => {
      return writeHeaderText(headerData.headers, preHeaderText).trim();
    },
    // See comment above
    displayTitle: ({ method, httpVersion, headerData, urlDetails }) => {
      const title = HEADERS_CONFIG.requestHeadersFromUploadStream.title;
      if (headerData.headersSize) {
        return `${title} (${getFormattedSize(headerData.headersSize, 3)})`;
      }
      let preHeaderText = "";
      const hostHeader = headerData.headers.find(ele => ele.name === "Host");
      if (hostHeader) {
        preHeaderText = `${method} ${
          urlDetails.url.split(hostHeader.value)[1]
        } ${httpVersion}`;
      }

      const rawHeaderValue =
        HEADERS_CONFIG.requestHeadersFromUploadStream.rawHeaderValue({
          headerData,
          preHeaderText,
        });
      return `${title} (${getFormattedSize(rawHeaderValue.length, 3)})`;
    },
  },
};

const HEADERS_TO_FETCH = Object.values(HEADERS_CONFIG).map(
  headers => headers.fetchKey
);
/**
 * Headers panel component
 * Lists basic information about the request
 *
 * In http/2 all response headers are in small case.
 * See: https://firefox-source-docs.mozilla.org/devtools-user/network_monitor/request_details/index.html#response-headers
 * RFC: https://tools.ietf.org/html/rfc7540#section-8.1.2
 */
class HeadersPanel extends Component {
  static get propTypes() {
    return {
      connector: PropTypes.object.isRequired,
      cloneSelectedRequest: PropTypes.func.isRequired,
      member: PropTypes.object,
      request: PropTypes.object.isRequired,
      renderValue: PropTypes.func,
      openLink: PropTypes.func,
      targetSearchResult: PropTypes.object,
      openRequestBlockingAndAddUrl: PropTypes.func.isRequired,
      openHTTPCustomRequestTab: PropTypes.func.isRequired,
      cloneRequest: PropTypes.func,
      sendCustomRequest: PropTypes.func,
      shouldExpandPreview: PropTypes.bool,
      setHeadersUrlPreviewExpanded: PropTypes.func,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      openedRawHeaders: new Set(),
      lastToggledRawHeader: "",
      filterText: null,
    };

    this.getProperties = this.getProperties.bind(this);
    this.getTargetHeaderPath = this.getTargetHeaderPath.bind(this);
    this.toggleRawHeader = this.toggleRawHeader.bind(this);
    this.renderSummary = this.renderSummary.bind(this);
    this.renderRow = this.renderRow.bind(this);
    this.renderValue = this.renderValue.bind(this);
    this.renderRawHeadersBtn = this.renderRawHeadersBtn.bind(this);
    this.onShowResendMenu = this.onShowResendMenu.bind(this);
    this.onShowHeadersContextMenu = this.onShowHeadersContextMenu.bind(this);
  }

  componentDidMount() {
    const { request, connector } = this.props;
    fetchNetworkUpdatePacket(connector.requestData, request, HEADERS_TO_FETCH);
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    const { request, connector } = nextProps;
    fetchNetworkUpdatePacket(connector.requestData, request, HEADERS_TO_FETCH);
  }

  // The title to be display in the heading
  getHeadersDisplayTitle(headerKey) {
    const headerData = this.props.request[headerKey];
    if (!headerData?.headers.length) {
      return "";
    }

    return HEADERS_CONFIG[headerKey].displayTitle({
      ...this.props.request,
      headerData,
    });
  }

  getProperties(headerKey) {
    let propertiesResult;
    const headerData = this.props.request[headerKey];
    if (headerData?.headers.length) {
      propertiesResult = {
        [headerKey]: this.state.openedRawHeaders.has(headerKey)
          ? { RAW_HEADERS_ID: headerData.rawHeaders }
          : new HeaderList(headerData.headers),
      };
    }
    return propertiesResult;
  }
  // Toggles the raw headers view on / off
  toggleRawHeader(headerKey) {
    const newOpenedRawHeaders = new Set([...this.state.openedRawHeaders]);
    if (newOpenedRawHeaders.has(headerKey)) {
      newOpenedRawHeaders.delete(headerKey);
    } else {
      newOpenedRawHeaders.add(headerKey);
    }
    this.setState({
      openedRawHeaders: newOpenedRawHeaders,
      lastToggledRawHeader: headerKey,
    });
  }

  /**
   * Renders the top part of the headers detail panel - Summary.
   */
  renderSummary(summaryLabel, value, summaryClass = "") {
    return div(
      {
        key: summaryLabel,
        className: "tabpanel-summary-container headers-summary " + summaryClass,
      },
      span(
        { className: "tabpanel-summary-label headers-summary-label" },
        summaryLabel
      ),
      span({ className: "tabpanel-summary-value" }, value)
    );
  }

  /**
   * Get path for target header if it's set. It's used to select
   * the header automatically within the tree of headers.
   * Note that the target header is set by the Search panel.
   */
  getTargetHeaderPath(searchResult) {
    if (!searchResult || !(searchResult.type in HEADERS_CONFIG)) {
      return null;
    }
    // Using `HeaderList` ensures that we'll get the same
    // header index as it's used in the tree.
    const headerData = this.props.request[searchResult.type];
    return (
      "/" +
      searchResult.type +
      "/" +
      new HeaderList(headerData.headers).headers.findIndex(
        header => header.name == searchResult.label
      )
    );
  }

  /**
   * Custom rendering method passed to PropertiesView. It's responsible
   * for rendering <textarea> element with raw headers data.
   */
  renderRow(props) {
    const { level, path } = props.member;

    if (path.includes("RAW_HEADERS_ID")) {
      const headerKey = path.split("/")[1];

      const value = HEADERS_CONFIG[headerKey].rawHeaderValue({
        ...this.props.request,
        headerData: this.props.request[headerKey],
      });

      let rows;
      if (value) {
        const match = value.match(/\n/g);
        rows = match !== null ? match.length : 0;
        // Need to add 1 for the horizontal scrollbar
        // not to cover the last row of raw data
        rows = rows + 1;
      }

      return tr(
        {
          key: path,
          role: "treeitem",
          className: "raw-headers-container",
          onClick: event => event.stopPropagation(),
        },
        td(
          {
            colSpan: 2,
          },
          textarea({
            className: "raw-headers",
            rows,
            value,
            readOnly: true,
          })
        )
      );
    }

    if (level !== 1) {
      return null;
    }

    return TreeRow(props);
  }

  renderRawHeadersBtn(headerKey) {
    return [
      label(
        {
          key: `${headerKey}RawHeadersBtn`,
          className: "raw-headers-toggle",
          onClick: event => event.stopPropagation(),
          onKeyDown: event => event.stopPropagation(),
        },
        span({ className: "headers-summary-label" }, RAW_HEADERS),
        span(
          { className: "raw-headers-toggle-input" },
          input({
            id: `raw-${headerKey}-checkbox`,
            checked: this.state.openedRawHeaders.has(headerKey),
            className: "devtools-checkbox-toggle",
            onChange: () => this.toggleRawHeader(headerKey),
            type: "checkbox",
          })
        )
      ),
    ];
  }

  renderValue(props) {
    const { member, value } = props;

    if (typeof value !== "string") {
      return null;
    }

    const headerDocURL = getHeadersURL(member.name);

    return div(
      { className: "treeValueCellDivider" },
      Rep(
        Object.assign(props, {
          // FIXME: A workaround for the issue in StringRep
          // Force StringRep to crop the text everytime
          member: Object.assign({}, member, { open: false }),
          mode: MODE.TINY,
          noGrip: true,
          openLink: openContentLink,
        })
      ),
      headerDocURL ? MDNLink({ url: headerDocURL }) : null
    );
  }

  getShouldOpen(headerKey, filterText, targetSearchResult) {
    return (item, opened) => {
      // If closed, open panel for these reasons
      //  1.The raw header is switched on or off
      //  2.The filter text is set
      //  3.The search text is set
      if (
        (!opened && this.state.lastToggledRawHeader === headerKey) ||
        (!opened && filterText) ||
        (!opened && targetSearchResult)
      ) {
        return true;
      }
      return !!opened;
    };
  }

  onShowResendMenu(event) {
    const {
      request: { id },
      cloneSelectedRequest,
      cloneRequest,
      sendCustomRequest,
    } = this.props;
    const menuItems = [
      {
        label: RESEND,
        type: "button",
        click: () => {
          cloneRequest(id);
          sendCustomRequest();
        },
      },
      {
        label: EDIT_AND_RESEND,
        type: "button",
        click: evt => {
          cloneSelectedRequest(evt);
        },
      },
    ];

    showMenu(menuItems, { button: event.target });
  }

  onShowHeadersContextMenu(event) {
    if (!this.contextMenu) {
      this.contextMenu = new HeadersPanelContextMenu();
    }
    this.contextMenu.open(event, window.getSelection());
  }

  render() {
    const {
      targetSearchResult,
      request: {
        fromCache,
        fromServiceWorker,
        httpVersion,
        method,
        remoteAddress,
        remotePort,
        status,
        statusText,
        urlDetails,
        referrerPolicy,
        priority,
        isThirdPartyTrackingResource,
        contentSize,
        transferredSize,
        isResolvedByTRR,
        proxyHttpVersion,
        proxyStatus,
        proxyStatusText,
        earlyHintsStatus,
      },
      openRequestBlockingAndAddUrl,
      openHTTPCustomRequestTab,
      shouldExpandPreview,
      setHeadersUrlPreviewExpanded,
    } = this.props;

    const headersDataExists = Object.keys(HEADERS_CONFIG).some(
      headerKey => this.props.request[headerKey]?.headers.length
    );

    if (!headersDataExists) {
      return div({ className: "empty-notice" }, HEADERS_EMPTY_TEXT);
    }

    const items = [];

    for (const headerKey in HEADERS_CONFIG) {
      if (this.props.request[headerKey]?.headers.length) {
        const { filterText } = this.state;
        items.push({
          component: PropertiesView,
          componentProps: {
            object: this.getProperties(headerKey),
            filterText,
            targetSearchResult,
            renderRow: this.renderRow,
            renderValue: this.renderValue,
            provider: HeadersProvider,
            selectPath: this.getTargetHeaderPath,
            defaultSelectFirstNode: false,
            enableInput: false,
            useQuotes: false,
          },
          header: this.getHeadersDisplayTitle(headerKey),
          buttons: this.renderRawHeadersBtn(headerKey),
          id: headerKey,
          opened: true,
          shouldOpen: this.getShouldOpen(
            headerKey,
            filterText,
            targetSearchResult
          ),
        });
      }
    }

    const sizeText = L10N.getFormatStrWithNumbers(
      "netmonitor.headers.sizeDetails",
      getFormattedSize(transferredSize),
      getFormattedSize(contentSize)
    );

    const summarySize = this.renderSummary(HEADERS_TRANSFERRED, sizeText);

    let summaryEarlyStatus;
    if (earlyHintsStatus) {
      summaryEarlyStatus = div(
        {
          key: "headers-summary",
          className:
            "tabpanel-summary-container headers-summary headers-earlyhint-status",
        },
        span(
          {
            className: "tabpanel-summary-label headers-summary-label",
          },
          HEADERS_EARLYHINT_STATUS
        ),
        span(
          {
            className: "tabpanel-summary-value status",
            "data-code": earlyHintsStatus,
          },
          StatusCode({
            item: {
              fromCache,
              fromServiceWorker,
              status: earlyHintsStatus,
              statusText: "",
            },
          }),
          MDNLink({
            url: getHTTPStatusCodeURL(earlyHintsStatus),
            title: SUMMARY_STATUS_LEARN_MORE,
          })
        )
      );
    }
    let summaryStatus;
    if (status) {
      summaryStatus = div(
        {
          key: "headers-summary",
          className: "tabpanel-summary-container headers-summary",
        },
        span(
          {
            className: "tabpanel-summary-label headers-summary-label",
          },
          HEADERS_STATUS
        ),
        span(
          {
            className: "tabpanel-summary-value status",
            "data-code": status,
          },
          StatusCode({
            item: { fromCache, fromServiceWorker, status, statusText },
          }),
          statusText,
          MDNLink({
            url: getHTTPStatusCodeURL(status),
            title: SUMMARY_STATUS_LEARN_MORE,
          })
        )
      );
    }

    let summaryProxyStatus;
    if (proxyStatus) {
      summaryProxyStatus = div(
        {
          key: "headers-summary ",
          className:
            "tabpanel-summary-container headers-summary headers-proxy-status",
        },
        span(
          {
            className: "tabpanel-summary-label headers-summary-label",
          },
          HEADERS_PROXY_STATUS
        ),
        span(
          {
            className: "tabpanel-summary-value status",
            "data-code": proxyStatus,
          },
          StatusCode({
            item: {
              fromCache,
              fromServiceWorker,
              status: proxyStatus,
              statusText: proxyStatusText,
            },
          }),
          proxyStatusText,
          MDNLink({
            url: getHTTPStatusCodeURL(proxyStatus),
            title: SUMMARY_STATUS_LEARN_MORE,
          })
        )
      );
    }

    let trackingProtectionStatus;
    let trackingProtectionDetails = "";
    if (isThirdPartyTrackingResource) {
      const trackingProtectionDocURL = getTrackingProtectionURL();

      trackingProtectionStatus = this.renderSummary(
        HEADERS_CONTENT_BLOCKING,
        div(null, span({ className: "tracking-resource" }), HEADERS_ETP)
      );
      trackingProtectionDetails = this.renderSummary(
        "",
        div(
          {
            key: "tracking-protection",
            className: "tracking-protection",
          },
          L10N.getStr("netmonitor.trackingResource.tooltip"),
          trackingProtectionDocURL
            ? MDNLink({
                url: trackingProtectionDocURL,
                title: SUMMARY_ETP_LEARN_MORE,
              })
            : span({ className: "headers-summary learn-more-link" })
        )
      );
    }

    const summaryVersion = httpVersion
      ? this.renderSummary(HEADERS_VERSION, httpVersion)
      : null;

    const summaryProxyHttpVersion = proxyHttpVersion
      ? this.renderSummary(
          HEADERS_PROXY_VERSION,
          proxyHttpVersion,
          "headers-proxy-version"
        )
      : null;

    const summaryReferrerPolicy = referrerPolicy
      ? this.renderSummary(HEADERS_REFERRER, referrerPolicy)
      : null;

    const summaryPriority = priority
      ? this.renderSummary(HEADERS_PRIORITY, getRequestPriorityAsText(priority))
      : null;

    const summaryDNS = this.renderSummary(
      HEADERS_DNS,
      L10N.getStr(
        isResolvedByTRR
          ? "netmonitor.headers.dns.overHttps"
          : "netmonitor.headers.dns.basic"
      )
    );

    const summaryItems = [
      summaryEarlyStatus,
      summaryStatus,
      summaryProxyStatus,
      summaryVersion,
      summaryProxyHttpVersion,
      summarySize,
      summaryReferrerPolicy,
      summaryPriority,
      summaryDNS,
      trackingProtectionStatus,
      trackingProtectionDetails,
    ].filter(summaryItem => summaryItem !== null);

    const newEditAndResendPref = Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend"
    );

    return div(
      { className: "headers-panel-container" },
      div(
        { className: "devtools-toolbar devtools-input-toolbar" },
        SearchBox({
          delay: FILTER_SEARCH_DELAY,
          type: "filter",
          onChange: text => this.setState({ filterText: text }),
          placeholder: HEADERS_FILTER_TEXT,
        }),
        span({ className: "devtools-separator" }),
        button(
          {
            id: "block-button",
            className: "devtools-button",
            title: L10N.getStr("netmonitor.context.blockURL"),
            onClick: () => openRequestBlockingAndAddUrl(urlDetails.url),
          },
          L10N.getStr("netmonitor.headers.toolbar.block")
        ),
        span({ className: "devtools-separator" }),
        button(
          {
            id: "edit-resend-button",
            className: !newEditAndResendPref
              ? "devtools-button devtools-dropdown-button"
              : "devtools-button",
            title: RESEND,
            onClick: !newEditAndResendPref
              ? this.onShowResendMenu
              : () => {
                  openHTTPCustomRequestTab();
                },
          },
          span({ className: "title" }, RESEND)
        )
      ),
      div(
        { className: "panel-container" },
        div(
          { className: "headers-overview" },
          UrlPreview({
            url: urlDetails.url,
            method,
            address: remoteAddress
              ? getFormattedIPAndPort(remoteAddress, remotePort)
              : null,
            shouldExpandPreview,
            onTogglePreview: expanded => setHeadersUrlPreviewExpanded(expanded),
            proxyStatus,
          }),
          div(
            {
              className: "summary",
              onContextMenu: this.onShowHeadersContextMenu,
            },
            summaryItems
          )
        ),
        Accordion({ items })
      )
    );
  }
}

module.exports = connect(
  state => ({
    shouldExpandPreview: state.ui.shouldExpandHeadersUrlPreview,
  }),
  dispatch => ({
    setHeadersUrlPreviewExpanded: expanded =>
      dispatch(Actions.setHeadersUrlPreviewExpanded(expanded)),
    openRequestBlockingAndAddUrl: url =>
      dispatch(Actions.openRequestBlockingAndAddUrl(url)),
    openHTTPCustomRequestTab: () =>
      dispatch(Actions.openHTTPCustomRequest(true)),
    cloneRequest: id => dispatch(Actions.cloneRequest(id)),
    sendCustomRequest: () => dispatch(Actions.sendCustomRequest()),
  })
)(HeadersPanel);
