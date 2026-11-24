/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const Accordion = createFactory(
  require("resource://devtools/client/shared/components/Accordion.js")
);
const FontList = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontList.js")
);

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const Types = require("resource://devtools/client/inspector/fonts/types.js");

class FontOverview extends PureComponent {
  static get propTypes() {
    return {
      fontData: PropTypes.shape(Types.fontData).isRequired,
      fontOptions: PropTypes.shape(Types.fontOptions).isRequired,
      onPreviewTextChange: PropTypes.func.isRequired,
      onToggleFontHighlight: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.onToggleFontHighlightGlobal = (font, show) => {
      this.props.onToggleFontHighlight(font, show, false);
    };
  }

  renderFonts() {
    const { fontData, fontOptions, onPreviewTextChange } = this.props;

    const fonts = fontData.allFonts;

    if (!fonts.length) {
      return null;
    }

    return Accordion({
      items: [
        {
          header: getStr("font-inspector-all-fonts-on-page-header"),
          id: "font-list-details",
          component: FontList,
          componentProps: {
            fontOptions,
            fonts,
            onPreviewTextChange,
            onToggleFontHighlight: this.onToggleFontHighlightGlobal,
          },
          opened: false,
        },
      ],
    });
  }

  render() {
    return dom.div(
      {
        id: "font-container",
      },
      this.renderFonts()
    );
  }
}

module.exports = FontOverview;
