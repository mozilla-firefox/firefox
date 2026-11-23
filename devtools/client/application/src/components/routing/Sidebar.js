/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  aside,
  ul,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const SidebarItem = createFactory(
  require("resource://devtools/client/application/src/components/routing/SidebarItem.js")
);

const Types = require("resource://devtools/client/application/src/types/index.js");
const {
  PAGE_TYPES,
} = require("resource://devtools/client/application/src/constants.js");

class Sidebar extends PureComponent {
  static get propTypes() {
    return {
      // this prop is automatically injected via connect
      selectedPage: Types.page.isRequired,
    };
  }

  render() {
    const navItems = [PAGE_TYPES.SERVICE_WORKERS, PAGE_TYPES.MANIFEST];
    if (
      Services.prefs.getBoolPref(
        "devtools.application.sessionHistory.enabled",
        false
      )
    ) {
      navItems.push(PAGE_TYPES.SESSION_HISTORY);
    }

    if (Services.prefs.getBoolPref("dom.speculation_rules.enabled", false)) {
      navItems.push(PAGE_TYPES.SPECULATIVE_LOADS);
    }

    const isSelected = page => {
      return page === this.props.selectedPage;
    };

    return aside(
      {
        className: "sidebar js-sidebar",
      },
      ul(
        {
          className: "sidebar__list",
        },
        navItems.map(page => {
          return SidebarItem({
            page,
            key: `sidebar-item-${page}`,
            isSelected: isSelected(page),
          });
        })
      )
    );
  }
}

function mapStateToProps(state) {
  return {
    selectedPage: state.ui.selectedPage,
  };
}

module.exports = connect(mapStateToProps)(Sidebar);
