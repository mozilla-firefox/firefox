/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const targetActions = require("resource://devtools/shared/commands/target/actions/targets.js");
const webconsoleActions = require("resource://devtools/client/webconsole/actions/index.js");

const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const targetSelectors = require("resource://devtools/shared/commands/target/selectors/targets.js");

loader.lazyGetter(this, "TARGET_TYPES", function () {
  return require("resource://devtools/shared/commands/target/target-command.js")
    .TYPES;
});

// Additional Components
const MenuButton = createFactory(
  require("resource://devtools/client/shared/components/menu/MenuButton.js")
);

loader.lazyGetter(this, "MenuItem", function () {
  return createFactory(
    require("resource://devtools/client/shared/components/menu/MenuItem.js")
  );
});

loader.lazyGetter(this, "MenuList", function () {
  return createFactory(
    require("resource://devtools/client/shared/components/menu/MenuList.js")
  );
});

class EvaluationContextSelector extends Component {
  static get propTypes() {
    return {
      selectTarget: PropTypes.func.isRequired,
      onContextChange: PropTypes.func.isRequired,
      selectedTarget: PropTypes.object,
      lastTargetRefresh: PropTypes.number,
      targets: PropTypes.array,
      webConsoleUI: PropTypes.object.isRequired,
    };
  }

  shouldComponentUpdate(nextProps) {
    if (this.props.selectedTarget !== nextProps.selectedTarget) {
      return true;
    }

    if (this.props.lastTargetRefresh !== nextProps.lastTargetRefresh) {
      return true;
    }

    if (this.props.targets.length !== nextProps.targets.length) {
      return true;
    }

    for (let i = 0; i < nextProps.targets.length; i++) {
      const target = this.props.targets[i];
      const nextTarget = nextProps.targets[i];
      if (target.url != nextTarget.url || target.name != nextTarget.name) {
        return true;
      }
    }
    return false;
  }

  componentDidUpdate(prevProps) {
    if (this.props.selectedTarget !== prevProps.selectedTarget) {
      this.props.onContextChange();
    }
  }

  getIcon(target) {
    if (target.targetType === TARGET_TYPES.FRAME) {
      return "chrome://devtools/content/shared/components/images/globe-small.svg";
    }

    if (
      target.targetType === TARGET_TYPES.WORKER ||
      target.targetType === TARGET_TYPES.SHARED_WORKER ||
      target.targetType === TARGET_TYPES.SERVICE_WORKER
    ) {
      return "chrome://devtools/content/shared/components/images/worker.svg";
    }

    if (target.targetType === TARGET_TYPES.PROCESS) {
      return "chrome://devtools/content/shared/components/images/window.svg";
    }

    if (target.targetType === TARGET_TYPES.CONTENT_SCRIPT) {
      return "chrome://devtools/content/shared/components/images/extension.svg";
    }

    return null;
  }

  renderMenuItem(target, indented = false) {
    const { selectTarget, selectedTarget } = this.props;

    // When debugging a Web Extension, the top level target is always the fallback document.
    // It isn't really a top level document as it won't be the parent of any other.
    // So only print its name.
    const label =
      target.isTopLevel && !target.commands.descriptorFront.isWebExtension
        ? l10n.getStr("webconsole-input-selector-top")
        : target.name;

    return MenuItem({
      key: `webconsole-evaluation-selector-item-${target.actorID}`,
      className: `menu-item webconsole-evaluation-selector-item ${
        indented ? "indented" : ""
      }`,
      type: "checkbox",
      checked: selectedTarget ? selectedTarget == target : target.isTopLevel,
      label,
      tooltip: target.url || target.name,
      icon: this.getIcon(target),
      onClick: () => selectTarget(target.actorID),
    });
  }

  renderMenuItems() {
    const { targets } = this.props;

    // Let's sort the targets (using "numeric" so Content processes are ordered by PID).
    const collator = new Intl.Collator("en", { numeric: true });
    targets.sort((a, b) => collator.compare(a.name, b.name));

    // When in Browser Toolbox, we want to display the process targets with the frames
    // in the same process as a group
    // e.g.
    //     |------------------------------|
    //     | Top                          |
    //     | -----------------------------|
    //     | (pid 1234) priviledgedabout  |
    //     | New Tab                      |
    //     | -----------------------------|
    //     | (pid 5678) web               |
    //     | cnn.com                      |
    //     | -----------------------------|
    //     | RemoteSettingWorker.js       |
    //     |------------------------------|
    //

    const { webConsoleUI } = this.props;
    const handleProcessTargets =
      webConsoleUI.isBrowserConsole || webConsoleUI.isBrowserToolboxConsole;

    const processTargets = [];
    const frameTargets = new Set();
    const contentScriptTargets = new Set();
    const workerTargets = new Set();
    let topTarget = null;

    for (const target of targets) {
      if (target.isTopLevel) {
        topTarget = target;
        continue;
      }
      switch (target.targetType) {
        case TARGET_TYPES.PROCESS:
          processTargets.push(target);
          break;
        case TARGET_TYPES.FRAME:
          frameTargets.add(target);
          break;
        case TARGET_TYPES.CONTENT_SCRIPT:
          contentScriptTargets.add(target);
          break;
        case TARGET_TYPES.WORKER:
        case TARGET_TYPES.SHARED_WORKER:
        case TARGET_TYPES.SERVICE_WORKER:
          workerTargets.add(target);
          break;
        default:
          console.warn(
            "Unsupported target type in the evalutiong context selector",
            target.targetType
          );
      }
    }

    const items = [];

    const renderFrameWithContentScripts = frameTarget => {
      items.push(this.renderMenuItem(frameTarget));

      // Render under each frame, its related web extension content scripts,...
      for (const contentScriptTarget of contentScriptTargets) {
        if (contentScriptTarget.innerWindowId != frameTarget.innerWindowId) {
          continue;
        }
        items.push(this.renderMenuItem(contentScriptTarget, true));
        contentScriptTargets.delete(contentScriptTarget);
      }

      // ...as well as all its related workers
      for (const workerTarget of workerTargets) {
        if (
          workerTarget.relatedDocumentInnerWindowId != frameTarget.innerWindowId
        ) {
          continue;
        }
        items.push(this.renderMenuItem(workerTarget, true));
        workerTargets.delete(workerTarget);
      }
    };

    // Note that while debugging popups, we might have a small period
    // of time where we don't have any top level target when we reload
    // the original tab
    if (topTarget) {
      renderFrameWithContentScripts(topTarget);
    }

    if (handleProcessTargets) {
      const sortedProcessTargets = processTargets.sort(
        (a, b) => a.processID < b.processID
      );
      for (const target of sortedProcessTargets) {
        items.push(
          dom.hr({
            role: "menuseparator",
            key: `process-separator-${target.actorID}`,
          }),
          this.renderMenuItem(target)
        );

        for (const frameTarget of frameTargets) {
          if (frameTarget.processID != target.processID) {
            continue;
          }
          renderFrameWithContentScripts(frameTarget);
          frameTargets.delete(frameTarget);
        }
      }
    }

    // Render all targets when running in regular non-browser-console/toolbox,
    // but also possibly render any leftover frame which can't be matched to any Process ID.
    const sortedFrames = [...frameTargets].sort(
      (a, b) => a.innerWindowID < b.innerWindowID
    );
    if (sortedFrames.length) {
      items.push(dom.hr({ role: "menuseparator", key: `frame-separator` }));
    }
    for (const frameTarget of sortedFrames) {
      renderFrameWithContentScripts(frameTarget);
    }

    // All content scripts and workers should have matched their related frame target in `renderFrameWithContentScripts`,
    // but just in case, display any leftover.
    for (const contentScriptTarget of contentScriptTargets) {
      items.push(this.renderMenuItem(contentScriptTarget));
    }
    const sortedWorkers = [...workerTargets].sort((a, b) => a.url < b.url);
    if (sortedWorkers.length) {
      items.push(dom.hr({ role: "menuseparator", key: `worker-separator` }));
    }
    for (const workerTarget of sortedWorkers) {
      items.push(this.renderMenuItem(workerTarget));
    }

    return MenuList(
      { id: "webconsole-console-evaluation-context-selector-menu-list" },
      items
    );
  }

  getLabel() {
    const { selectedTarget } = this.props;

    // When debugging a Web Extension, the top level target is always the fallback document.
    // It isn't really a top level document as it won't be the parent of any other.
    // So only print its name.
    if (
      !selectedTarget ||
      (selectedTarget.isTopLevel &&
        !selectedTarget.commands.descriptorFront.isWebExtension)
    ) {
      return l10n.getStr("webconsole-input-selector-top");
    }

    return selectedTarget.name;
  }

  render() {
    const { webConsoleUI, targets, selectedTarget } = this.props;

    // Don't render if there's only one target.
    // Also bail out if the console is being destroyed (where WebConsoleUI.wrapper gets
    // nullified).
    if (targets.length <= 1 || !webConsoleUI.wrapper) {
      return null;
    }

    const doc = webConsoleUI.document;
    const { toolbox } = webConsoleUI.wrapper;

    return MenuButton(
      {
        menuId: "webconsole-input-evaluationsButton",
        toolboxDoc: toolbox ? toolbox.doc : doc,
        label: this.getLabel(),
        className:
          "webconsole-evaluation-selector-button devtools-button devtools-dropdown-button" +
          (selectedTarget && !selectedTarget.isTopLevel ? " checked" : ""),
        title: l10n.getStr("webconsole-input-selector-tooltip"),
      },
      // We pass the children in a function so we don't require the MenuItem and MenuList
      // components until we need to display them (i.e. when the button is clicked).
      () => this.renderMenuItems()
    );
  }
}

const toolboxConnected = connect(
  state => ({
    targets: targetSelectors.getToolboxTargets(state),
    selectedTarget: targetSelectors.getSelectedTarget(state),
    lastTargetRefresh: targetSelectors.getLastTargetRefresh(state),
  }),
  dispatch => ({
    selectTarget: actorID => dispatch(targetActions.selectTarget(actorID)),
  }),
  undefined,
  { storeKey: "target-store" }
)(EvaluationContextSelector);

module.exports = connect(
  state => state,
  dispatch => ({
    onContextChange: () => {
      dispatch(
        webconsoleActions.updateInstantEvaluationResultForCurrentExpression()
      );
      dispatch(webconsoleActions.autocompleteClear());
    },
  })
)(toolboxConnected);
