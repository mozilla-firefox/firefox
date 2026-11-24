/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createRef,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const Types = require("resource://devtools/client/inspector/fonts/types.js");
const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");

const PREVIEW_TEXT_MAX_LENGTH = 30;

class FontPreviewInput extends PureComponent {
  static get propTypes() {
    return {
      onPreviewTextChange: PropTypes.func.isRequired,
      previewText: Types.fontOptions.previewText.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.onChange = this.onChange.bind(this);
    this.onFocus = this.onFocus.bind(this);
    this.inputRef = createRef();

    this.state = {
      value: this.props.previewText,
    };
  }

  onChange(e) {
    const value = e.target.value;
    this.props.onPreviewTextChange(value);

    this.setState(prevState => {
      return { ...prevState, value };
    });
  }

  onFocus(e) {
    e.target.select();
  }

  focus() {
    this.inputRef.current.focus();
  }

  render() {
    return dom.div(
      {
        id: "font-preview-input-container",
      },
      dom.input({
        className: "devtools-searchinput",
        onChange: this.onChange,
        onFocus: this.onFocus,
        maxLength: PREVIEW_TEXT_MAX_LENGTH,
        placeholder: getStr("font-inspector-preview-text-placeholder"),
        ref: this.inputRef,
        type: "text",
        value: this.state.value,
      })
    );
  }
}

module.exports = FontPreviewInput;
