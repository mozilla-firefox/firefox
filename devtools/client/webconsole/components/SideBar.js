/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const GridElementWidthResizer = createFactory(
  require("resource://devtools/client/shared/components/splitter/GridElementWidthResizer.js")
);
loader.lazyRequireGetter(
  this,
  "dom",
  "resource://devtools/client/shared/vendor/react-dom-factories.js"
);
loader.lazyRequireGetter(
  this,
  "getObjectInspector",
  "resource://devtools/client/webconsole/utils/object-inspector.js",
  true
);
loader.lazyRequireGetter(
  this,
  "actions",
  "resource://devtools/client/webconsole/actions/index.js"
);
loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);
const reps = ChromeUtils.importESModule(
  "resource://devtools/client/shared/components/reps/index.mjs",
  { global: "current" }
);
loader.lazyRequireGetter(
  this,
  "l10n",
  "resource://devtools/client/webconsole/utils/messages.js",
  true
);

class SideBar extends Component {
  static get propTypes() {
    return {
      serviceContainer: PropTypes.object,
      dispatch: PropTypes.func.isRequired,
      front: PropTypes.object,
      onResized: PropTypes.func,
    };
  }

  constructor(props) {
    super(props);
    this.onClickSidebarClose = this.onClickSidebarClose.bind(this);
  }

  shouldComponentUpdate(nextProps) {
    const { front } = nextProps;
    return front !== this.props.front;
  }

  onClickSidebarClose() {
    this.props.dispatch(actions.sidebarClose());
  }

  render() {
    const { front, serviceContainer } = this.props;

    const objectInspector = getObjectInspector(front, serviceContainer, {
      autoExpandDepth: 1,
      mode: reps.MODE.SHORT,
      autoFocusRoot: true,
      pathPrefix: "WebConsoleSidebar",
      customFormat: false,
    });

    return [
      dom.aside(
        {
          className: "sidebar",
          key: "sidebar",
          ref: node => {
            this.node = node;
          },
        },
        dom.header(
          {
            className: "devtools-toolbar webconsole-sidebar-toolbar",
          },
          dom.button({
            className: "devtools-button sidebar-close-button",
            title: l10n.getStr("webconsole-close-sidebar-button-tooltip"),
            onClick: this.onClickSidebarClose,
          })
        ),
        dom.aside(
          {
            className: "sidebar-contents",
          },
          objectInspector
        )
      ),
      GridElementWidthResizer({
        key: "resizer",
        enabled: true,
        position: "start",
        className: "sidebar-resizer",
        getControlledElementNode: () => this.node,
      }),
    ];
  }
}

function mapStateToProps(state) {
  return {
    front: state.ui.frontInSidebar,
  };
}

module.exports = connect(mapStateToProps)(SideBar);
