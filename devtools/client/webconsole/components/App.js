/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const actions = require("resource://devtools/client/webconsole/actions/index.js");
const {
  FILTERBAR_DISPLAY_MODES,
} = require("resource://devtools/client/webconsole/constants.js");

// We directly require Components that we know are going to be used right away
const ConsoleOutput = createFactory(
  require("resource://devtools/client/webconsole/components/Output/ConsoleOutput.js")
);
const FilterBar = createFactory(
  require("resource://devtools/client/webconsole/components/FilterBar/FilterBar.js")
);
const ReverseSearchInput = createFactory(
  require("resource://devtools/client/webconsole/components/Input/ReverseSearchInput.js")
);
const JSTerm = createFactory(
  require("resource://devtools/client/webconsole/components/Input/JSTerm.js")
);
const ConfirmDialog = createFactory(
  require("resource://devtools/client/webconsole/components/Input/ConfirmDialog.js")
);
const EagerEvaluation = createFactory(
  require("resource://devtools/client/webconsole/components/Input/EagerEvaluation.js")
);
const EvaluationNotification = createFactory(
  require("resource://devtools/client/webconsole/components/Input/EvaluationNotification.js")
);

// And lazy load the ones that may not be used.
loader.lazyGetter(this, "SideBar", () =>
  createFactory(
    require("resource://devtools/client/webconsole/components/SideBar.js")
  )
);

loader.lazyGetter(this, "EditorToolbar", () =>
  createFactory(
    require("resource://devtools/client/webconsole/components/Input/EditorToolbar.js")
  )
);

loader.lazyGetter(this, "NotificationBox", () =>
  createFactory(
    require("resource://devtools/client/shared/components/NotificationBox.js")
      .NotificationBox
  )
);
loader.lazyRequireGetter(
  this,
  ["getNotificationWithValue", "PriorityLevels"],
  "resource://devtools/client/shared/components/NotificationBox.js",
  true
);

loader.lazyGetter(this, "GridElementWidthResizer", () =>
  createFactory(
    require("resource://devtools/client/shared/components/splitter/GridElementWidthResizer.js")
  )
);

loader.lazyGetter(this, "ChromeDebugToolbar", () =>
  createFactory(
    require("resource://devtools/client/framework/components/ChromeDebugToolbar.js")
  )
);

const l10n = require("resource://devtools/shared/webconsole/l10n.js");
const {
  Utils: WebConsoleUtils,
} = require("resource://devtools/client/webconsole/utils.js");

const SELF_XSS_OK = l10n.getStr("webconsole-selfxss-ok-string");
const SELF_XSS_MSG = l10n.getStr("webconsole-selfxss-message", {
  okString: SELF_XSS_OK,
});

const {
  getAllNotifications,
} = require("resource://devtools/client/webconsole/selectors/notifications.js");
const { div } = dom;
const isMacOS = Services.appinfo.OS === "Darwin";

/**
 * Console root Application component.
 */
class App extends Component {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      webConsoleUI: PropTypes.object.isRequired,
      notifications: PropTypes.object,
      onFirstMeaningfulPaint: PropTypes.func.isRequired,
      serviceContainer: PropTypes.object.isRequired,
      closeSplitConsole: PropTypes.func.isRequired,
      autocomplete: PropTypes.bool,
      currentReverseSearchEntry: PropTypes.string,
      reverseSearchInputVisible: PropTypes.bool,
      reverseSearchInitialValue: PropTypes.string,
      editorMode: PropTypes.bool,
      editorWidth: PropTypes.number,
      inputEnabled: PropTypes.bool,
      sidebarVisible: PropTypes.bool.isRequired,
      eagerEvaluationEnabled: PropTypes.bool.isRequired,
      filterBarDisplayMode: PropTypes.oneOf([
        ...Object.values(FILTERBAR_DISPLAY_MODES),
      ]).isRequired,
    };
  }

  #abortController = new AbortController();

  constructor(props) {
    super(props);

    this.onClick = this.onClick.bind(this);
    this.onPaste = this.onPaste.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onBlur = this.onBlur.bind(this);
  }

  componentDidMount() {
    window.addEventListener("blur", this.onBlur, {
      signal: this.#abortController.signal,
    });
  }

  componentWillUnmount() {
    this.#abortController.abort();
  }

  onBlur() {
    this.props.dispatch(actions.autocompleteClear());
  }

  onKeyDown(event) {
    const { dispatch, webConsoleUI } = this.props;

    if (
      (!isMacOS && event.key === "F9") ||
      (isMacOS && event.key === "r" && event.ctrlKey === true)
    ) {
      const initialValue =
        webConsoleUI.jsterm && webConsoleUI.jsterm.editor.getSelectedText();

      dispatch(
        actions.reverseSearchInputToggle({ initialValue, access: "keyboard" })
      );
      event.stopPropagation();
      // Prevent Reader Mode to be enabled (See Bug 1682340)
      event.preventDefault();
    }

    if (
      event.key.toLowerCase() === "b" &&
      ((isMacOS && event.metaKey) || (!isMacOS && event.ctrlKey))
    ) {
      event.stopPropagation();
      event.preventDefault();
      dispatch(actions.editorToggle());
    }
  }

  onClick(event) {
    const target = event.originalTarget || event.target;
    const { reverseSearchInputVisible, dispatch, webConsoleUI } = this.props;

    if (
      reverseSearchInputVisible === true &&
      !target.closest(".reverse-search")
    ) {
      event.preventDefault();
      event.stopPropagation();
      dispatch(actions.reverseSearchInputToggle());
      return;
    }

    // Do not focus on middle/right-click or 2+ clicks.
    if (event.detail !== 1 || event.button !== 0) {
      return;
    }

    // Do not focus if a link was clicked
    if (target.closest("a")) {
      return;
    }

    // Do not focus if an input field was clicked
    if (target.closest("input")) {
      return;
    }

    // Do not focus if the click happened in the reverse search toolbar.
    if (target.closest(".reverse-search")) {
      return;
    }

    // Do not focus if something other than the output region was clicked
    // (including e.g. the clear messages button in toolbar)
    if (!target.closest(".webconsole-app")) {
      return;
    }

    // Do not focus if something is selected
    const selection = webConsoleUI.document.defaultView.getSelection();
    if (selection && !selection.isCollapsed) {
      return;
    }

    if (webConsoleUI?.jsterm) {
      webConsoleUI.jsterm.focus();
    }
  }

  onPaste(event) {
    const { dispatch, webConsoleUI, notifications } = this.props;

    const { usageCount, CONSOLE_ENTRY_THRESHOLD } = WebConsoleUtils;

    // Bail out if self-xss notification is suppressed.
    if (
      webConsoleUI.isBrowserConsole ||
      usageCount >= CONSOLE_ENTRY_THRESHOLD
    ) {
      return;
    }

    // Stop event propagation, so the clipboard content is *not* inserted.
    event.preventDefault();
    event.stopPropagation();

    // Bail out if self-xss notification is already there.
    if (getNotificationWithValue(notifications, "selfxss-notification")) {
      return;
    }

    const input = event.target;

    // Cleanup function if notification is closed by the user.
    const removeCallback = eventType => {
      if (eventType == "removed") {
        input.removeEventListener("keyup", pasteKeyUpHandler);
        dispatch(actions.removeNotification("selfxss-notification"));
      }
    };

    // Create self-xss notification
    dispatch(
      actions.appendNotification(
        SELF_XSS_MSG,
        "selfxss-notification",
        null,
        PriorityLevels.PRIORITY_WARNING_HIGH,
        null,
        removeCallback
      )
    );

    // Remove notification automatically when the user types "allow pasting".
    const pasteKeyUpHandler = e => {
      const { value } = e.target;
      if (value.includes(SELF_XSS_OK)) {
        dispatch(actions.removeNotification("selfxss-notification"));
        input.removeEventListener("keyup", pasteKeyUpHandler);
        WebConsoleUtils.usageCount = WebConsoleUtils.CONSOLE_ENTRY_THRESHOLD;
      }
    };

    input.addEventListener("keyup", pasteKeyUpHandler, {
      signal: this.#abortController.signal,
    });
  }

  renderChromeDebugToolbar() {
    const { webConsoleUI } = this.props;
    if (!webConsoleUI.isBrowserConsole) {
      return null;
    }
    return ChromeDebugToolbar({
      // This should always be true at this point
      isBrowserConsole: webConsoleUI.isBrowserConsole,
    });
  }

  renderFilterBar() {
    const {
      closeSplitConsole,
      filterBarDisplayMode,
      serviceContainer,
      webConsoleUI,
    } = this.props;

    return FilterBar({
      key: "filterbar",
      closeSplitConsole,
      displayMode: filterBarDisplayMode,
      serviceContainer,
      webConsoleUI,
    });
  }

  renderEditorToolbar() {
    const {
      editorMode,
      dispatch,
      reverseSearchInputVisible,
      serviceContainer,
      webConsoleUI,
      inputEnabled,
    } = this.props;

    if (!inputEnabled) {
      return null;
    }

    return editorMode
      ? EditorToolbar({
          key: "editor-toolbar",
          editorMode,
          dispatch,
          reverseSearchInputVisible,
          serviceContainer,
          webConsoleUI,
        })
      : null;
  }

  renderConsoleOutput() {
    const { onFirstMeaningfulPaint, serviceContainer, editorMode } = this.props;

    return ConsoleOutput({
      key: "console-output",
      serviceContainer,
      onFirstMeaningfulPaint,
      editorMode,
    });
  }

  renderJsTerm() {
    const {
      webConsoleUI,
      serviceContainer,
      autocomplete,
      editorMode,
      editorWidth,
      inputEnabled,
    } = this.props;

    return JSTerm({
      key: "jsterm",
      webConsoleUI,
      serviceContainer,
      onPaste: this.onPaste,
      autocomplete,
      editorMode,
      editorWidth,
      inputEnabled,
    });
  }

  renderEagerEvaluation() {
    const { eagerEvaluationEnabled, serviceContainer, inputEnabled } =
      this.props;

    if (!eagerEvaluationEnabled || !inputEnabled) {
      return null;
    }

    return EagerEvaluation({ serviceContainer });
  }

  renderReverseSearch() {
    const { serviceContainer, reverseSearchInitialValue } = this.props;

    return ReverseSearchInput({
      key: "reverse-search-input",
      setInputValue: serviceContainer.setInputValue,
      focusInput: serviceContainer.focusInput,
      initialValue: reverseSearchInitialValue,
    });
  }

  renderSideBar() {
    const { serviceContainer, sidebarVisible } = this.props;
    return sidebarVisible
      ? SideBar({
          key: "sidebar",
          serviceContainer,
          visible: sidebarVisible,
        })
      : null;
  }

  renderNotificationBox() {
    const { notifications, editorMode } = this.props;

    return notifications && notifications.size > 0
      ? NotificationBox({
          id: "webconsole-notificationbox",
          key: "notification-box",
          displayBorderTop: !editorMode,
          displayBorderBottom: editorMode,
          wrapping: true,
          notifications,
        })
      : null;
  }

  renderConfirmDialog() {
    const { webConsoleUI, serviceContainer } = this.props;

    return ConfirmDialog({
      webConsoleUI,
      serviceContainer,
      key: "confirm-dialog",
    });
  }

  renderRootElement(children) {
    const { editorMode, sidebarVisible, inputEnabled, eagerEvaluationEnabled } =
      this.props;

    const classNames = ["webconsole-app"];
    if (sidebarVisible) {
      classNames.push("sidebar-visible");
    }
    if (editorMode && inputEnabled) {
      classNames.push("jsterm-editor");
    }

    if (eagerEvaluationEnabled && inputEnabled) {
      classNames.push("eager-evaluation");
    }

    return div(
      {
        className: classNames.join(" "),
        onKeyDown: this.onKeyDown,
        onClick: this.onClick,
        ref: node => {
          this.node = node;
        },
      },
      children
    );
  }

  render() {
    const { webConsoleUI, editorMode, dispatch, inputEnabled } = this.props;

    const chromeDebugToolbar = this.renderChromeDebugToolbar();
    const filterBar = this.renderFilterBar();
    const editorToolbar = this.renderEditorToolbar();
    const consoleOutput = this.renderConsoleOutput();
    const notificationBox = this.renderNotificationBox();
    const jsterm = this.renderJsTerm();
    const eager = this.renderEagerEvaluation();
    const evaluationNotification = EvaluationNotification();
    const reverseSearch = this.renderReverseSearch();
    const sidebar = this.renderSideBar();
    const confirmDialog = this.renderConfirmDialog();

    return this.renderRootElement([
      chromeDebugToolbar,
      filterBar,
      editorToolbar,
      dom.div(
        { className: "flexible-output-input", key: "in-out-container" },
        consoleOutput,
        notificationBox,
        jsterm,
        eager,
        evaluationNotification
      ),
      editorMode && inputEnabled
        ? GridElementWidthResizer({
            key: "editor-resizer",
            enabled: editorMode,
            position: "end",
            className: "editor-resizer",
            getControlledElementNode: () => webConsoleUI.jsterm.node,
            onResizeEnd: width => dispatch(actions.setEditorWidth(width)),
          })
        : null,
      reverseSearch,
      sidebar,
      confirmDialog,
    ]);
  }
}

const mapStateToProps = state => ({
  notifications: getAllNotifications(state),
  reverseSearchInputVisible: state.ui.reverseSearchInputVisible,
  reverseSearchInitialValue: state.ui.reverseSearchInitialValue,
  editorMode: state.ui.editor,
  editorWidth: state.ui.editorWidth,
  sidebarVisible: state.ui.sidebarVisible,
  filterBarDisplayMode: state.ui.filterBarDisplayMode,
  eagerEvaluationEnabled: state.prefs.eagerEvaluation,
  autocomplete: state.prefs.autocomplete,
});

const mapDispatchToProps = dispatch => ({
  dispatch,
});

module.exports = connect(mapStateToProps, mapDispatchToProps)(App);
