/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const { openContentLink } = require("resource://devtools/client/shared/link");

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const Types = require("resource://devtools/client/inspector/fonts/types.js");

const isMacOS = Services.appinfo.OS === "Darwin";
const MAX_STRING_LENGTH = 250;

class FontMetadata extends PureComponent {
  static get propTypes() {
    return {
      font: PropTypes.shape(Types.font).isRequired,
    };
  }

  constructor() {
    super();

    this.state = { expanded: {} };
  }

  /**
   * Handle displaying potentially long font properties (e.g. License).
   * If the string is larger than MAX_STRING_LENGTH, the string gets truncated
   * and we display a twisty button to reveal the full text.
   *
   * @param {string} fontProperty: The font property to render
   * @returns {string | Array<ReactElement>}
   */
  renderExpandableString(fontProperty) {
    const str = this.props.font[fontProperty];
    if (str.length <= MAX_STRING_LENGTH) {
      return str;
    }
    const expanded = !!this.state.expanded[fontProperty];

    const toggleExpanded = () => {
      this.setState({
        expanded: { ...this.state.expanded, [fontProperty]: !expanded },
      });
    };

    const title = getStr("font-inspector-show-full-text");

    return [
      dom.button(
        {
          className: "theme-twisty",
          title,
          "aria-expanded": expanded,
          onClick: toggleExpanded,
        },
        ""
      ),
      !expanded ? str.substring(0, MAX_STRING_LENGTH) : str,
      !expanded
        ? dom.button({
            onClick: toggleExpanded,
            title,
            className: "font-truncated-string-expander",
          })
        : null,
    ];
  }

  render() {
    const { font } = this.props;

    const dlItems = [];

    if (font.version) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-version")),
        // font.version might already have the `Version` prefix, remove it.
        dom.dd({}, font.version.replace(/^version(\s|:)*/i, ""))
      );
    }

    const onClickLink = e => {
      e.preventDefault();
      openContentLink(e.target.href, {
        inBackground: isMacOS ? e.metaKey : e.ctrlKey,
        relatedToCurrent: true,
      });
    };

    if (font.designer) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-designer")),
        dom.dd(
          {},
          font.designerUrl && URL.canParse(font.designerUrl)
            ? dom.a(
                {
                  href: font.designerUrl,
                  onClick: onClickLink,
                },
                font.designer
              )
            : font.designer
        )
      );
    }

    if (font.manufacturer) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-manufacturer")),
        dom.dd({}, font.manufacturer)
      );
    }

    if (font.vendorUrl) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-vendor")),
        dom.dd(
          {},
          dom.a({ href: font.vendorUrl, onClick: onClickLink }, font.vendorUrl)
        )
      );
    }

    if (font.description) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-description")),
        dom.dd({}, this.renderExpandableString("description"))
      );
    }

    if (font.license) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-license")),
        dom.dd({}, this.renderExpandableString("license"))
      );
    }

    if (font.licenseUrl) {
      dlItems.push(
        dom.dt({}, getStr("font-inspector-font-license-info-url")),
        dom.dd(
          {},
          dom.a(
            { href: font.licenseUrl, onClick: onClickLink },
            font.licenseUrl
          )
        )
      );
    }

    if (!dlItems) {
      return null;
    }

    return dom.dl({}, dlItems);
  }
}

module.exports = FontMetadata;
