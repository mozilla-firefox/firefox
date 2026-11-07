/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const { div, input, label, span, h2 } = dom;
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const {
  getMessagePayload,
  getResponseHeader,
  parseJSON,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const {
  getFormattedSize,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");
const MESSAGE_DATA_LIMIT = Services.prefs.getIntPref(
  "devtools.netmonitor.msg.messageDataLimit"
);
const MESSAGE_DATA_TRUNCATED = L10N.getStr("messageDataTruncated");
const SocketIODecoder = require("resource://devtools/client/netmonitor/src/components/messages/parsers/socket-io/index.js");
const {
  JsonHubProtocol,
  HandshakeProtocol,
} = require("resource://devtools/client/netmonitor/src/components/messages/parsers/signalr/index.js");
const {
  parseSockJS,
} = require("resource://devtools/client/netmonitor/src/components/messages/parsers/sockjs/index.js");
const {
  parseStompJs,
} = require("resource://devtools/client/netmonitor/src/components/messages/parsers/stomp/index.js");
const {
  wampSerializers,
} = require("resource://devtools/client/netmonitor/src/components/messages/parsers/wamp/serializers.js");
const {
  getRequestByChannelId,
} = require("resource://devtools/client/netmonitor/src/selectors/index.js");

// Components
const RawData = createFactory(
  require("resource://devtools/client/netmonitor/src/components/messages/RawData.js")
);
loader.lazyGetter(this, "PropertiesView", function () {
  return createFactory(
    require("resource://devtools/client/netmonitor/src/components/request-details/PropertiesView.js")
  );
});

const RAW_DATA = L10N.getStr("netmonitor.response.raw");

/**
 * Shows the full payload of a message.
 * The payload is unwrapped from the LongStringActor object.
 */
class MessagePayload extends Component {
  static get propTypes() {
    return {
      connector: PropTypes.object.isRequired,
      selectedMessage: PropTypes.object,
      request: PropTypes.object.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      payload: "",
      isFormattedData: false,
      formattedData: {},
      formattedDataTitle: "",
      rawDataDisplayed: false,
    };

    this.toggleRawData = this.toggleRawData.bind(this);
    this.renderRawDataBtn = this.renderRawDataBtn.bind(this);
  }

  componentDidMount() {
    this.updateMessagePayload();
  }

  componentDidUpdate(prevProps) {
    if (this.props.selectedMessage !== prevProps.selectedMessage) {
      this.updateMessagePayload();
    }
  }

  updateMessagePayload() {
    const { selectedMessage, connector } = this.props;

    getMessagePayload(selectedMessage.payload, connector.getLongString).then(
      async payload => {
        const { formattedData, formattedDataTitle } =
          await this.parsePayload(payload);
        this.setState({
          payload,
          isFormattedData: !!formattedData,
          formattedData,
          formattedDataTitle,
        });
      }
    );
  }

  async parsePayload(payload) {
    const { connector, selectedMessage, request } = this.props;

    // Don't apply formatting to control frames
    // Control frame check can be done using opCode as specified here:
    // https://tools.ietf.org/html/rfc6455
    const controlFrames = [0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf];
    const isControlFrame = controlFrames.includes(selectedMessage.opCode);
    if (isControlFrame) {
      return {
        formattedData: null,
        formattedDataTitle: "",
      };
    }

    // Make sure that request headers are fetched from the backend before
    // looking for `Sec-WebSocket-Protocol` header.
    const responseHeaders = await connector.requestData(
      request.id,
      "responseHeaders"
    );

    if (!responseHeaders.headers) {
      // If the network event actor was destroyed while retrieving the request
      // data, no headers will be available.
      return {
        formattedData: null,
        formattedDataTitle: "",
      };
    }

    const wsProtocol = getResponseHeader(
      { responseHeaders },
      "Sec-WebSocket-Protocol"
    );

    const wampSerializer = wampSerializers[wsProtocol];
    if (wampSerializer) {
      const wampPayload = wampSerializer.deserializeMessage(payload);

      return {
        formattedData: wampPayload,
        formattedDataTitle: wampSerializer.description,
      };
    }

    // socket.io payload
    const socketIOPayload = this.parseSocketIOPayload(payload);

    if (socketIOPayload) {
      return {
        formattedData: socketIOPayload,
        formattedDataTitle: "Socket.IO",
      };
    }
    // sockjs payload
    const sockJSPayload = parseSockJS(payload);
    if (sockJSPayload) {
      let formattedData = sockJSPayload.data;

      if (sockJSPayload.type === "message") {
        if (Array.isArray(formattedData)) {
          formattedData = formattedData.map(
            message => parseStompJs(message) || message
          );
        } else {
          formattedData = parseStompJs(formattedData) || formattedData;
        }
      }

      return {
        formattedData,
        formattedDataTitle: "SockJS",
      };
    }
    // signalr payload
    const signalRPayload = this.parseSignalR(payload);
    if (signalRPayload) {
      return {
        formattedData: signalRPayload,
        formattedDataTitle: "SignalR",
      };
    }
    // STOMP
    const stompPayload = parseStompJs(payload);
    if (stompPayload) {
      return {
        formattedData: stompPayload,
        formattedDataTitle: "STOMP",
      };
    }

    // json payload
    let { json } = parseJSON(payload);
    if (json) {
      const { data, identifier } = json;
      // A json payload MAY be an "Action cable" if it
      // contains either a `data` or an `identifier` property
      // which are also json strings and would need to be parsed.
      // See https://medium.com/codequest/actioncable-in-rails-api-f087b65c860d
      if (
        (data && typeof data == "string") ||
        (identifier && typeof identifier == "string")
      ) {
        const actionCablePayload = this.parseActionCable(json);
        return {
          formattedData: actionCablePayload,
          formattedDataTitle: "Action Cable",
        };
      }

      if (Array.isArray(json)) {
        json = json.map(message => parseStompJs(message) || message);
      }

      return {
        formattedData: json,
        formattedDataTitle: "JSON",
      };
    }

    // JSON Lines payload. Frames carry no content type, so this is only
    // attempted once the payload turned out not to be a single JSON value.
    const jsonlPayload = this.parseJsonl(payload);
    if (jsonlPayload) {
      return {
        formattedData: jsonlPayload,
        formattedDataTitle: "JSON Lines",
      };
    }

    return {
      formattedData: null,
      formattedDataTitle: "",
    };
  }

  /**
   * Parses a payload holding several JSON values, one per line.
   *
   * Every non-blank line has to parse for the payload to be treated as JSON
   * Lines, so that ordinary multi-line text isn't turned into a tree. parseJSON
   * rejects primitives, which also keeps a payload such as "1\n2\n3" out.
   *
   * @param {string} payload
   * @returns {Array|null} one entry per line, or null when the payload isn't
   *                       JSON Lines
   */
  parseJsonl(payload) {
    const lines = payload.split("\n").filter(line => line.trim());
    if (lines.length < 2) {
      return null;
    }

    const entries = [];
    for (const line of lines) {
      const { json } = parseJSON(line);
      if (!json) {
        return null;
      }
      entries.push(json);
    }
    return entries;
  }

  parseSocketIOPayload(payload) {
    let result;
    // Try decoding socket.io frames
    try {
      const decoder = new SocketIODecoder();
      decoder.on("decoded", decodedPacket => {
        if (
          decodedPacket &&
          !decodedPacket.data.includes("parser error") &&
          decodedPacket.type
        ) {
          result = decodedPacket;
        }
      });
      decoder.add(payload);
      return result;
    } catch (err) {
      // Ignore errors
    }
    return null;
  }

  parseSignalR(payload) {
    // attempt to parse as HandshakeResponseMessage
    let decoder;
    try {
      decoder = new HandshakeProtocol();
      const [remainingData, responseMessage] =
        decoder.parseHandshakeResponse(payload);

      if (responseMessage) {
        return {
          handshakeResponse: responseMessage,
          remainingData: this.parseSignalR(remainingData),
        };
      }
    } catch (err) {
      // ignore errors;
    }

    // attempt to parse as JsonHubProtocolMessage
    try {
      decoder = new JsonHubProtocol();
      const msgs = decoder.parseMessages(payload, null);
      if (msgs?.length) {
        return msgs;
      }
    } catch (err) {
      // ignore errors;
    }

    // MVP Signalr
    if (payload.endsWith("\u001e")) {
      const { json } = parseJSON(payload.slice(0, -1));
      if (json) {
        return json;
      }
    }

    return null;
  }

  parseActionCable(payload) {
    const identifier = payload.identifier && parseJSON(payload.identifier).json;
    const data = payload.data && parseJSON(payload.data).json;

    if (identifier) {
      payload.identifier = identifier;
    }
    if (data) {
      payload.data = data;
    }
    return payload;
  }

  toggleRawData() {
    this.setState({
      rawDataDisplayed: !this.state.rawDataDisplayed,
    });
  }

  renderRawDataBtn(key, checked, onChange) {
    return [
      label(
        {
          key: `${key}RawDataBtn`,
          className: "raw-data-toggle",
          htmlFor: `raw-${key}-checkbox`,
          onClick: event => {
            // stop the header click event
            event.stopPropagation();
          },
        },
        span({ className: "raw-data-toggle-label" }, RAW_DATA),
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

  renderData(component, componentProps) {
    return component(componentProps);
  }

  render() {
    let component;
    let componentProps;
    let dataLabel;
    let { payload, rawDataDisplayed } = this.state;
    let isTruncated = false;
    if (this.state.payload.length >= MESSAGE_DATA_LIMIT) {
      payload = payload.substring(0, MESSAGE_DATA_LIMIT);
      isTruncated = true;
    }

    if (
      !isTruncated &&
      this.state.isFormattedData &&
      !this.state.rawDataDisplayed
    ) {
      component = PropertiesView;
      componentProps = {
        object: this.state.formattedData,
      };
      dataLabel = this.state.formattedDataTitle;
    } else {
      component = RawData;
      componentProps = { payload };
      dataLabel = L10N.getFormatStrWithNumbers(
        "netmonitor.ws.rawData.header",
        getFormattedSize(
          this.props.selectedMessage.payloadSize ?? this.state.payload.length
        )
      );
    }

    return div(
      {
        className: "message-payload",
      },
      isTruncated &&
        div(
          {
            className: "truncated-data-message",
          },
          MESSAGE_DATA_TRUNCATED
        ),
      h2({ className: "data-header", role: "heading" }, [
        span({ key: "data-label", className: "data-label" }, dataLabel),
        !isTruncated &&
          this.state.isFormattedData &&
          this.renderRawDataBtn("data", rawDataDisplayed, this.toggleRawData),
      ]),
      this.renderData(component, componentProps)
    );
  }
}

module.exports = connect(state => ({
  request: getRequestByChannelId(state, state.messages.currentChannelId),
}))(MessagePayload);
