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

const ComputedProperty = createFactory(
  require("resource://devtools/client/inspector/boxmodel/components/ComputedProperty.js")
);

const Types = require("resource://devtools/client/inspector/boxmodel/types.js");

const BOXMODEL_L10N = require("resource://devtools/client/inspector/boxmodel/utils/l10n.js");

class BoxModelProperties extends PureComponent {
  static get propTypes() {
    return {
      boxModel: PropTypes.shape(Types.boxModel).isRequired,
      dispatch: PropTypes.func.isRequired,
      setSelectedNode: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      isOpen: true,
    };

    this.getReferenceElement = this.getReferenceElement.bind(this);
    this.onToggleExpander = this.onToggleExpander.bind(this);
  }

  /**
   * Various properties can display a reference element. E.g. position displays an offset
   * parent if its value is other than fixed or static. Or z-index displays a stacking
   * context, etc.
   * This returns the right element if there needs to be one, and one was passed in the
   * props.
   *
   * @return {object} An object with 2 properties:
   * - referenceElement {NodeFront}
   * - referenceElementType {String}
   */
  getReferenceElement(propertyName) {
    const value = this.props.boxModel.layout[propertyName];

    if (
      propertyName === "position" &&
      this.props.boxModel.offsetParent &&
      value !== "static" &&
      // A fixed position element's offsetParent is the <body> element if its
      // containing block is the viewport. We only show the offsetParent for
      // fixed position elements if the offsetParent is not the viewport. This
      // can occur if the element has an ancestor that creates a fixed position
      // containing block, for instance with a `transform` declaration.
      (value !== "fixed" || this.props.boxModel.offsetParent.tagName !== "BODY")
    ) {
      return {
        referenceElement: this.props.boxModel.offsetParent,
        referenceElementType: BOXMODEL_L10N.getStr("boxmodel-offset-parent"),
      };
    }

    return {};
  }

  onToggleExpander(event) {
    this.setState({
      isOpen: !this.state.isOpen,
    });
    event.stopPropagation();
  }

  render() {
    const { boxModel, dispatch, setSelectedNode } = this.props;
    const { layout } = boxModel;

    const layoutInfo = [
      "box-sizing",
      "display",
      "float",
      "line-height",
      "position",
      "z-index",
    ];

    const properties = layoutInfo.map(info => {
      const { referenceElement, referenceElementType } =
        this.getReferenceElement(info);

      return ComputedProperty({
        dispatch,
        key: info,
        name: info,
        referenceElement,
        referenceElementType,
        setSelectedNode,
        value: layout[info],
      });
    });

    return dom.div(
      { className: "layout-properties" },
      dom.div(
        {
          className: "layout-properties-header",
          role: "heading",
          "aria-level": "3",
          onDoubleClick: this.onToggleExpander,
        },
        dom.span({
          className: "layout-properties-expander theme-twisty",
          open: this.state.isOpen,
          role: "button",
          "aria-label": BOXMODEL_L10N.getStr(
            this.state.isOpen
              ? "boxmodel-properties-hide-label"
              : "boxmodel-properties-show-label"
          ),
          onClick: this.onToggleExpander,
        }),
        BOXMODEL_L10N.getStr("boxmodel-properties-label")
      ),
      dom.div(
        {
          className: "layout-properties-wrapper devtools-monospace",
          hidden: !this.state.isOpen,
          role: "table",
        },
        properties
      )
    );
  }
}

module.exports = BoxModelProperties;
