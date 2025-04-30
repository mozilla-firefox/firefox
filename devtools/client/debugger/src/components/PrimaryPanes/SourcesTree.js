/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Dependencies
import React, {
  Component,
  Fragment,
} from "devtools/client/shared/vendor/react";
import {
  div,
  button,
  span,
  footer,
} from "devtools/client/shared/vendor/react-dom-factories";
import PropTypes from "devtools/client/shared/vendor/react-prop-types";
import { connect } from "devtools/client/shared/vendor/react-redux";
const MenuButton = require("resource://devtools/client/shared/components/menu/MenuButton.js");
const MenuItem = require("resource://devtools/client/shared/components/menu/MenuItem.js");
const MenuList = require("resource://devtools/client/shared/components/menu/MenuList.js");
import { prefs } from "../../utils/prefs";
import { createLocation } from "../../utils/location";

// Selectors
import {
  getMainThreadHost,
  getExpandedState,
  getProjectDirectoryRoot,
  getProjectDirectoryRootName,
  getProjectDirectoryRootFullName,
  getSourcesTreeSources,
  getFocusedSourceItem,
  getHideIgnoredSources,
} from "../../selectors/index";

// Actions
import actions from "../../actions/index";

// Components
import SourcesTreeItem from "./SourcesTreeItem";
import AccessibleImage from "../shared/AccessibleImage";

const classnames = require("resource://devtools/client/shared/classnames.js");
const Tree = require("resource://devtools/client/shared/components/Tree.js");

function shouldAutoExpand(item, mainThreadHost) {
  // There is only one case where we want to force auto expand,
  // when we are on the group of the page's domain.
  return item.type == "group" && item.groupName === mainThreadHost;
}

class SourcesTree extends Component {
  constructor(props) {
    super(props);

    this.state = {
      hasOverflow: undefined,
    };

    // Monitor resize to check if the source tree shows a scrollbar.
    this.onResize = this.onResize.bind(this);
    this.resizeObserver = new ResizeObserver(this.onResize);
  }

  static get propTypes() {
    return {
      mainThreadHost: PropTypes.string,
      expanded: PropTypes.object.isRequired,
      focusItem: PropTypes.func.isRequired,
      focused: PropTypes.object,
      projectRoot: PropTypes.string.isRequired,
      selectMayBePrettyPrintedLocation: PropTypes.func.isRequired,
      setExpandedState: PropTypes.func.isRequired,
      rootItems: PropTypes.array.isRequired,
      clearProjectDirectoryRoot: PropTypes.func.isRequired,
      projectRootName: PropTypes.string.isRequired,
      setHideOrShowIgnoredSources: PropTypes.func.isRequired,
      hideIgnoredSources: PropTypes.bool.isRequired,
    };
  }

  onResize() {
    const tree = this.refs.tree;
    if (!tree) {
      return;
    }

    // "treeRef" is created via createRef() in the Tree component.
    const treeEl = tree.treeRef.current;
    const hasOverflow = treeEl.scrollHeight > treeEl.clientHeight;
    if (hasOverflow !== this.state.hasOverflow) {
      this.setState({ hasOverflow });
    }
  }

  componentDidUpdate() {
    this.onResize();
  }

  componentDidMount() {
    this.resizeObserver.observe(this.refs.pane);
    this.onResize();
  }

  componentWillUnmount() {
    this.resizeObserver.disconnect();
  }

  selectSourceItem = item => {
    // Use a dedicated selection method to handle edgecases around pretty printed sources
    // When a source is pretty printed, the `item.source` still refers to the minified source,
    // whereas we expect to open the pretty printed version (if it exists).
    this.props.selectMayBePrettyPrintedLocation(
      createLocation({ source: item.source, sourceActor: item.sourceActor })
    );
  };

  onFocus = item => {
    this.props.focusItem(item);
  };

  onActivate = item => {
    if (item.type == "source") {
      this.selectSourceItem(item);
    }
  };

  onExpand = (item, shouldIncludeChildren) => {
    this.setExpanded(item, true, shouldIncludeChildren);
  };

  onCollapse = (item, shouldIncludeChildren) => {
    this.setExpanded(item, false, shouldIncludeChildren);
  };

  setExpanded = (item, isExpanded, shouldIncludeChildren) => {
    // Note that setExpandedState relies on us to clone this Set
    // which is going to be store as-is in the reducer.
    const expanded = new Set(this.props.expanded);

    let changed = false;
    const expandItem = i => {
      const key = this.getKey(i);
      if (isExpanded) {
        changed |= !expanded.has(key);
        expanded.add(key);
      } else {
        changed |= expanded.has(key);
        expanded.delete(key);
      }
    };
    expandItem(item);

    if (shouldIncludeChildren) {
      let parents = [item];
      while (parents.length) {
        const children = [];
        for (const parent of parents) {
          for (const child of this.getChildren(parent)) {
            expandItem(child);
            children.push(child);
          }
        }
        parents = children;
      }
    }
    if (changed) {
      this.props.setExpandedState(expanded);
    }
  };

  isEmpty() {
    return !this.getRoots().length;
  }

  renderEmptyElement(message) {
    return div(
      {
        key: "empty",
        className: "no-sources-message",
      },
      message
    );
  }

  getRoots = () => {
    return this.props.rootItems;
  };

  getKey = item => {
    // As this is used as React key in Tree component,
    // we need to update the key when switching to a new project root
    // otherwise these items won't be updated and will have a buggy padding start.
    const { projectRoot } = this.props;
    if (projectRoot) {
      return projectRoot + item.uniquePath;
    }
    return item.uniquePath;
  };

  getChildren = item => {
    // This is the precial magic that coalesce "empty" folders,
    // i.e folders which have only one sub-folder as children.
    function skipEmptyDirectories(directory) {
      if (directory.type != "directory") {
        return directory;
      }
      if (
        directory.children.length == 1 &&
        directory.children[0].type == "directory"
      ) {
        return skipEmptyDirectories(directory.children[0]);
      }
      return directory;
    }
    if (item.type == "thread") {
      return item.children;
    } else if (item.type == "group" || item.type == "directory") {
      return item.children.map(skipEmptyDirectories);
    }
    return [];
  };

  getParent = item => {
    if (item.type == "thread") {
      return null;
    }
    const { rootItems } = this.props;
    // This is the second magic which skip empty folders
    // (See getChildren comment)
    function skipEmptyDirectories(directory) {
      if (
        directory.type == "group" ||
        directory.type == "thread" ||
        rootItems.includes(directory)
      ) {
        return directory;
      }
      if (
        directory.children.length == 1 &&
        directory.children[0].type == "directory"
      ) {
        return skipEmptyDirectories(directory.parent);
      }
      return directory;
    }
    return skipEmptyDirectories(item.parent);
  };

  renderProjectRootHeader() {
    const { projectRootName, projectRootFullName } = this.props;

    if (!projectRootName) {
      return null;
    }
    return div(
      {
        key: "root",
        className: "sources-clear-root-container",
      },
      button(
        {
          className: "sources-clear-root",
          onClick: () => this.props.clearProjectDirectoryRoot(),
          title: L10N.getFormatStr("removeDirectoryRoot.label"),
        },
        React.createElement(AccessibleImage, {
          className: "back",
        })
      ),
      div({ className: "devtools-separator" }),
      span(
        {
          className: "sources-clear-root-label",
          title: L10N.getFormatStr(
            "directoryRoot.tooltip.label",
            projectRootFullName || projectRootName
          ),
        },
        projectRootName
      )
    );
  }

  renderItem = (item, depth, focused, arrow, expanded) => {
    const { mainThreadHost } = this.props;
    return React.createElement(SourcesTreeItem, {
      arrow,
      item,
      depth,
      focused,
      autoExpand: shouldAutoExpand(item, mainThreadHost),
      expanded,
      focusItem: this.onFocus,
      selectSourceItem: this.selectSourceItem,
      setExpanded: this.setExpanded,
      getParent: this.getParent,
    });
  };

  renderTree() {
    const { expanded, focused } = this.props;

    const treeProps = {
      autoExpandAll: false,
      autoExpandDepth: 1,
      expanded,
      focused,
      getChildren: this.getChildren,
      getParent: this.getParent,
      getKey: this.getKey,
      getRoots: this.getRoots,
      onCollapse: this.onCollapse,
      onExpand: this.onExpand,
      onFocus: this.onFocus,
      isExpanded: item => {
        return this.props.expanded.has(this.getKey(item));
      },
      onActivate: this.onActivate,
      ref: "tree",
      renderItem: this.renderItem,
      preventBlur: true,
    };
    return React.createElement(Tree, treeProps);
  }

  renderPane(child) {
    const { projectRoot } = this.props;
    return div(
      {
        key: "pane",
        className: classnames("sources-pane", {
          "sources-list-custom-root": !!projectRoot,
        }),
      },
      child
    );
  }

  renderFooter() {
    if (this.props.hideIgnoredSources) {
      return footer(
        {
          className: "source-list-footer",
        },
        L10N.getStr("ignoredSourcesHidden"),
        button(
          {
            className: "devtools-togglebutton",
            onClick: () => this.props.setHideOrShowIgnoredSources(false),
            title: L10N.getStr("showIgnoredSources.tooltip.label"),
          },
          L10N.getStr("showIgnoredSources")
        )
      );
    }
    return null;
  }

  renderSettingsButton() {
    const { toolboxDoc } = this.context;
    return React.createElement(
      MenuButton,
      {
        menuId: "sources-tree-settings-menu-button",
        toolboxDoc,
        className:
          "devtools-button command-bar-button debugger-settings-menu-button",
        title: L10N.getStr("sources-settings.button.label"),
        "aria-label": L10N.getStr("sources-settings.button.label"),
      },
      () => this.renderSettingsMenuItems()
    );
  }

  renderSettingsMenuItems() {
    return React.createElement(
      MenuList,
      {
        id: "sources-tree-settings-menu-list",
      },
      React.createElement(MenuItem, {
        key: "debugger-settings-menu-item-hide-ignored-sources",
        className: "menu-item debugger-settings-menu-item-hide-ignored-sources",
        checked: prefs.hideIgnoredSources,
        label: L10N.getStr("settings.hideIgnoredSources.label"),
        tooltip: L10N.getStr("settings.hideIgnoredSources.tooltip"),
        onClick: () =>
          this.props.setHideOrShowIgnoredSources(!prefs.hideIgnoredSources),
      }),
      React.createElement(MenuItem, {
        key: "debugger-settings-menu-item-show-content-scripts",
        className: "menu-item debugger-settings-menu-item-show-content-scripts",
        checked: prefs.showContentScripts,
        label: L10N.getStr("sources-settings.showContentScripts.label"),
        tooltip: L10N.getStr("sources-settings.showContentScripts.tooltip"),
        onClick: () =>
          this.props.setShowContentScripts(!prefs.showContentScripts),
      })
    );
  }

  render() {
    const { projectRoot } = this.props;
    return div(
      {
        key: "pane",
        ref: "pane",
        className: classnames("sources-list", {
          "sources-list-custom-root": !!projectRoot,
          "sources-list-has-overflow": this.state.hasOverflow,
        }),
      },
      this.renderSettingsButton(),
      this.renderProjectRootHeader(),
      this.isEmpty()
        ? this.renderEmptyElement(
            L10N.getStr(
              projectRoot ? "noSourcesInDirectoryRootText" : "noSourcesText"
            )
          )
        : React.createElement(
            Fragment,
            null,
            this.renderTree(),
            this.renderFooter()
          )
    );
  }
}

SourcesTree.contextTypes = {
  toolboxDoc: PropTypes.object,
};

const mapStateToProps = state => {
  return {
    mainThreadHost: getMainThreadHost(state),
    expanded: getExpandedState(state),
    focused: getFocusedSourceItem(state),
    projectRoot: getProjectDirectoryRoot(state),
    rootItems: getSourcesTreeSources(state),
    projectRootName: getProjectDirectoryRootName(state),
    projectRootFullName: getProjectDirectoryRootFullName(state),
    hideIgnoredSources: getHideIgnoredSources(state),
  };
};

export default connect(mapStateToProps, {
  selectMayBePrettyPrintedLocation: actions.selectMayBePrettyPrintedLocation,
  setExpandedState: actions.setExpandedState,
  focusItem: actions.focusItem,
  clearProjectDirectoryRoot: actions.clearProjectDirectoryRoot,
  setHideOrShowIgnoredSources: actions.setHideOrShowIgnoredSources,
  setShowContentScripts: actions.setShowContentScripts,
})(SourcesTree);
