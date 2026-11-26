/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const actions = require("resource://devtools/client/webconsole/actions/index.js");

const Message = createFactory(
  require("resource://devtools/client/webconsole/components/Output/Message.js")
);

loader.lazyRequireGetter(
  this,
  "GripMessageBody",
  "resource://devtools/client/webconsole/components/Output/GripMessageBody.js"
);

/**
 * This component is responsible for rendering CSS warnings in the Console panel.
 *
 * CSS warnings are expandable when they have associated CSS selectors so the
 * user can inspect any matching DOM elements. Not all CSS warnings have
 * associated selectors (those that don't are not expandable) and not all
 * selectors match elements in the current page (warnings can appear for styles
 * which don't apply to the current page).
 *
 * @augments Component
 */
class CSSWarning extends Component {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      inWarningGroup: PropTypes.bool.isRequired,
      message: PropTypes.object.isRequired,
      open: PropTypes.bool,
      cssMatchingElements: PropTypes.object,
      repeat: PropTypes.any,
      serviceContainer: PropTypes.object,
      timestampsVisible: PropTypes.bool.isRequired,
      setExpanded: PropTypes.func,
    };
  }

  static get defaultProps() {
    return {
      open: false,
    };
  }

  static get displayName() {
    return "CSSWarning";
  }

  constructor(props) {
    super(props);
    this.onToggle = this.onToggle.bind(this);
  }

  onToggle(messageId) {
    const { dispatch, message, cssMatchingElements, open } = this.props;

    if (open) {
      dispatch(actions.messageClose(messageId));
    } else if (cssMatchingElements) {
      // If the message already has information about the elements matching
      // the selectors associated with this CSS warning, just open the message.
      dispatch(actions.messageOpen(messageId));
    } else {
      // Query the server for elements matching the CSS selectors associated
      // with this CSS warning and populate the message's additional cssMatchingElements with
      // the result. It's an async operation and potentially expensive, so we only do it
      // on demand, once, when the component is first expanded.
      dispatch(actions.messageGetMatchingElements(message));
      dispatch(actions.messageOpen(messageId));
    }
  }

  render() {
    const {
      dispatch,
      message,
      open,
      cssMatchingElements,
      repeat,
      serviceContainer,
      timestampsVisible,
      inWarningGroup,
      setExpanded,
    } = this.props;

    const {
      id: messageId,
      indent,
      cssSelectors,
      source,
      type,
      level,
      messageText,
      frame,
      exceptionDocURL,
      timeStamp,
      notes,
    } = message;

    let messageBody;
    if (typeof messageText === "string") {
      messageBody = messageText;
    } else if (
      typeof messageText === "object" &&
      messageText.type === "longString"
    ) {
      messageBody = `${message.messageText.initial}…`;
    }

    // Create a message attachment only when the message is open and there is a result
    // to the query for elements matching the CSS selectors associated with the message.
    const attachment =
      open &&
      cssMatchingElements !== undefined &&
      dom.div(
        { className: "devtools-monospace" },
        dom.div(
          { className: "elements-label" },
          l10n.getStr("webconsole-css-warning-elements", {
            selector: cssSelectors,
          })
        ),
        GripMessageBody({
          dispatch,
          escapeWhitespace: false,
          grip: cssMatchingElements,
          serviceContainer,
          setExpanded,
        })
      );

    return Message({
      attachment,
      collapsible: !!cssSelectors.length,
      dispatch,
      exceptionDocURL,
      frame,
      indent,
      inWarningGroup,
      level,
      messageBody,
      messageId,
      notes,
      open,
      onToggle: this.onToggle,
      repeat,
      serviceContainer,
      source,
      timeStamp,
      timestampsVisible,
      topLevelClasses: [],
      type,
      message,
    });
  }
}

module.exports = createFactory(CSSWarning);
