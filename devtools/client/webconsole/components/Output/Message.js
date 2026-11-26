/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  Component,
  createFactory,
  createElement,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const actions = require("resource://devtools/client/webconsole/actions/index.js");
const {
  MESSAGE_LEVEL,
  MESSAGE_SOURCE,
  MESSAGE_TYPE,
} = require("resource://devtools/client/webconsole/constants.js");
const {
  MessageIndent,
} = require("resource://devtools/client/webconsole/components/Output/MessageIndent.js");
const MessageIcon = require("resource://devtools/client/webconsole/components/Output/MessageIcon.js");
const FrameView = createFactory(
  require("resource://devtools/client/shared/components/Frame.js")
);

loader.lazyRequireGetter(
  this,
  "CollapseButton",
  "resource://devtools/client/webconsole/components/Output/CollapseButton.js"
);
loader.lazyRequireGetter(
  this,
  "MessageRepeat",
  "resource://devtools/client/webconsole/components/Output/MessageRepeat.js"
);
loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);
loader.lazyRequireGetter(
  this,
  "SmartTrace",
  "resource://devtools/client/shared/components/SmartTrace.js"
);

class Message extends Component {
  static get propTypes() {
    return {
      open: PropTypes.bool,
      collapsible: PropTypes.bool,
      collapseTitle: PropTypes.string,
      disabled: PropTypes.bool,
      onToggle: PropTypes.func,
      source: PropTypes.string.isRequired,
      type: PropTypes.string.isRequired,
      level: PropTypes.string.isRequired,
      indent: PropTypes.number.isRequired,
      inWarningGroup: PropTypes.bool,
      isBlockedNetworkMessage: PropTypes.bool,
      topLevelClasses: PropTypes.array.isRequired,
      messageBody: PropTypes.any.isRequired,
      repeat: PropTypes.any,
      frame: PropTypes.any,
      attachment: PropTypes.any,
      stacktrace: PropTypes.any,
      messageId: PropTypes.string,
      scrollToMessage: PropTypes.bool,
      exceptionDocURL: PropTypes.string,
      request: PropTypes.object,
      dispatch: PropTypes.func,
      timeStamp: PropTypes.number,
      timestampsVisible: PropTypes.bool.isRequired,
      serviceContainer: PropTypes.shape({
        emitForTests: PropTypes.func.isRequired,
        onViewSource: PropTypes.func.isRequired,
        onViewSourceInDebugger: PropTypes.func,
        onViewSourceInStyleEditor: PropTypes.func,
        openContextMenu: PropTypes.func.isRequired,
        openLink: PropTypes.func.isRequired,
        sourceMapURLService: PropTypes.any,
        preventStacktraceInitialRenderDelay: PropTypes.bool,
      }),
      notes: PropTypes.arrayOf(
        PropTypes.shape({
          messageBody: PropTypes.string.isRequired,
          frame: PropTypes.any,
        })
      ),
      maybeScrollToBottom: PropTypes.func,
      message: PropTypes.object.isRequired,
    };
  }

  static get defaultProps() {
    return {
      indent: 0,
    };
  }

  constructor(props) {
    super(props);
    this.onLearnMoreClick = this.onLearnMoreClick.bind(this);
    this.toggleMessage = this.toggleMessage.bind(this);
    this.onContextMenu = this.onContextMenu.bind(this);
    this.renderIcon = this.renderIcon.bind(this);
  }

  componentDidMount() {
    if (this.messageNode) {
      if (this.props.scrollToMessage) {
        this.messageNode.scrollIntoView();
      }

      this.emitNewMessage(this.messageNode);
    }
  }

  componentDidCatch(e) {
    this.setState({ error: e });
  }

  // Event used in tests. Some message types don't pass it in because existing tests
  // did not emit for them.
  emitNewMessage(node) {
    const { serviceContainer, messageId, timeStamp } = this.props;
    serviceContainer.emitForTests(
      "new-messages",
      new Set([{ node, messageId, timeStamp }])
    );
  }

  onLearnMoreClick(e) {
    const { exceptionDocURL } = this.props;
    this.props.serviceContainer.openLink(exceptionDocURL, e);
    e.preventDefault();
  }

  toggleMessage(e) {
    // Don't bubble up to the main App component, which  redirects focus to input,
    // making difficult for screen reader users to review output
    e.stopPropagation();
    const { open, dispatch, messageId, onToggle, disabled } = this.props;

    if (disabled) {
      return;
    }

    // Early exit the function to avoid the message to collapse if the user is
    // selecting a range in the toggle message.
    const window = e.target.ownerDocument.defaultView;
    if (window.getSelection && window.getSelection().type === "Range") {
      return;
    }

    // If defined on props, we let the onToggle() method handle the toggling,
    // otherwise we toggle the message open/closed ourselves.
    if (onToggle) {
      onToggle(messageId, e);
    } else if (open) {
      dispatch(actions.messageClose(messageId));
    } else {
      dispatch(actions.messageOpen(messageId));
    }
  }

  onContextMenu(e) {
    const { serviceContainer, source, request, messageId } = this.props;
    const messageInfo = {
      source,
      request,
      messageId,
    };
    serviceContainer.openContextMenu(e, messageInfo);
    e.stopPropagation();
    e.preventDefault();
  }

  renderIcon() {
    const { level, inWarningGroup, isBlockedNetworkMessage, type, disabled } =
      this.props;

    if (inWarningGroup) {
      return undefined;
    }

    if (disabled) {
      return MessageIcon({
        level: MESSAGE_LEVEL.INFO,
        type,
        title: l10n.getStr("webconsole-disabled-icon-title"),
      });
    }

    if (isBlockedNetworkMessage) {
      return MessageIcon({
        level: MESSAGE_LEVEL.ERROR,
        type: "blockedReason",
      });
    }

    return MessageIcon({
      level,
      type,
    });
  }

  renderTimestamp() {
    if (!this.props.timestampsVisible) {
      return null;
    }

    const timestamp = this.props.timeStamp || Date.now();

    return dom.span(
      {
        className: "timestamp devtools-monospace",
        title: l10n.dateString(timestamp),
      },
      l10n.timestampString(timestamp)
    );
  }

  renderErrorState() {
    const newBugUrl =
      "https://bugzilla.mozilla.org/enter_bug.cgi?product=DevTools&component=Console";
    const timestampEl = this.renderTimestamp();

    console.error(
      "Unable to render a console message",
      this.state.error,
      this.state.error.stack
    );

    return dom.div(
      {
        className: "message error message-did-catch",
      },
      timestampEl,
      MessageIcon({ level: "error" }),
      dom.span(
        { className: "message-body-wrapper" },
        dom.span(
          {
            className: "message-flex-body",
          },
          // Add whitespaces for formatting when copying to the clipboard.
          timestampEl ? " " : null,
          dom.span(
            { className: "message-body devtools-monospace" },
            l10n.getStr("webconsole-message-render-error", {
              url: newBugUrl,
            }),
            dom.button(
              {
                className: "devtools-button",
                onClick: () =>
                  navigator.clipboard.writeText(
                    JSON.stringify(
                      {
                        message: this.props.message,
                        error: this.state.error.message,
                        stack: this.state.error.stack,
                      },
                      function (key, value) {
                        if (key === "targetFront") {
                          return null;
                        }

                        // The message can hold one or multiple fronts that we need to serialize
                        if (value?.getGrip) {
                          return value.getGrip();
                        }
                        return value;
                      },
                      2
                    )
                  ),
              },
              l10n.getStr("webconsole-message-render-error-copy-button")
            )
          )
        )
      ),
      dom.br()
    );
  }

  // eslint-disable-next-line complexity
  render() {
    if (this.state && this.state.error) {
      return this.renderErrorState();
    }

    const {
      open,
      collapsible,
      collapseTitle,
      disabled,
      source,
      type,
      level,
      indent,
      inWarningGroup,
      topLevelClasses,
      messageBody,
      frame,
      stacktrace,
      serviceContainer,
      exceptionDocURL,
      messageId,
      notes,
    } = this.props;

    topLevelClasses.push("message", source, type, level);
    if (open) {
      topLevelClasses.push("open");
    }

    if (disabled) {
      topLevelClasses.push("disabled");
    }

    const timestampEl = this.renderTimestamp();
    const icon = this.renderIcon();

    // Figure out if there is an expandable part to the message.
    let attachment = null;
    if (this.props.attachment) {
      attachment = this.props.attachment;
    } else if (stacktrace && open) {
      const smartTraceAttributes = {
        stacktrace,
        onViewSourceInDebugger:
          serviceContainer.onViewSourceInDebugger ||
          serviceContainer.onViewSource,
        onViewSource: serviceContainer.onViewSource,
        onReady: this.props.maybeScrollToBottom,
        sourceMapURLService: serviceContainer.sourceMapURLService,
      };

      if (serviceContainer.preventStacktraceInitialRenderDelay) {
        smartTraceAttributes.initialRenderDelay = 0;
      }

      attachment = dom.div(
        {
          className: "stacktrace devtools-monospace",
        },
        createElement(SmartTrace, smartTraceAttributes)
      );
    }

    // If there is an expandable part, make it collapsible.
    let collapse = null;
    if (collapsible && !disabled) {
      collapse = createElement(CollapseButton, {
        open,
        title: collapseTitle,
        onClick: this.toggleMessage,
      });
    }

    let notesNodes;
    if (notes) {
      notesNodes = notes.map(note =>
        dom.span(
          { className: "message-flex-body error-note" },
          dom.span(
            { className: "message-body devtools-monospace" },
            "note: " + note.messageBody
          ),
          dom.span(
            { className: "message-location devtools-monospace" },
            note.frame
              ? FrameView({
                  frame: note.frame,
                  onClick: serviceContainer
                    ? serviceContainer.onViewSourceInDebugger ||
                      serviceContainer.onViewSource
                    : undefined,
                  showEmptyPathAsHost: true,
                  sourceMapURLService: serviceContainer
                    ? serviceContainer.sourceMapURLService
                    : undefined,
                })
              : null
          )
        )
      );
    } else {
      notesNodes = [];
    }

    const repeat =
      this.props.repeat && this.props.repeat > 1
        ? createElement(MessageRepeat, { repeat: this.props.repeat })
        : null;

    let onFrameClick;
    if (serviceContainer && frame) {
      if (source === MESSAGE_SOURCE.CSS) {
        onFrameClick =
          serviceContainer.onViewSourceInStyleEditor ||
          serviceContainer.onViewSource;
      } else {
        // Point everything else to debugger, if source not available,
        // it will fall back to view-source.
        onFrameClick =
          serviceContainer.onViewSourceInDebugger ||
          serviceContainer.onViewSource;
      }
    }

    // Configure the location.
    const location = frame
      ? FrameView({
          className: "message-location devtools-monospace",
          frame,
          onClick: onFrameClick,
          showEmptyPathAsHost: true,
          sourceMapURLService: serviceContainer
            ? serviceContainer.sourceMapURLService
            : undefined,
          messageSource: source,
        })
      : null;

    let learnMore;
    if (exceptionDocURL) {
      const isMdnUrl = exceptionDocURL.startsWith(
        "https://developer.mozilla.org"
      );
      learnMore = dom.a(
        {
          className: `webconsole-learn-more-link${
            isMdnUrl ? " learn-more-link mdn-link" : ""
          }`,
          href: exceptionDocURL,
          title: exceptionDocURL.split("?")[0],
          onClick: this.onLearnMoreClick,
        },
        l10n.getStr("webconsole-learn-more")
      );
    }

    const bodyElements = Array.isArray(messageBody)
      ? messageBody
      : [messageBody];

    return dom.div(
      {
        className: topLevelClasses.join(" "),
        onContextMenu: this.onContextMenu,
        ref: node => {
          this.messageNode = node;
        },
        "data-message-id": messageId,
        "data-indent": indent || 0,
        "aria-live": type === MESSAGE_TYPE.COMMAND ? "off" : "polite",
      },
      timestampEl,
      MessageIndent({
        indent,
        inWarningGroup,
      }),
      this.props.isBlockedNetworkMessage ? collapse : icon,
      this.props.isBlockedNetworkMessage ? icon : collapse,
      dom.span(
        { className: "message-body-wrapper" },
        dom.span(
          {
            className: "message-flex-body",
            onClick: collapsible ? this.toggleMessage : undefined,
          },
          // Add whitespaces for formatting when copying to the clipboard.
          timestampEl ? " " : null,
          dom.span(
            { className: "message-body devtools-monospace" },
            ...bodyElements,
            learnMore ? " " : null,
            learnMore
          ),
          repeat ? " " : null,
          repeat,
          " ",
          location
        ),
        attachment,
        ...notesNodes
      ),
      // If an attachment is displayed, the final newline is handled by the attachment.
      attachment ? null : dom.br()
    );
  }
}

module.exports = Message;
