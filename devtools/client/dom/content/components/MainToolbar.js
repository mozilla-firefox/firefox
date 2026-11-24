/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// React
const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const SearchBox = createFactory(
  require("resource://devtools/client/shared/components/SearchBox.js")
);

const { l10n } = require("resource://devtools/client/dom/content/utils.js");

// Actions
const {
  fetchProperties,
} = require("resource://devtools/client/dom/content/actions/grips.js");
const {
  setVisibilityFilter,
} = require("resource://devtools/client/dom/content/actions/filter.js");

/**
 * This template is responsible for rendering a toolbar
 * within the 'Headers' panel.
 */
class MainToolbar extends Component {
  static get propTypes() {
    return {
      object: PropTypes.any.isRequired,
      dispatch: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.onRefresh = this.onRefresh.bind(this);
    this.onSearch = this.onSearch.bind(this);
  }

  onRefresh() {
    this.props.dispatch(fetchProperties(this.props.object));
  }

  onSearch(value) {
    this.props.dispatch(setVisibilityFilter(value));
  }

  render() {
    return dom.div(
      { className: "devtools-toolbar devtools-input-toolbar" },
      SearchBox({
        key: "filter",
        delay: 250,
        onChange: this.onSearch,
        placeholder: l10n.getStr("dom-panel-filter-placeholder"),
        type: "filter",
      }),
      dom.span({ className: "devtools-separator" }),
      dom.button({
        key: "refresh",
        className: "refresh devtools-button",
        id: "dom-refresh-button",
        title: l10n.getStr("dom-panel-refresh-button-title"),
        onClick: this.onRefresh,
      })
    );
  }
}

// Exports from this module
module.exports = MainToolbar;
