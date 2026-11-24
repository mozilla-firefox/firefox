/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");

const Types = require("resource://devtools/client/inspector/boxmodel/types.js");

const BOXMODEL_L10N = require("resource://devtools/client/inspector/boxmodel/utils/l10n.js");

const SHARED_STRINGS_URI = "devtools/client/locales/shared.properties";
const SHARED_L10N = new LocalizationHelper(SHARED_STRINGS_URI);

class BoxModelInfo extends PureComponent {
  static get propTypes() {
    return {
      boxModel: PropTypes.shape(Types.boxModel).isRequired,
      onToggleGeometryEditor: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.onToggleGeometryEditor = this.onToggleGeometryEditor.bind(this);
  }

  onToggleGeometryEditor() {
    this.props.onToggleGeometryEditor();
  }

  render() {
    const { boxModel } = this.props;
    const { geometryEditorEnabled, layout } = boxModel;
    const { height = "-", isPositionEditable, position, width = "-" } = layout;

    let buttonClass = "layout-geometry-editor devtools-button";
    if (geometryEditorEnabled) {
      buttonClass += " checked";
    }

    return dom.div(
      {
        className: "boxmodel-info",
        role: "region",
        "aria-label": SHARED_L10N.getFormatStr(
          "boxModelInfo.accessibleLabel",
          width,
          height,
          position
        ),
      },
      dom.span(
        { className: "boxmodel-element-size" },
        SHARED_L10N.getFormatStr("dimensions", width, height)
      ),
      dom.section(
        { className: "boxmodel-position-group" },
        isPositionEditable
          ? dom.button({
              className: buttonClass,
              title: BOXMODEL_L10N.getStr("boxmodel-geometry-button-title"),
              onClick: this.onToggleGeometryEditor,
            })
          : null,
        dom.span({ className: "boxmodel-element-position" }, position)
      )
    );
  }
}

module.exports = BoxModelInfo;
