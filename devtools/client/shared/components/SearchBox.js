/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global window */

"use strict";

const {
  createFactory,
  createRef,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const l10n = new LocalizationHelper(
  "devtools/client/locales/components.properties"
);

loader.lazyGetter(this, "SearchBoxAutocompletePopup", function () {
  return createFactory(
    require("resource://devtools/client/shared/components/SearchBoxAutocompletePopup.js")
  );
});
loader.lazyGetter(this, "MDNLink", function () {
  return createFactory(
    require("resource://devtools/client/shared/components/MdnLink.js")
  );
});

loader.lazyRequireGetter(
  this,
  "KeyShortcuts",
  "resource://devtools/client/shared/key-shortcuts.js"
);

class SearchBox extends PureComponent {
  static get propTypes() {
    return {
      autocompleteProvider: PropTypes.func,
      delay: PropTypes.number,
      keyShortcut: PropTypes.string,
      learnMoreTitle: PropTypes.string,
      learnMoreUrl: PropTypes.string,
      onBlur: PropTypes.func,
      onChange: PropTypes.func.isRequired,
      onClearButtonClick: PropTypes.func,
      onFocus: PropTypes.func,
      // Optional function that will be called on the focus keyboard shortcut, before
      // setting the focus to the input. If the function returns false, the input won't
      // get focused.
      onFocusKeyboardShortcut: PropTypes.func,
      onKeyDown: PropTypes.func,
      placeholder: PropTypes.string.isRequired,
      summary: PropTypes.string,
      summaryId: PropTypes.string,
      summaryTooltip: PropTypes.string,
      type: PropTypes.string,
      initialValue: PropTypes.string,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      value: props.initialValue || "",
      focused: false,
    };

    this.autocompleteRef = createRef();
    this.inputRef = createRef();

    this.onBlur = this.onBlur.bind(this);
    this.onChange = this.onChange.bind(this);
    this.onClearButtonClick = this.onClearButtonClick.bind(this);
    this.onFocus = this.onFocus.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
  }

  componentDidMount() {
    if (!this.props.keyShortcut) {
      return;
    }

    this.shortcuts = new KeyShortcuts({
      window,
    });
    this.shortcuts.on(this.props.keyShortcut, event => {
      if (this.props.onFocusKeyboardShortcut?.(event)) {
        return;
      }

      event.preventDefault();
      this.focus();
    });
  }

  componentWillUnmount() {
    if (this.shortcuts) {
      this.shortcuts.destroy();
    }

    // Clean up an existing timeout.
    if (this.searchTimeout) {
      clearTimeout(this.searchTimeout);
    }
  }

  focus() {
    if (this.inputRef) {
      this.inputRef.current.focus();
    }
  }

  onChange(inputValue = "") {
    if (this.state.value !== inputValue) {
      this.setState({
        focused: true,
        value: inputValue,
      });
    }

    if (!this.props.delay) {
      this.props.onChange(inputValue);
      return;
    }

    // Clean up an existing timeout before creating a new one.
    if (this.searchTimeout) {
      clearTimeout(this.searchTimeout);
    }

    // Execute the search after a timeout. It makes the UX
    // smoother if the user is typing quickly.
    this.searchTimeout = setTimeout(() => {
      this.searchTimeout = null;
      this.props.onChange(this.state.value);
    }, this.props.delay);
  }

  onClearButtonClick() {
    this.onChange("");

    if (this.props.onClearButtonClick) {
      this.props.onClearButtonClick();
    }
  }

  onFocus() {
    if (this.props.onFocus) {
      this.props.onFocus();
    }

    this.setState({ focused: true });
  }

  onBlur() {
    if (this.props.onBlur) {
      this.props.onBlur();
    }

    this.setState({ focused: false });
  }

  onKeyDown(e) {
    if (this.props.onKeyDown) {
      this.props.onKeyDown(e);
    }

    const autocomplete = this.autocompleteRef.current;
    if (!autocomplete || autocomplete.state.list.length <= 0) {
      return;
    }

    switch (e.key) {
      case "ArrowDown":
        e.preventDefault();
        autocomplete.jumpBy(1);
        break;
      case "ArrowUp":
        e.preventDefault();
        autocomplete.jumpBy(-1);
        break;
      case "PageDown":
        e.preventDefault();
        autocomplete.jumpBy(5);
        break;
      case "PageUp":
        e.preventDefault();
        autocomplete.jumpBy(-5);
        break;
      case "Enter":
      case "Tab":
        e.preventDefault();
        autocomplete.select();
        break;
      case "Escape":
        e.preventDefault();
        this.onBlur();
        break;
      case "Home":
        e.preventDefault();
        autocomplete.jumpToTop();
        break;
      case "End":
        e.preventDefault();
        autocomplete.jumpToBottom();
        break;
    }
  }

  render() {
    const {
      autocompleteProvider,
      summary,
      summaryId,
      summaryTooltip,
      learnMoreTitle,
      learnMoreUrl,
      placeholder,
      type = "search",
    } = this.props;
    const { value } = this.state;
    const showAutocomplete =
      autocompleteProvider && this.state.focused && value !== "";
    const showLearnMoreLink = learnMoreUrl && value === "";

    return dom.div(
      { className: "devtools-searchbox" },
      dom.input({
        className: `devtools-${type}input`,
        onBlur: this.onBlur,
        onChange: e => this.onChange(e.target.value),
        onFocus: this.onFocus,
        onKeyDown: this.onKeyDown,
        placeholder,
        ref: this.inputRef,
        value,
        type: "search",
        "aria-describedby": (summary && summaryId) || undefined,
      }),
      showLearnMoreLink &&
        MDNLink({
          title: learnMoreTitle,
          url: learnMoreUrl,
        }),
      summary
        ? dom.span(
            {
              className: "devtools-searchinput-summary",
              id: summaryId,
              title: summaryTooltip,
            },
            summary
          )
        : null,
      dom.button({
        className: "devtools-searchinput-clear",
        hidden: value === "",
        onClick: this.onClearButtonClick,
        title: l10n.getStr("searchBox.clearButtonTitle"),
      }),
      showAutocomplete &&
        SearchBoxAutocompletePopup({
          autocompleteProvider,
          filter: value,
          onItemSelected: itemValue => this.onChange(itemValue),
          ref: this.autocompleteRef,
        })
    );
  }
}

module.exports = SearchBox;
