/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import React, { Component } from "devtools/client/shared/vendor/react";
import { div } from "devtools/client/shared/vendor/react-dom-factories";
import PropTypes from "devtools/client/shared/vendor/react-prop-types";
import { connect } from "devtools/client/shared/vendor/react-redux";
import { basename } from "../utils/path";
import { createLocation } from "../utils/location";

const fuzzyAldrin = require("resource://devtools/client/shared/vendor/fuzzaldrin-plus.js");
const { throttle } = require("resource://devtools/shared/throttle.js");

import actions from "../actions/index";
import {
  getDisplayedSourcesList,
  getQuickOpenQuery,
  getQuickOpenType,
  getSelectedLocation,
  getSettledSourceTextContent,
  getSourceTabs,
  getBlackBoxRanges,
  getProjectDirectoryRoot,
} from "../selectors/index";
import { memoizeLast } from "../utils/memoizeLast";
import { searchKeys } from "../constants";
import {
  formatSymbol,
  parseLineColumn,
  formatShortcutResults,
  formatSourceForList,
} from "../utils/quick-open";
import Modal from "./shared/Modal";
import SearchInput from "./shared/SearchInput";
import ResultList from "./shared/ResultList";

const maxResults = 100;

const SIZE_BIG = { size: "big" };
const SIZE_DEFAULT = {};

function filter(values, query, key = "value") {
  const preparedQuery = fuzzyAldrin.prepareQuery(query);

  return fuzzyAldrin.filter(values, query, {
    key,
    maxResults,
    preparedQuery,
  });
}

export class QuickOpenModal extends Component {
  // Put it on the class so it can be retrieved in tests
  static UPDATE_RESULTS_THROTTLE = 100;

  #willUnmountCalled = false;

  constructor(props) {
    super(props);
    this.state = { results: null, selectedIndex: 0 };
  }

  static get propTypes() {
    return {
      closeQuickOpen: PropTypes.func.isRequired,
      displayedSources: PropTypes.array.isRequired,
      blackBoxRanges: PropTypes.object.isRequired,
      highlightLineRange: PropTypes.func.isRequired,
      clearHighlightLineRange: PropTypes.func.isRequired,
      query: PropTypes.string.isRequired,
      searchType: PropTypes.oneOf([
        "functions",
        "goto",
        "gotoSource",
        "other",
        "shortcuts",
        "sources",
        "variables",
      ]).isRequired,
      selectSpecificLocation: PropTypes.func.isRequired,
      selectedContentLoaded: PropTypes.bool,
      selectedLocation: PropTypes.object,
      setQuickOpenQuery: PropTypes.func.isRequired,
      openedTabUrls: PropTypes.array.isRequired,
      toggleShortcutsModal: PropTypes.func.isRequired,
      projectDirectoryRoot: PropTypes.string,
      getFunctionSymbols: PropTypes.func.isRequired,
      updateCursorPosition: PropTypes.func.isRequired,
    };
  }

  setResults(results) {
    if (results) {
      results = results.slice(0, maxResults);
    }
    this.setState({ results });
  }

  componentDidMount() {
    const { query, shortcutsModalEnabled, toggleShortcutsModal } = this.props;

    this.updateResults(query);

    if (shortcutsModalEnabled) {
      toggleShortcutsModal();
    }
  }

  componentDidUpdate(prevProps) {
    const queryChanged = prevProps.query !== this.props.query;

    if (queryChanged) {
      this.updateResults(this.props.query);
    }
  }

  componentWillUnmount() {
    this.#willUnmountCalled = true;
  }

  closeModal = () => {
    this.props.closeQuickOpen();
  };

  dropGoto = query => {
    const index = query.indexOf(":");
    return index !== -1 ? query.slice(0, index) : query;
  };

  formatSources = memoizeLast(
    (displayedSources, openedTabUrls, blackBoxRanges, projectDirectoryRoot) => {
      // Note that we should format all displayed sources,
      // the actual filtering will only be done late from `searchSources()`
      return displayedSources.map(source => {
        const isBlackBoxed = !!blackBoxRanges[source.url];
        const hasTabOpened = openedTabUrls.includes(source.url);
        return formatSourceForList(
          source,
          hasTabOpened,
          isBlackBoxed,
          projectDirectoryRoot
        );
      });
    }
  );

  searchSources = query => {
    const {
      displayedSources,
      openedTabUrls,
      blackBoxRanges,
      projectDirectoryRoot,
    } = this.props;

    const sources = this.formatSources(
      displayedSources,
      openedTabUrls,
      blackBoxRanges,
      projectDirectoryRoot
    );
    const results =
      query == "" ? sources : filter(sources, this.dropGoto(query));
    return this.setResults(results);
  };

  searchSymbols = async query => {
    const { getFunctionSymbols, selectedLocation } = this.props;
    if (!selectedLocation) {
      return this.setResults([]);
    }
    let results = await getFunctionSymbols(selectedLocation, maxResults);

    if (query === "@" || query === "#") {
      results = results.map(formatSymbol);
      return this.setResults(results);
    }
    results = filter(results, query.slice(1), "name");
    results = results.map(formatSymbol);
    return this.setResults(results);
  };

  searchShortcuts = query => {
    const results = formatShortcutResults();
    if (query == "?") {
      this.setResults(results);
    } else {
      this.setResults(filter(results, query.slice(1)));
    }
  };

  /**
   * This method is called when we just opened the modal and the query input is empty
   */
  showTopSources = () => {
    const { openedTabUrls, blackBoxRanges, projectDirectoryRoot } = this.props;
    let { displayedSources } = this.props;

    // If there is some tabs opened, only show tab's sources.
    // Otherwise, we display all visible sources (per SourceTree definition),
    // setResults will restrict the number of results to a maximum limit.
    if (openedTabUrls.length) {
      displayedSources = displayedSources.filter(
        source => !!source.url && openedTabUrls.includes(source.url)
      );
    }

    this.setResults(
      this.formatSources(
        displayedSources,
        openedTabUrls,
        blackBoxRanges,
        projectDirectoryRoot
      )
    );
  };

  updateResults = throttle(async query => {
    try {
      if (this.isGotoQuery()) {
        return;
      }

      if (query == "" && !this.isShortcutQuery()) {
        this.showTopSources();
        return;
      }

      if (this.isSymbolSearch()) {
        await this.searchSymbols(query);
        return;
      }

      if (this.isShortcutQuery()) {
        this.searchShortcuts(query);
        return;
      }

      this.searchSources(query);
    } catch (e) {
      // Due to throttling this might get scheduled after the component and the
      // toolbox are destroyed.
      if (this.#willUnmountCalled) {
        console.warn("Throttled QuickOpen.updateResults failed", e);
      } else {
        throw e;
      }
    }
  }, QuickOpenModal.UPDATE_RESULTS_THROTTLE);

  setModifier = item => {
    if (["@", "#", ":"].includes(item.id)) {
      this.props.setQuickOpenQuery(item.id);
    }
  };

  selectResultItem = (e, item) => {
    if (item == null) {
      return;
    }

    if (this.isShortcutQuery()) {
      this.setModifier(item);
      return;
    }

    if (this.isGotoSourceQuery()) {
      const location = parseLineColumn(this.props.query);
      this.gotoLocation({ ...location, source: item.source });
      return;
    }

    if (this.isSymbolSearch()) {
      this.gotoLocation({
        line:
          item.location && item.location.start ? item.location.start.line : 0,
      });
      return;
    }

    this.gotoLocation({ source: item.source, line: 0 });
  };

  onSelectResultItem = item => {
    const { selectedLocation, highlightLineRange, clearHighlightLineRange } =
      this.props;
    if (
      selectedLocation == null ||
      !this.isSymbolSearch() ||
      !this.isFunctionQuery()
    ) {
      return;
    }

    if (item.location) {
      highlightLineRange({
        start: item.location.start.line,
        end: item.location.end.line,
        sourceId: selectedLocation.source.id,
      });
    } else {
      clearHighlightLineRange();
    }
  };

  traverseResults = e => {
    const direction = e.key === "ArrowUp" ? -1 : 1;
    const { selectedIndex, results } = this.state;
    const resultCount = this.getResultCount();
    const index = selectedIndex + direction;
    const nextIndex = (index + resultCount) % resultCount || 0;

    this.setState({ selectedIndex: nextIndex });

    if (results != null) {
      this.onSelectResultItem(results[nextIndex]);
    }
  };

  gotoLocation = location => {
    const { selectSpecificLocation, selectedLocation, updateCursorPosition } =
      this.props;

    if (location != null) {
      const sourceLocation = createLocation({
        source: location.source || selectedLocation?.source,
        line: location.line,
        column: location.column || 0,
      });
      selectSpecificLocation(sourceLocation);
      updateCursorPosition(sourceLocation);
      this.closeModal();
    }
  };

  onChange = e => {
    const { selectedLocation, selectedContentLoaded, setQuickOpenQuery } =
      this.props;
    setQuickOpenQuery(e.target.value);
    const noSource = !selectedLocation || !selectedContentLoaded;
    if ((noSource && this.isSymbolSearch()) || this.isGotoQuery()) {
      return;
    }

    // Wait for the next tick so that reducer updates are complete.
    const targetValue = e.target.value;
    setTimeout(() => this.updateResults(targetValue), 0);
  };

  onKeyDown = e => {
    const { query } = this.props;
    const { results, selectedIndex } = this.state;
    const isGoToQuery = this.isGotoQuery();

    if (!results && !isGoToQuery) {
      return;
    }

    if (e.key === "Enter") {
      if (isGoToQuery) {
        const location = parseLineColumn(query);
        this.gotoLocation(location);
        return;
      }

      if (results) {
        this.selectResultItem(e, results[selectedIndex]);
        return;
      }
    }

    if (e.key === "Tab") {
      this.closeModal();
      return;
    }

    if (["ArrowUp", "ArrowDown"].includes(e.key)) {
      e.preventDefault();
      this.traverseResults(e);
    }
  };

  getResultCount = () => {
    const { results } = this.state;
    return results && results.length ? results.length : 0;
  };

  // Query helpers
  isFunctionQuery = () => this.props.searchType === "functions";
  isSymbolSearch = () => this.isFunctionQuery();
  isGotoQuery = () => this.props.searchType === "goto";
  isGotoSourceQuery = () => this.props.searchType === "gotoSource";
  isShortcutQuery = () => this.props.searchType === "shortcuts";
  isSourcesQuery = () => this.props.searchType === "sources";
  isSourceSearch = () => this.isSourcesQuery() || this.isGotoSourceQuery();

  /* eslint-disable react/no-danger */
  renderHighlight(candidateString, query) {
    const options = {
      wrap: {
        tagOpen: '<mark class="highlight">',
        tagClose: "</mark>",
      },
    };
    const html = fuzzyAldrin.wrap(candidateString, query, options);
    return div({
      dangerouslySetInnerHTML: {
        __html: html,
      },
    });
  }

  highlightMatching = (query, results) => {
    let newQuery = query;
    if (newQuery === "") {
      return results;
    }
    newQuery = query.replace(/[@:#?]/gi, " ");

    return results.map(result => {
      if (typeof result.title == "string") {
        return {
          ...result,
          title: this.renderHighlight(
            result.title,
            basename(newQuery),
            "title"
          ),
        };
      }
      return result;
    });
  };

  shouldShowErrorEmoji() {
    const { query } = this.props;
    if (this.isGotoQuery()) {
      return !/^:\d*$/.test(query);
    }
    return !!query && !this.getResultCount();
  }

  getSummaryMessage() {
    let summaryMsg = "";
    if (this.isGotoQuery()) {
      summaryMsg = L10N.getStr("shortcuts.gotoLine");
    } else if (this.isFunctionQuery() && !this.state.results) {
      summaryMsg = L10N.getStr("loadingText");
    }
    return summaryMsg;
  }

  render() {
    const { query } = this.props;
    const { selectedIndex, results } = this.state;

    const items = this.highlightMatching(query, results || []);
    const expanded = !!items && !!items.length;
    return React.createElement(
      Modal,
      {
        handleClose: this.closeModal,
      },
      React.createElement(SearchInput, {
        query,
        hasPrefix: true,
        count: this.getResultCount(),
        placeholder: L10N.getStr("sourceSearch.search2"),
        summaryMsg: this.getSummaryMessage(),
        showErrorEmoji: this.shouldShowErrorEmoji(),
        isLoading: false,
        onChange: this.onChange,
        onKeyDown: this.onKeyDown,
        handleClose: this.closeModal,
        expanded,
        showClose: false,
        searchKey: searchKeys.QUICKOPEN_SEARCH,
        showExcludePatterns: false,
        showSearchModifiers: false,
        selectedItemId:
          expanded && items[selectedIndex] ? items[selectedIndex].id : "",
        ...(this.isSourceSearch() ? SIZE_BIG : SIZE_DEFAULT),
      }),
      results &&
        React.createElement(ResultList, {
          key: "results",
          items,
          selected: selectedIndex,
          selectItem: this.selectResultItem,
          ref: "resultList",
          expanded,
          ...(this.isSourceSearch() ? SIZE_BIG : SIZE_DEFAULT),
        })
    );
  }
}

/* istanbul ignore next: ignoring testing of redux connection stuff */
function mapStateToProps(state) {
  const selectedLocation = getSelectedLocation(state);
  const displayedSources = getDisplayedSourcesList(state);
  const tabs = getSourceTabs(state);
  const openedTabUrls = [...new Set(tabs.map(tab => tab.url))];

  return {
    displayedSources,
    blackBoxRanges: getBlackBoxRanges(state),
    projectDirectoryRoot: getProjectDirectoryRoot(state),
    selectedLocation,
    selectedContentLoaded: selectedLocation
      ? !!getSettledSourceTextContent(state, selectedLocation)
      : undefined,
    query: getQuickOpenQuery(state),
    searchType: getQuickOpenType(state),
    openedTabUrls,
  };
}

export default connect(mapStateToProps, {
  selectSpecificLocation: actions.selectSpecificLocation,
  setQuickOpenQuery: actions.setQuickOpenQuery,
  highlightLineRange: actions.highlightLineRange,
  clearHighlightLineRange: actions.clearHighlightLineRange,
  closeQuickOpen: actions.closeQuickOpen,
  getFunctionSymbols: actions.getFunctionSymbols,
  updateCursorPosition: actions.updateCursorPosition,
})(QuickOpenModal);
