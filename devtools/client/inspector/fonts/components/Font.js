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

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const FontName = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontName.js")
);
const FontOrigin = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontOrigin.js")
);
const FontPreview = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontPreview.js")
);
const FontMetadata = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontMetadata.js")
);

const Types = require("resource://devtools/client/inspector/fonts/types.js");

class Font extends PureComponent {
  static get propTypes() {
    return {
      font: PropTypes.shape(Types.font).isRequired,
      onPreviewClick: PropTypes.func,
      onToggleFontHighlight: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      isFontFaceRuleExpanded: false,
    };

    this.onFontFaceRuleToggle = this.onFontFaceRuleToggle.bind(this);
  }

  onFontFaceRuleToggle(event) {
    this.setState({
      isFontFaceRuleExpanded: !this.state.isFontFaceRuleExpanded,
    });
    event.stopPropagation();
  }

  renderFontCSSCode(rule, ruleText) {
    if (!rule) {
      return null;
    }

    const { isFontFaceRuleExpanded } = this.state;

    // Cut the rule text in 3 parts: the selector, the declarations, the closing brace.
    // This way we can collapse the declarations by default and display an expander icon
    // to expand them again.
    const leading = ruleText.substring(0, ruleText.indexOf("{") + 1);

    const trailing = ruleText.substring(ruleText.lastIndexOf("}"));

    let body;
    if (isFontFaceRuleExpanded) {
      const ruleBodyText = ruleText
        .substring(ruleText.indexOf("{") + 1, ruleText.lastIndexOf("}"))
        .trim();

      const indent = "  ";
      body = "\n";
      const lexer = new InspectorCSSParser(ruleBodyText);
      let token;
      let isNewLine = true;
      while ((token = lexer.nextToken())) {
        if (isNewLine) {
          // If we just added a new line, ignore any whitespace as we'll handle the
          // indentation ourselves.
          if (token.tokenType === "WhiteSpace") {
            continue;
          }
          body += indent;
          isNewLine = false;
        }

        body += token.text;
        if (token.tokenType === "Semicolon") {
          body += "\n";
          isNewLine = true;
        }
      }
    }

    return dom.pre(
      {
        className: "font-css-code",
      },
      this.renderFontCSSCodeTwisty(),
      leading,
      isFontFaceRuleExpanded
        ? body
        : dom.button({
            className: "font-truncated-string-expander",
            onClick: this.onFontFaceRuleToggle,
            title: getStr("font-inspector-show-full-text"),
          }),
      trailing
    );
  }

  renderFontCSSCodeTwisty() {
    const { isFontFaceRuleExpanded } = this.state;

    return dom.button({
      className: "theme-twisty",
      onClick: this.onFontFaceRuleToggle,
      "aria-expanded": isFontFaceRuleExpanded,
      title: getStr("font-inspector-show-full-text"),
    });
  }

  renderFontFamilyName(family) {
    if (!family) {
      return null;
    }

    return dom.div({ className: "font-family-name" }, family);
  }

  render() {
    const { font, onPreviewClick, onToggleFontHighlight } = this.props;

    const { CSSFamilyName, previewUrl, rule, ruleText } = font;

    return dom.li(
      {
        className: "font",
      },
      dom.div(
        {},
        this.renderFontFamilyName(CSSFamilyName),
        FontName({ font, onToggleFontHighlight })
      ),
      FontOrigin({ font }),
      FontPreview({ onPreviewClick, previewUrl }),
      this.renderFontCSSCode(rule, ruleText),
      FontMetadata({ font })
    );
  }
}

module.exports = Font;
