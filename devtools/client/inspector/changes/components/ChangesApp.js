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
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const CSSDeclaration = createFactory(
  require("resource://devtools/client/inspector/changes/components/CSSDeclaration.js")
);
const {
  getChangesTree,
} = require("resource://devtools/client/inspector/changes/selectors/changes.js");
const {
  getSourceForDisplay,
} = require("resource://devtools/client/inspector/changes/utils/changes-utils.js");
const {
  getStr,
} = require("resource://devtools/client/inspector/changes/utils/l10n.js");

class ChangesApp extends PureComponent {
  static get propTypes() {
    return {
      // Nested CSS rule tree structure of CSS changes grouped by source (stylesheet)
      changesTree: PropTypes.object.isRequired,
      // Event handler for "contextmenu" event
      onContextMenu: PropTypes.func.isRequired,
      // Event handler for click on "Copy All Changes" button
      onCopyAllChanges: PropTypes.func.isRequired,
      // Event handler for click on "Copy Rule" button
      onCopyRule: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);
  }

  renderCopyAllChangesButton() {
    const button = dom.button(
      {
        className: "changes__copy-all-changes-button",
        onClick: e => {
          e.stopPropagation();
          this.props.onCopyAllChanges();
        },
        title: getStr("changes-copy-all-changes-button-title"),
      },
      getStr("changes-copy-all-changes-button")
    );

    return dom.div({ className: "changes__toolbar" }, button);
  }

  renderCopyButton(ruleId) {
    return dom.button(
      {
        className: "changes__copy-rule-button",
        onClick: e => {
          e.stopPropagation();
          this.props.onCopyRule(ruleId);
        },
        title: getStr("changes-copy-rule-button-title"),
      },
      getStr("changes-copy-rule-button")
    );
  }

  renderDeclarations(remove = [], add = []) {
    const removals = remove
      // Sorting changed declarations in the order they appear in the Rules view.
      .sort((a, b) => a.index > b.index)
      .map(({ property, value, index }) => {
        return CSSDeclaration({
          key: "remove-" + property + index,
          className: "level diff-remove",
          property,
          value,
        });
      });

    const additions = add
      // Sorting changed declarations in the order they appear in the Rules view.
      .sort((a, b) => a.index > b.index)
      .map(({ property, value, index }) => {
        return CSSDeclaration({
          key: "add-" + property + index,
          className: "level diff-add",
          property,
          value,
        });
      });

    return [removals, additions];
  }

  renderRule(ruleId, rule, level = 0) {
    const diffClass = rule.isNew ? "diff-add" : "";
    return dom.div(
      {
        key: ruleId,
        className: "changes__rule devtools-monospace",
        "data-rule-id": ruleId,
        style: {
          "--diff-level": level,
        },
      },
      this.renderSelectors(rule.selectors, rule.isNew),
      this.renderCopyButton(ruleId),
      // Render any nested child rules if they exist.
      rule.children.map(childRule => {
        return this.renderRule(childRule.ruleId, childRule, level + 1);
      }),
      // Render any changed CSS declarations.
      this.renderDeclarations(rule.remove, rule.add),
      // Render the closing bracket with a diff marker if necessary.
      dom.div({ className: `level ${diffClass}` }, "}")
    );
  }

  /**
   * Return an array of React elements for the rule's selector.
   *
   * @param  {Array} selectors
   *         List of strings as versions of this rule's selector over time.
   * @param  {boolean} isNewRule
   *         Whether the rule was created at runtime.
   * @return {Array}
   */
  renderSelectors(selectors, isNewRule) {
    const selectorDiffClassMap = new Map();

    // The selectors array has just one item if it hasn't changed. Render it as-is.
    // If the rule was created at runtime, mark the single selector as added.
    // If it has two or more items, the first item was the original selector (mark as
    // removed) and the last item is the current selector (mark as added).
    if (selectors.length === 1) {
      selectorDiffClassMap.set(selectors[0], isNewRule ? "diff-add" : "");
    } else if (selectors.length >= 2) {
      selectorDiffClassMap.set(selectors[0], "diff-remove");
      selectorDiffClassMap.set(selectors[selectors.length - 1], "diff-add");
    }

    const elements = [];

    for (const [selector, diffClass] of selectorDiffClassMap) {
      elements.push(
        dom.div(
          {
            key: selector,
            className: `level changes__selector ${diffClass}`,
            title: selector,
          },
          selector,
          dom.span({}, " {")
        )
      );
    }

    return elements;
  }

  renderDiff(changes = {}) {
    // Render groups of style sources: stylesheets and element style attributes.
    return Object.entries(changes).map(([sourceId, source]) => {
      const path = getSourceForDisplay(source);
      const { href, rules, isFramed } = source;

      return dom.div(
        {
          key: sourceId,
          "data-source-id": sourceId,
          className: "source",
        },
        dom.div(
          {
            className: "href",
            title: href,
          },
          dom.span({}, path),
          isFramed && this.renderFrameBadge(href)
        ),
        // Render changed rules within this source.
        Object.entries(rules).map(([ruleId, rule]) => {
          return this.renderRule(ruleId, rule);
        })
      );
    });
  }

  renderFrameBadge(href = "") {
    return dom.span(
      {
        className: "inspector-badge",
        title: href,
      },
      getStr("changes-iframe-label")
    );
  }

  renderEmptyState() {
    return dom.div(
      { className: "devtools-sidepanel-no-result" },
      dom.p({}, getStr("changes-no-changes")),
      dom.p({}, getStr("changes-no-changes-description"))
    );
  }

  render() {
    const hasChanges = !!Object.keys(this.props.changesTree).length;
    return dom.div(
      {
        className: "theme-sidebar inspector-tabpanel",
        id: "sidebar-panel-changes",
        role: "document",
        tabIndex: "0",
        onContextMenu: this.props.onContextMenu,
      },
      !hasChanges && this.renderEmptyState(),
      hasChanges && this.renderCopyAllChangesButton(),
      hasChanges && this.renderDiff(this.props.changesTree)
    );
  }
}

const mapStateToProps = state => {
  return {
    changesTree: getChangesTree(state.changes),
  };
};

module.exports = connect(mapStateToProps)(ChangesApp);
