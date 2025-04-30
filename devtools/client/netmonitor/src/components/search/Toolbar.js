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
const {
  FILTER_SEARCH_DELAY,
} = require("resource://devtools/client/netmonitor/src/constants.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const Actions = require("resource://devtools/client/netmonitor/src/actions/index.js");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const { button, span, div } = dom;

// Components
const SearchBox = createFactory(
  require("resource://devtools/client/shared/components/SearchBox.js")
);

/**
 * Network Search toolbar component.
 *
 * Provides tools for greater control over search.
 */
class Toolbar extends Component {
  static get propTypes() {
    return {
      searchboxRef: PropTypes.object.isRequired,
      clearSearchResults: PropTypes.func.isRequired,
      search: PropTypes.func.isRequired,
      closeSearch: PropTypes.func.isRequired,
      addSearchQuery: PropTypes.func.isRequired,
      clearSearchResultAndCancel: PropTypes.func.isRequired,
      caseSensitive: PropTypes.bool.isRequired,
      toggleCaseSensitiveSearch: PropTypes.func.isRequired,
      connector: PropTypes.object.isRequired,
      query: PropTypes.string,
    };
  }

  /**
   * Render a separator.
   */
  renderSeparator() {
    return span({ className: "devtools-separator" });
  }

  /**
   * Handles what we do when key is pressed in search input.
   * @param event
   * @param conn
   */
  onKeyDown(event, connector) {
    switch (event.key) {
      case "Escape":
        event.preventDefault();
        this.props.closeSearch();
        break;
      case "Enter":
        event.preventDefault();
        this.props.addSearchQuery(event.target.value);
        this.props.search(connector, event.target.value);
        break;
    }
  }

  renderModifiers() {
    return div(
      { className: "search-modifiers" },
      span({ className: "pipe-divider" }),
      this.renderCaseSensitiveButton()
    );
  }

  /**
   * Render a clear button to clear search results.
   */
  renderClearButton() {
    return button({
      className:
        "devtools-button devtools-clear-icon ws-frames-list-clear-button",
      title: L10N.getStr("netmonitor.search.toolbar.clear"),
      onClick: () => {
        this.props.clearSearchResults();
      },
    });
  }

  /**
   * Render the case sensitive search modifier button
   */
  renderCaseSensitiveButton() {
    const { caseSensitive, toggleCaseSensitiveSearch } = this.props;
    const active = caseSensitive ? "checked" : "";

    return button({
      id: "devtools-network-search-caseSensitive",
      className: `devtools-button ${active}`,
      title: L10N.getStr("netmonitor.search.toolbar.caseSensitive"),
      onClick: toggleCaseSensitiveSearch,
    });
  }

  /**
   * Render Search box.
   */
  renderFilterBox() {
    const { addSearchQuery, clearSearchResultAndCancel, connector, query } =
      this.props;
    return SearchBox({
      keyShortcut: "CmdOrCtrl+Shift+F",
      placeholder: L10N.getStr("netmonitor.search.toolbar.inputPlaceholder"),
      type: "search",
      delay: FILTER_SEARCH_DELAY,
      ref: this.props.searchboxRef,
      initialValue: query,
      onClearButtonClick: () => clearSearchResultAndCancel(),
      onChange: newQuery => addSearchQuery(newQuery),
      onKeyDown: event => this.onKeyDown(event, connector),
    });
  }

  render() {
    return div(
      {
        id: "netmonitor-toolbar-container",
        className: "devtools-toolbar devtools-input-toolbar",
      },
      this.renderFilterBox(),
      this.renderModifiers()
    );
  }
}

module.exports = connect(
  state => ({
    caseSensitive: state.search.caseSensitive,
    query: state.search.query,
  }),
  dispatch => ({
    closeSearch: () => dispatch(Actions.closeSearch()),
    openSearch: () => dispatch(Actions.openSearch()),
    clearSearchResultAndCancel: () =>
      dispatch(Actions.clearSearchResultAndCancel()),
    toggleCaseSensitiveSearch: () =>
      dispatch(Actions.toggleCaseSensitiveSearch()),
    search: (connector, query) => dispatch(Actions.search(connector, query)),
    addSearchQuery: query => dispatch(Actions.addSearchQuery(query)),
  })
)(Toolbar);
