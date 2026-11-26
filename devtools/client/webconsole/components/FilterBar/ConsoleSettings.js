/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const actions = require("resource://devtools/client/webconsole/actions/index.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

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

function getLabelAndTooltip(id) {
  const { label, title } = l10n.getAttributes(id);
  return { label, tooltip: title };
}

class ConsoleSettings extends Component {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      eagerEvaluation: PropTypes.bool.isRequired,
      groupSimilar: PropTypes.bool.isRequired,
      persistLogs: PropTypes.bool.isRequired,
      timestampsVisible: PropTypes.bool.isRequired,
      webConsoleUI: PropTypes.object.isRequired,
      autocomplete: PropTypes.bool.isRequired,
      enableNetworkMonitoring: PropTypes.bool.isRequired,
    };
  }

  renderMenuItems() {
    const {
      dispatch,
      eagerEvaluation,
      groupSimilar,
      persistLogs,
      timestampsVisible,
      autocomplete,
      webConsoleUI,
      enableNetworkMonitoring,
    } = this.props;

    const items = [];

    if (
      !webConsoleUI.isBrowserConsole &&
      !webConsoleUI.isBrowserToolboxConsole
    ) {
      // Persist Logs
      items.push(
        MenuItem({
          key: "webconsole-console-settings-menu-item-persistent-logs",
          checked: persistLogs,
          className:
            "menu-item webconsole-console-settings-menu-item-persistentLogs",
          ...getLabelAndTooltip("webconsole-settings-persistent-logs"),
          onClick: () => dispatch(actions.persistToggle()),
        })
      );
    }

    if (webConsoleUI.isBrowserConsole || webConsoleUI.isBrowserToolboxConsole) {
      // Enable network monitoring
      items.push(
        MenuItem({
          key: "webconsole-console-settings-menu-item-enable-network-monitoring",
          checked: enableNetworkMonitoring,
          className:
            "menu-item webconsole-console-settings-menu-item-enableNetworkMonitoring",
          ...getLabelAndTooltip("browserconsole-settings-network-monitoring"),
          onClick: () => dispatch(actions.networkMonitoringToggle()),
        })
      );
    }

    // Timestamps
    items.push(
      MenuItem({
        key: "webconsole-console-settings-menu-item-timestamps",
        checked: timestampsVisible,
        className: "menu-item webconsole-console-settings-menu-item-timestamps",
        ...getLabelAndTooltip("webconsole-settings-timestamps"),
        onClick: () => dispatch(actions.timestampsToggle()),
      })
    );

    // Warning Groups
    items.push(
      MenuItem({
        key: "webconsole-console-settings-menu-item-warning-groups",
        checked: groupSimilar,
        className:
          "menu-item webconsole-console-settings-menu-item-warning-groups",
        ...getLabelAndTooltip("webconsole-settings-warning-groups"),
        onClick: () => dispatch(actions.groupSimilarMessagesToggle()),
      })
    );

    // autocomplete
    items.push(
      MenuItem({
        key: "webconsole-console-settings-menu-item-autocomplete",
        checked: autocomplete,
        className:
          "menu-item webconsole-console-settings-menu-item-autocomplete",
        ...getLabelAndTooltip("webconsole-settings-autocomplete"),
        onClick: () => dispatch(actions.autocompleteToggle()),
      })
    );

    // Eager Evaluation
    items.push(
      MenuItem({
        key: "webconsole-console-settings-menu-item-eager-evaluation",
        checked: eagerEvaluation,
        className:
          "menu-item webconsole-console-settings-menu-item-eager-evaluation",
        ...getLabelAndTooltip("webconsole-settings-instant-evaluation"),
        onClick: () => dispatch(actions.eagerEvaluationToggle()),
      })
    );

    return MenuList({ id: "webconsole-console-settings-menu-list" }, items);
  }

  render() {
    const { webConsoleUI } = this.props;
    const doc = webConsoleUI.document;
    const { toolbox } = webConsoleUI.wrapper;

    return MenuButton(
      {
        menuId: "webconsole-console-settings-menu-button",
        toolboxDoc: toolbox ? toolbox.doc : doc,
        className: "devtools-button webconsole-console-settings-menu-button",
        title: l10n.getStr("webconsole-settings-button-tooltip"),
      },
      // We pass the children in a function so we don't require the MenuItem and MenuList
      // components until we need to display them (i.e. when the button is clicked).
      () => this.renderMenuItems()
    );
  }
}

module.exports = ConsoleSettings;
