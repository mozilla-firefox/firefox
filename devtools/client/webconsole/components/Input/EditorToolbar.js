/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const EvaluationContextSelector = createFactory(
  require("resource://devtools/client/webconsole/components/Input/EvaluationContextSelector.js")
);

const actions = require("resource://devtools/client/webconsole/actions/index.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const isMacOS = Services.appinfo.OS === "Darwin";

// Constants used for defining the direction of JSTerm input history navigation.
const {
  HISTORY_BACK,
  HISTORY_FORWARD,
} = require("resource://devtools/client/webconsole/constants.js");

class EditorToolbar extends Component {
  static get propTypes() {
    return {
      editorMode: PropTypes.bool,
      dispatch: PropTypes.func.isRequired,
      reverseSearchInputVisible: PropTypes.bool.isRequired,
      serviceContainer: PropTypes.object.isRequired,
      webConsoleUI: PropTypes.object.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.onReverseSearchButtonClick =
      this.onReverseSearchButtonClick.bind(this);
  }

  onReverseSearchButtonClick(event) {
    const { dispatch, serviceContainer } = this.props;

    event.stopPropagation();
    dispatch(
      actions.reverseSearchInputToggle({
        initialValue: serviceContainer.getInputSelection(),
        access: "editor-toolbar-icon",
      })
    );
  }

  renderEvaluationContextSelector() {
    return EvaluationContextSelector({
      webConsoleUI: this.props.webConsoleUI,
    });
  }

  render() {
    const { editorMode, dispatch, reverseSearchInputVisible, webConsoleUI } =
      this.props;

    if (!editorMode) {
      return null;
    }

    const enterStr = l10n.getStr("webconsole-enter-key");

    return dom.div(
      {
        className:
          "devtools-toolbar devtools-input-toolbar webconsole-editor-toolbar",
      },
      dom.button(
        {
          className: "devtools-button webconsole-editor-toolbar-executeButton",
          title: l10n.getStr(
            "webconsole-editor-toolbar-execute-button-tooltip",
            {
              shortcut: isMacOS ? `Cmd + ${enterStr}` : `Ctrl + ${enterStr}`,
            }
          ),
          onClick: () => dispatch(actions.evaluateExpression()),
        },
        l10n.getStr("webconsole-editor-toolbar-execute-button-label")
      ),
      this.renderEvaluationContextSelector(),
      dom.button({
        className:
          "devtools-button webconsole-editor-toolbar-prettyPrintButton",
        title: l10n.getStr(
          "webconsole-editor-toolbar-pretty-print-button-tooltip"
        ),
        onClick: () => dispatch(actions.prettyPrintEditor()),
      }),
      dom.div({
        className:
          "devtools-separator webconsole-editor-toolbar-prettyPrintSeparator",
      }),
      dom.button({
        className:
          "devtools-button webconsole-editor-toolbar-history-prevExpressionButton",
        title: l10n.getStr(
          "webconsole-editor-toolbar-prev-expression-button-tooltip"
        ),
        onClick: () => {
          webConsoleUI.jsterm.historyPeruse(HISTORY_BACK);
        },
      }),
      dom.button({
        className:
          "devtools-button webconsole-editor-toolbar-history-nextExpressionButton",
        title: l10n.getStr(
          "webconsole-editor-toolbar-next-expression-button-tooltip"
        ),
        onClick: () => {
          webConsoleUI.jsterm.historyPeruse(HISTORY_FORWARD);
        },
      }),
      dom.button({
        className: `devtools-button webconsole-editor-toolbar-reverseSearchButton ${
          reverseSearchInputVisible ? "checked" : ""
        }`,
        title: reverseSearchInputVisible
          ? l10n.getStr(
              "webconsole-editor-toolbar-close-reverse-search-tooltip",
              { shortcut: "Esc" + (isMacOS ? " | Ctrl + C" : "") }
            )
          : l10n.getStr(
              "webconsole-editor-toolbar-open-reverse-search-tooltip",
              { shortcut: isMacOS ? "Ctrl + R" : "F9" }
            ),
        onClick: this.onReverseSearchButtonClick,
      }),
      dom.div({
        className:
          "devtools-separator webconsole-editor-toolbar-historyNavSeparator",
      }),
      dom.button({
        className: "devtools-button webconsole-editor-toolbar-closeButton",
        title: l10n.getStr("webconsole-editor-toolbar-close-button-tooltip", {
          shortcut: isMacOS ? "Cmd + B" : "Ctrl + B",
        }),
        onClick: () => dispatch(actions.editorToggle()),
      })
    );
  }
}

module.exports = EditorToolbar;
