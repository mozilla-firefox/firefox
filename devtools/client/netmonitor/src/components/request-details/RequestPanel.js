/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const {
  fetchNetworkUpdatePacket,
  getRequestHeader,
  isJsonlContentType,
  parseFormData,
  parseJSON,
  parseJSONL,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const {
  sortObjectKeys,
} = require("resource://devtools/client/netmonitor/src/utils/sort-utils.js");
const {
  FILTER_SEARCH_DELAY,
} = require("resource://devtools/client/netmonitor/src/constants.js");
const {
  updateFormDataSections,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const Actions = require("resource://devtools/client/netmonitor/src/actions/index.js");

// Components
const PropertiesView = createFactory(
  require("resource://devtools/client/netmonitor/src/components/request-details/PropertiesView.js")
);
const SearchBox = createFactory(
  require("resource://devtools/client/shared/components/SearchBox.js")
);

loader.lazyGetter(this, "SourcePreview", function () {
  return createFactory(
    require("resource://devtools/client/netmonitor/src/components/previews/SourcePreview.js")
  );
});

const { div, input, label, span, h2 } = dom;

const JSON_SCOPE_NAME = L10N.getStr("jsonScopeName");
const JSONL_SCOPE_NAME = L10N.getStr("jsonlScopeName");
const REQUEST_EMPTY_TEXT = L10N.getStr("paramsNoPayloadText");
const REQUEST_FILTER_TEXT = L10N.getStr("paramsFilterText");
const REQUEST_FORM_DATA = L10N.getStr("paramsFormData");
const REQUEST_POST_PAYLOAD = L10N.getStr("paramsPostPayload");
const RAW_REQUEST_PAYLOAD = L10N.getStr("netmonitor.request.raw");
const REQUEST_TRUNCATED = L10N.getStr("requestTruncated");

/**
 * Params panel component
 * Displays the GET parameters and POST data of a request
 */
class RequestPanel extends Component {
  static get propTypes() {
    return {
      connector: PropTypes.object.isRequired,
      openLink: PropTypes.func,
      request: PropTypes.object.isRequired,
      updateRequest: PropTypes.func.isRequired,
      targetSearchResult: PropTypes.object,
    };
  }

  constructor(props) {
    super(props);
    this.state = {
      filterText: "",
      rawRequestPayloadDisplayed: !!props.targetSearchResult,
    };

    this.toggleRawRequestPayload = this.toggleRawRequestPayload.bind(this);
    this.renderRawRequestPayloadBtn =
      this.renderRawRequestPayloadBtn.bind(this);
  }

  componentDidMount() {
    const { request, connector } = this.props;
    fetchNetworkUpdatePacket(connector.requestData, request, [
      "requestHeaders",
      "requestPostData",
    ]);
    updateFormDataSections(this.props);
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    const { request, connector } = nextProps;
    fetchNetworkUpdatePacket(connector.requestData, request, [
      "requestHeaders",
      "requestPostData",
    ]);
    updateFormDataSections(nextProps);

    if (nextProps.targetSearchResult !== null) {
      this.setState({
        rawRequestPayloadDisplayed: !!nextProps.targetSearchResult,
      });
    }
  }

  /**
   * Update only if:
   * 1) The rendered object has changed
   * 2) The filter text has changed
   * 2) The display got toggled between formatted and raw data
   * 3) The user selected another search result target.
   */
  shouldComponentUpdate(nextProps, nextState) {
    return (
      this.props.request !== nextProps.request ||
      this.state.filterText !== nextState.filterText ||
      this.state.rawRequestPayloadDisplayed !==
        nextState.rawRequestPayloadDisplayed ||
      this.props.targetSearchResult !== nextProps.targetSearchResult
    );
  }

  /**
   * This maps an array to a dictionary for TreeView usage,
   * sincs the treeView only supports the Object(dict) format.
   *
   * This function also deals with the duplicate key scenario
   * (i.e multiple selections and query params with same keys)
   *
   * Note: This is not sorting the result properties since it can
   * result in an unexpected order of parameters. See bug 1469533
   *
   * @param {object[]} arrOfKeyValuePairs - An array of key-value pairs or form params.
   * @param {string} arrOfKeyValuePairs[].name
   * @param {string|Array} arrOfKeyValuePairs[].value
   *
   * @returns {object} Rep compatible object
   */
  getProperties(arrOfKeyValuePairs) {
    return arrOfKeyValuePairs.reduce((dict, { name, value }) => {
      if (name in dict) {
        const dictValue = dict[name];
        if (!Array.isArray(dictValue)) {
          dict[name] = [dictValue];
        }
        dict[name].push(value);
      } else {
        dict[name] = value;
      }
      return dict;
    }, Object.create(null));
  }

  toggleRawRequestPayload() {
    this.setState({
      rawRequestPayloadDisplayed: !this.state.rawRequestPayloadDisplayed,
    });
  }

  renderRawRequestPayloadBtn(key, checked, onChange) {
    return [
      label(
        {
          key: `${key}RawRequestPayloadBtn`,
          className: "raw-data-toggle",
          onClick: event => {
            // stop the header click event
            event.stopPropagation();
          },
        },
        span({ className: "raw-data-toggle-label" }, RAW_REQUEST_PAYLOAD),
        span(
          { className: "raw-data-toggle-input" },
          input({
            id: `raw-${key}-checkbox`,
            checked,
            className: "devtools-checkbox-toggle",
            onChange,
            type: "checkbox",
          })
        )
      ),
    ];
  }

  /**
   * Parses a request payload which can be displayed as a tree, either JSON or
   * JSON Lines.
   *
   * @param {string} postData: the request payload
   * @returns {object|null} null when the payload has no formatted display,
   *  otherwise an object with the shape:
   *  {object} object: the parsed payload, passed to PropertiesView
   *  {boolean} isJsonl: true when the payload was parsed as JSON Lines
   */
  parseStructuredPayload(postData) {
    const { request } = this.props;

    // The payload's own content type, not request.mimeType, which describes
    // the response.
    if (isJsonlContentType(getRequestHeader(request, "content-type"))) {
      const { json } = parseJSONL(postData);
      // Not sorted: JSON Lines entries are in document order.
      return json ? { object: json, isJsonl: true } : null;
    }

    const { json, strippedChars } = parseJSON(postData);
    // If XSSI characters were present in the request just display the raw
    // data because a request should never have XSSI escape characters
    if (strippedChars || !json) {
      return null;
    }
    return { object: sortObjectKeys(json), isJsonl: false };
  }

  renderRequestPayload(component, componentProps) {
    return component(componentProps);
  }

  render() {
    const { request, targetSearchResult } = this.props;
    const { filterText, rawRequestPayloadDisplayed } = this.state;
    const { formDataSections, mimeType, requestPostData, url } = request;
    const postData = requestPostData ? requestPostData.postData?.text : null;

    if ((!formDataSections || formDataSections.length === 0) && !postData) {
      return div({ className: "empty-notice" }, REQUEST_EMPTY_TEXT);
    }

    let component;
    let componentProps;
    let requestPayloadLabel = REQUEST_POST_PAYLOAD;
    let hasFormattedDisplay = false;

    let error;

    // Form Data section
    if (formDataSections && formDataSections.length) {
      component = PropertiesView;
      componentProps = {
        object: this.getProperties(parseFormData(formDataSections)),
        filterText,
        targetSearchResult,
        defaultSelectFirstNode: false,
        url,
      };
      requestPayloadLabel = REQUEST_FORM_DATA;
      hasFormattedDisplay = true;
    }

    // Request payload section
    const limit = Services.prefs.getIntPref("devtools.netmonitor.bodyLimit");

    // Check if the request post data has been truncated from the backend,
    // in which case no parse should be attempted.
    if (postData && limit > 0 && limit <= postData.length) {
      error = REQUEST_TRUNCATED;
    }
    if (
      formDataSections &&
      formDataSections.length === 0 &&
      postData &&
      !error
    ) {
      const parsedPayload = this.parseStructuredPayload(postData);
      if (parsedPayload) {
        component = PropertiesView;
        componentProps = {
          object: parsedPayload.object,
          filterText,
          targetSearchResult,
          defaultSelectFirstNode: false,
          url,
        };
        requestPayloadLabel = parsedPayload.isJsonl
          ? JSONL_SCOPE_NAME
          : JSON_SCOPE_NAME;
        hasFormattedDisplay = true;
      }
    }

    if (
      (!hasFormattedDisplay || this.state.rawRequestPayloadDisplayed) &&
      postData
    ) {
      component = SourcePreview;
      componentProps = {
        text: postData,
        mimeType: mimeType ? mimeType.replace(/;.+/, "") : "text/plain",
        targetSearchResult,
        url,
      };
      requestPayloadLabel = REQUEST_POST_PAYLOAD;
    }

    return div(
      { className: "panel-container" },
      error && div({ className: "request-error-header", title: error }, error),
      div(
        { className: "devtools-toolbar devtools-input-toolbar" },
        SearchBox({
          delay: FILTER_SEARCH_DELAY,
          type: "filter",
          onChange: text => this.setState({ filterText: text }),
          placeholder: REQUEST_FILTER_TEXT,
        })
      ),
      h2({ className: "data-header", role: "heading" }, [
        span(
          {
            key: "data-label",
            className: "data-label",
          },
          requestPayloadLabel
        ),
        hasFormattedDisplay &&
          this.renderRawRequestPayloadBtn(
            "request",
            rawRequestPayloadDisplayed,
            this.toggleRawRequestPayload
          ),
      ]),
      this.renderRequestPayload(component, componentProps)
    );
  }
}

module.exports = connect(null, dispatch => ({
  updateRequest: (id, data, batch) =>
    dispatch(Actions.updateRequest(id, data, batch)),
}))(RequestPanel);
