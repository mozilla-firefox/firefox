/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

loader.lazyRequireGetter(
  this,
  "getNodeRep",
  "resource://devtools/client/inspector/shared/node-reps.js"
);

const {
  highlightNode,
  unhighlightNode,
} = require("resource://devtools/client/inspector/boxmodel/actions/box-model-highlighter.js");

const BOXMODEL_L10N = require("resource://devtools/client/inspector/boxmodel/utils/l10n.js");

class ComputedProperty extends PureComponent {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      name: PropTypes.string.isRequired,
      referenceElement: PropTypes.object,
      referenceElementType: PropTypes.string,
      setSelectedNode: PropTypes.func,
      value: PropTypes.string,
    };
  }

  constructor(props) {
    super(props);

    this.renderReferenceElementPreview =
      this.renderReferenceElementPreview.bind(this);
  }

  renderReferenceElementPreview() {
    const {
      dispatch,
      referenceElement,
      referenceElementType,
      setSelectedNode,
    } = this.props;

    if (!referenceElement) {
      return null;
    }

    return dom.div(
      { className: "reference-element" },
      dom.span(
        {
          className: "reference-element-type",
          role: "button",
          title: BOXMODEL_L10N.getStr("boxmodel-offset-parent-title"),
        },
        referenceElementType
      ),
      getNodeRep(referenceElement, {
        onInspectIconClick: () =>
          setSelectedNode(referenceElement, { reason: "box-model" }),
        onDOMNodeMouseOver: () => dispatch(highlightNode(referenceElement)),
        onDOMNodeMouseOut: () => dispatch(unhighlightNode()),
      })
    );
  }

  render() {
    const { name, value } = this.props;

    return dom.div(
      {
        className: "computed-property-view",
        role: "row",
        "data-property-name": name,
        ref: container => {
          this.container = container;
        },
      },
      dom.div(
        {
          className: "computed-property-name-container",
          role: "presentation",
        },
        dom.div(
          {
            className: "computed-property-name theme-fg-color3",
            role: "cell",
            title: name,
          },
          name
        )
      ),
      dom.div(
        {
          className: "computed-property-value-container",
          role: "presentation",
        },
        dom.div(
          {
            className: "computed-property-value theme-fg-color1",
            dir: "ltr",
            role: "cell",
          },
          value
        ),
        this.renderReferenceElementPreview()
      )
    );
  }
}

module.exports = ComputedProperty;
