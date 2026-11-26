/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// React & Redux
const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

// Actions
const actions = require("resource://devtools/client/webconsole/actions/index.js");

// Selectors
const {
  getAllFilters,
} = require("resource://devtools/client/webconsole/selectors/filters.js");
const {
  getFilteredMessagesCount,
} = require("resource://devtools/client/webconsole/selectors/messages.js");
const {
  getAllPrefs,
} = require("resource://devtools/client/webconsole/selectors/prefs.js");
const {
  getAllUi,
} = require("resource://devtools/client/webconsole/selectors/ui.js");

// Utilities
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const KeyShortcuts = require("resource://devtools/client/shared/key-shortcuts.js");

// Constants
const {
  FILTERS,
  FILTERBAR_DISPLAY_MODES,
} = require("resource://devtools/client/webconsole/constants.js");

// Additional Components
const FilterButton = require("resource://devtools/client/webconsole/components/FilterBar/FilterButton.js");
const ConsoleSettings = createFactory(
  require("resource://devtools/client/webconsole/components/FilterBar/ConsoleSettings.js")
);
const SearchBox = createFactory(
  require("resource://devtools/client/shared/components/SearchBox.js")
);

loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);

const disabledCssFilterButtonTitle = l10n.getStr(
  "webconsole-filter-button-css-inactive-tooltip"
);

class FilterBar extends Component {
  static get propTypes() {
    return {
      closeButtonVisible: PropTypes.bool,
      closeSplitConsole: PropTypes.func,
      dispatch: PropTypes.func.isRequired,
      displayMode: PropTypes.oneOf([...Object.values(FILTERBAR_DISPLAY_MODES)])
        .isRequired,
      enableNetworkMonitoring: PropTypes.bool.isRequired,
      filter: PropTypes.object.isRequired,
      filteredMessagesCount: PropTypes.object.isRequired,
      groupSimilar: PropTypes.bool.isRequired,
      persistLogs: PropTypes.bool.isRequired,
      eagerEvaluation: PropTypes.bool.isRequired,
      timestampsVisible: PropTypes.bool.isRequired,
      serviceContainer: PropTypes.object.isRequired,
      webConsoleUI: PropTypes.object.isRequired,
      autocomplete: PropTypes.bool.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.renderFiltersConfigBar = this.renderFiltersConfigBar.bind(this);
    this.maybeUpdateLayout = this.maybeUpdateLayout.bind(this);
    this.resizeObserver = new ResizeObserver(this.maybeUpdateLayout);
  }

  componentDidMount() {
    this.filterInputMinWidth = 150;
    try {
      const filterInput = this.wrapperNode.querySelector(".devtools-searchbox");
      this.filterInputMinWidth = Number(
        window.getComputedStyle(filterInput)["min-width"].replace("px", "")
      );
    } catch (e) {
      // If the min-width of the filter input isn't set, or is set in a different unit
      // than px.
      console.error("min-width of the filter input couldn't be retrieved.", e);
    }

    this.maybeUpdateLayout();
    this.resizeObserver.observe(this.wrapperNode);
  }

  shouldComponentUpdate(nextProps) {
    const {
      closeButtonVisible,
      displayMode,
      enableNetworkMonitoring,
      filter,
      filteredMessagesCount,
      groupSimilar,
      persistLogs,
      timestampsVisible,
      eagerEvaluation,
      autocomplete,
    } = this.props;

    if (
      nextProps.closeButtonVisible !== closeButtonVisible ||
      nextProps.displayMode !== displayMode ||
      nextProps.enableNetworkMonitoring !== enableNetworkMonitoring ||
      nextProps.filter !== filter ||
      nextProps.groupSimilar !== groupSimilar ||
      nextProps.persistLogs !== persistLogs ||
      nextProps.timestampsVisible !== timestampsVisible ||
      nextProps.eagerEvaluation !== eagerEvaluation ||
      nextProps.autocomplete !== autocomplete
    ) {
      return true;
    }

    if (
      JSON.stringify(nextProps.filteredMessagesCount) !==
      JSON.stringify(filteredMessagesCount)
    ) {
      return true;
    }

    return false;
  }

  /**
   * Update the boolean state that informs where the filter buttons should be rendered.
   * If the filter buttons are rendered inline with the filter input and the filter
   * input width is reduced below a threshold, the filter buttons are rendered on a new
   * row. When the filter buttons are on a separate row and the filter input grows
   * wide enough to display the filter buttons without dropping below the threshold,
   * the filter buttons are rendered inline.
   */
  maybeUpdateLayout() {
    const { dispatch, displayMode } = this.props;

    // If we don't have the wrapperNode reference, or if the wrapperNode isn't connected
    // anymore, we disconnect the resize observer (componentWillUnmount is never called
    // on this component, so we have to do it here).
    if (!this.wrapperNode || !this.wrapperNode.isConnected) {
      this.resizeObserver.disconnect();
      return;
    }

    const filterInput = this.wrapperNode.querySelector(".devtools-searchbox");
    const { width: filterInputWidth } = filterInput.getBoundingClientRect();

    if (displayMode === FILTERBAR_DISPLAY_MODES.WIDE) {
      if (filterInputWidth <= this.filterInputMinWidth) {
        dispatch(
          actions.filterBarDisplayModeSet(FILTERBAR_DISPLAY_MODES.NARROW)
        );
      }

      return;
    }

    if (displayMode === FILTERBAR_DISPLAY_MODES.NARROW) {
      const filterButtonsToolbar = this.wrapperNode.querySelector(
        ".webconsole-filterbar-secondary"
      );

      const buttonMargin = 5;
      const filterButtonsToolbarWidth = Array.from(
        filterButtonsToolbar.children
      ).reduce(
        (width, el) => width + el.getBoundingClientRect().width + buttonMargin,
        0
      );

      if (
        filterInputWidth - this.filterInputMinWidth >
        filterButtonsToolbarWidth
      ) {
        dispatch(actions.filterBarDisplayModeSet(FILTERBAR_DISPLAY_MODES.WIDE));
      }
    }
  }

  renderSeparator() {
    return dom.div({
      className: "devtools-separator",
    });
  }

  renderClearButton() {
    return dom.button({
      className: "devtools-button devtools-clear-icon",
      title: l10n.getStr("webconsole-clear-button-tooltip", {
        shortcut: this.props.webConsoleUI
          .getClearKeyShortcuts()
          .map(electronKey =>
            KeyShortcuts.stringifyFromElectronKey(electronKey)
          )
          .join(", "),
      }),
      onClick: () => this.props.dispatch(actions.messagesClear()),
    });
  }

  renderFiltersConfigBar() {
    const { dispatch, filter, filteredMessagesCount, serviceContainer } =
      this.props;

    const { isBrowserConsoleOrBrowserToolbox } = serviceContainer;

    const getLabel = (baseLabel, filterKey) => {
      const count = filteredMessagesCount[filterKey];
      if (filter[filterKey] || count === 0) {
        return baseLabel;
      }
      return `${baseLabel} (${count})`;
    };

    return dom.div(
      {
        className: "devtools-toolbar webconsole-filterbar-secondary",
        key: "config-bar",
      },
      FilterButton({
        active: filter[FILTERS.ERROR],
        label: getLabel(
          l10n.getStr("webconsole-filter-button-errors"),
          FILTERS.ERROR
        ),
        filterKey: FILTERS.ERROR,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.WARN],
        label: getLabel(
          l10n.getStr("webconsole-filter-button-warnings"),
          FILTERS.WARN
        ),
        filterKey: FILTERS.WARN,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.INFO],
        label: getLabel(
          l10n.getStr("webconsole-filter-button-info"),
          FILTERS.INFO
        ),
        filterKey: FILTERS.INFO,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.LOG],
        label: getLabel(
          l10n.getStr("webconsole-filter-button-logs"),
          FILTERS.LOG
        ),
        filterKey: FILTERS.LOG,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.DEBUG],
        label: getLabel(
          l10n.getStr("webconsole-filter-button-debug"),
          FILTERS.DEBUG
        ),
        filterKey: FILTERS.DEBUG,
        dispatch,
      }),
      dom.div({
        className: "devtools-separator",
      }),
      FilterButton({
        active: filter[FILTERS.CSS],
        title: filter[FILTERS.CSS] ? undefined : disabledCssFilterButtonTitle,
        label: l10n.getStr("webconsole-filter-button-css"),
        filterKey: FILTERS.CSS,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.NETXHR],
        label: l10n.getStr("webconsole-filter-button-xhr"),
        filterKey: FILTERS.NETXHR,
        dispatch,
      }),
      FilterButton({
        active: filter[FILTERS.NET],
        label: l10n.getStr("webconsole-filter-button-requests"),
        filterKey: FILTERS.NET,
        dispatch,
      }),
      // The browser console and browser toolbox console aggregate messages from
      // the browser itself (privileged code) and from web content. Let the user
      // show or hide each origin separately.
      isBrowserConsoleOrBrowserToolbox &&
        dom.div({
          className: "devtools-separator",
        }),
      isBrowserConsoleOrBrowserToolbox &&
        FilterButton({
          active: filter[FILTERS.CHROME],
          label: getLabel(
            l10n.getStr("webconsole-filter-button-chrome"),
            FILTERS.CHROME
          ),
          title: l10n.getStr("webconsole-filter-button-chrome-tooltip"),
          filterKey: FILTERS.CHROME,
          dispatch,
        }),
      isBrowserConsoleOrBrowserToolbox &&
        FilterButton({
          active: filter[FILTERS.CONTENT],
          label: getLabel(
            l10n.getStr("webconsole-filter-button-content"),
            FILTERS.CONTENT
          ),
          title: l10n.getStr("webconsole-filter-button-content-tooltip"),
          filterKey: FILTERS.CONTENT,
          dispatch,
        })
    );
  }

  renderSearchBox() {
    const { dispatch, filteredMessagesCount } = this.props;

    // We want the summary to always be announced to screen reader, even if there are
    // no filtered out messages.
    // We'll hide the "0 hidden" summary when the input field is not focused.
    const searchBoxSummary = l10n.getStr(
      "webconsole-filtered-messages-by-text",
      {
        count: filteredMessagesCount.text,
      }
    );

    const searchBoxSummaryTooltip = l10n.getStr(
      "webconsole-filtered-messages-by-text-tooltip",
      { count: filteredMessagesCount.text }
    );

    return SearchBox({
      type: "filter",
      placeholder: l10n.getStr("webconsole-filter-input-placeholder"),
      keyShortcut: l10n.getStr("webconsole-find-key"),
      onChange: text => dispatch(actions.filterTextSet(text)),
      summary: searchBoxSummary,
      summaryTooltip: searchBoxSummaryTooltip,
      summaryId: "devtools-console-output-filter-summary",
    });
  }

  renderSettingsButton() {
    const {
      dispatch,
      enableNetworkMonitoring,
      eagerEvaluation,
      groupSimilar,
      persistLogs,
      timestampsVisible,
      webConsoleUI,
      autocomplete,
    } = this.props;

    return ConsoleSettings({
      dispatch,
      enableNetworkMonitoring,
      eagerEvaluation,
      groupSimilar,
      persistLogs,
      timestampsVisible,
      webConsoleUI,
      autocomplete,
    });
  }

  renderCloseButton() {
    const { closeSplitConsole } = this.props;

    return dom.div(
      {
        className: "devtools-toolbar split-console-close-button-wrapper",
        key: "wrapper",
      },
      dom.button({
        id: "split-console-close-button",
        key: "split-console-close-button",
        className: "devtools-button",
        title: l10n.getStr("webconsole-close-split-console-button-tooltip"),
        onClick: () => {
          closeSplitConsole();
        },
      })
    );
  }

  render() {
    const { closeButtonVisible, displayMode, filteredMessagesCount } =
      this.props;

    const isNarrow = displayMode === FILTERBAR_DISPLAY_MODES.NARROW;
    const isWide = displayMode === FILTERBAR_DISPLAY_MODES.WIDE;

    const separator = this.renderSeparator();
    const clearButton = this.renderClearButton();
    const searchBox = this.renderSearchBox();
    const filtersConfigBar = this.renderFiltersConfigBar();
    const settingsButton = this.renderSettingsButton();

    const children = [
      dom.div(
        {
          className:
            "devtools-toolbar devtools-input-toolbar webconsole-filterbar-primary",
          key: "primary-bar",
          "data-has-filtered-by-text": filteredMessagesCount.text > 0,
        },
        clearButton,
        separator,
        searchBox,
        isWide && separator,
        isWide && filtersConfigBar,
        separator,
        settingsButton
      ),
    ];

    if (closeButtonVisible) {
      children.push(this.renderCloseButton());
    }

    if (isNarrow) {
      children.push(filtersConfigBar);
    }

    return dom.div(
      {
        className: `webconsole-filteringbar-wrapper ${displayMode}`,
        "aria-live": "off",
        ref: node => {
          this.wrapperNode = node;
        },
      },
      children
    );
  }
}

function mapStateToProps(state) {
  const uiState = getAllUi(state);
  const prefsState = getAllPrefs(state);
  return {
    closeButtonVisible: uiState.closeButtonVisible,
    filter: getAllFilters(state),
    filteredMessagesCount: getFilteredMessagesCount(state),
    groupSimilar: prefsState.groupSimilar,
    persistLogs: uiState.persistLogs,
    eagerEvaluation: prefsState.eagerEvaluation,
    timestampsVisible: uiState.timestampsVisible,
    autocomplete: prefsState.autocomplete,
    enableNetworkMonitoring: uiState.enableNetworkMonitoring,
  };
}

module.exports = connect(mapStateToProps)(FilterBar);
