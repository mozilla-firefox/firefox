/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  createRef,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const {
  getSelectorFromGrip,
  translateNodeFrontToGrip,
} = require("resource://devtools/client/inspector/shared/utils.js");
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");

const Accordion = createFactory(
  require("resource://devtools/client/shared/components/Accordion.js")
);
const BoxModel = createFactory(
  require("resource://devtools/client/inspector/boxmodel/components/BoxModel.js")
);
const Flexbox = createFactory(
  require("resource://devtools/client/inspector/flexbox/components/Flexbox.js")
);
const Grid = createFactory(
  require("resource://devtools/client/inspector/grids/components/Grid.js")
);

const BoxModelTypes = require("resource://devtools/client/inspector/boxmodel/types.js");
const FlexboxTypes = require("resource://devtools/client/inspector/flexbox/types.js");
const GridTypes = require("resource://devtools/client/inspector/grids/types.js");

const BOXMODEL_L10N = require("resource://devtools/client/inspector/boxmodel/utils/l10n.js");

const LAYOUT_STRINGS_URI = "devtools/client/locales/layout.properties";
const LAYOUT_L10N = new LocalizationHelper(LAYOUT_STRINGS_URI);

const FLEXBOX_OPENED_PREF = "devtools.layout.flexbox.opened";
const FLEX_CONTAINER_OPENED_PREF = "devtools.layout.flex-container.opened";
const FLEX_ITEM_OPENED_PREF = "devtools.layout.flex-item.opened";
const GRID_OPENED_PREF = "devtools.layout.grid.opened";
const BOXMODEL_OPENED_PREF = "devtools.layout.boxmodel.opened";

class LayoutApp extends PureComponent {
  static get propTypes() {
    return {
      boxModel: PropTypes.shape(BoxModelTypes.boxModel).isRequired,
      dispatch: PropTypes.func.isRequired,
      flexbox: PropTypes.shape(FlexboxTypes.flexbox).isRequired,
      getSwatchColorPickerTooltip: PropTypes.func.isRequired,
      grids: PropTypes.arrayOf(PropTypes.shape(GridTypes.grid)).isRequired,
      highlighterSettings: PropTypes.shape(GridTypes.highlighterSettings)
        .isRequired,
      onSetFlexboxOverlayColor: PropTypes.func.isRequired,
      onSetGridOverlayColor: PropTypes.func.isRequired,
      onShowBoxModelEditor: PropTypes.func.isRequired,
      onShowGridOutlineHighlight: PropTypes.func,
      onToggleGeometryEditor: PropTypes.func.isRequired,
      onToggleGridHighlighter: PropTypes.func.isRequired,
      onToggleShowGridAreas: PropTypes.func.isRequired,
      onToggleShowGridLineNumbers: PropTypes.func.isRequired,
      onToggleShowInfiniteLines: PropTypes.func.isRequired,
      setSelectedNode: PropTypes.func.isRequired,
      showBoxModelProperties: PropTypes.bool.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.containerRef = createRef();

    this.scrollToTop = this.scrollToTop.bind(this);
  }

  getBoxModelSection() {
    return {
      component: BoxModel,
      componentProps: this.props,
      contentClassName: "layout-content",
      header: BOXMODEL_L10N.getStr("boxmodel-title"),
      id: "layout-section-boxmodel",
      opened: Services.prefs.getBoolPref(BOXMODEL_OPENED_PREF),
      onToggle: opened => {
        Services.prefs.setBoolPref(BOXMODEL_OPENED_PREF, opened);
      },
    };
  }

  getFlexAccordionData(flexContainer) {
    if (!flexContainer.actorID) {
      // No flex container or flex item selected.
      return {
        pref: FLEXBOX_OPENED_PREF,
        id: "layout-section-flex",
        header: LAYOUT_L10N.getStr("flexbox.header"),
      };
    } else if (!flexContainer.flexItemShown) {
      // No flex item selected.
      return {
        pref: FLEX_CONTAINER_OPENED_PREF,
        id: "layout-section-flex-container",
        header: LAYOUT_L10N.getStr("flexbox.flexContainer"),
      };
    }

    return {
      pref: FLEX_ITEM_OPENED_PREF,
      id: "layout-section-flex-item",
      header: LAYOUT_L10N.getFormatStr(
        "flexbox.flexItemOf",
        getSelectorFromGrip(translateNodeFrontToGrip(flexContainer.nodeFront))
      ),
    };
  }

  getFlexSection(flexContainer) {
    const { pref, id, header } = this.getFlexAccordionData(flexContainer);

    return {
      className: "flex-accordion",
      component: Flexbox,
      componentProps: {
        ...this.props,
        flexContainer,
        scrollToTop: this.scrollToTop,
      },
      contentClassName: "layout-content",
      header,
      id,
      opened: Services.prefs.getBoolPref(pref),
      onToggle: opened => {
        Services.prefs.setBoolPref(pref, opened);
      },
    };
  }

  getGridSection() {
    return {
      component: Grid,
      componentProps: this.props,
      contentClassName: "layout-content",
      header: LAYOUT_L10N.getStr("layout.header"),
      id: "layout-grid-section",
      opened: Services.prefs.getBoolPref(GRID_OPENED_PREF),
      onToggle: opened => {
        Services.prefs.setBoolPref(GRID_OPENED_PREF, opened);
      },
    };
  }

  /**
   * Scrolls to the top of the layout container.
   */
  scrollToTop() {
    this.containerRef.current.scrollTop = 0;
  }

  render() {
    const { flexContainer, flexItemContainer } = this.props.flexbox;

    const items = [
      this.getFlexSection(flexContainer),
      this.getGridSection(),
      this.getBoxModelSection(),
    ];

    // If the current selected node is both a flex container and flex item. Render
    // an extra accordion with another Flexbox component where the node is shown as an
    // item of its parent flex container.
    // If the node was selected from the markup-view, then show this accordion after the
    // container accordion. Otherwise show it first.
    // The reason is that if the user selects an item-container in the markup view, it
    // is assumed that they want to primarily see that element as a container, so the
    // container info should be at the top.
    if (flexItemContainer?.actorID) {
      items.splice(
        this.props.flexbox.initiatedByMarkupViewSelection ? 1 : 0,
        0,
        this.getFlexSection(flexItemContainer)
      );
    }

    return dom.div(
      {
        className: "layout-container",
        ref: this.containerRef,
        role: "document",
      },
      Accordion({ items })
    );
  }
}

module.exports = connect(state => state)(LayoutApp);
