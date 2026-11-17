/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { connect } from "react-redux";

export class ContextMenu extends React.PureComponent {
  constructor(props) {
    super(props);
    this.hideContext = this.hideContext.bind(this);
    this.onShow = this.onShow.bind(this);
    this.onClick = this.onClick.bind(this);
  }

  hideContext() {
    this.props.onUpdate(false);
  }

  onShow() {
    if (this.props.onShow) {
      this.props.onShow();
    }
  }

  componentDidMount() {
    this.onShow();
    setTimeout(() => {
      globalThis.addEventListener("click", this.hideContext);
    }, 0);
  }

  componentWillUnmount() {
    globalThis.removeEventListener("click", this.hideContext);
  }

  onClick(event) {
    // Eat all clicks on the context menu so they don't bubble up to window.
    // This prevents the context menu from closing when clicking disabled items
    // or the separators.
    event.stopPropagation();
  }

  render() {
    // Disabling focus on the menu span allows the first tab to focus on the first menu item instead of the wrapper.
    return (
      // eslint-disable-next-line jsx-a11y/interactive-supports-focus
      <span className="context-menu">
        <ul
          role="menu"
          onClick={this.onClick}
          onKeyDown={this.onClick}
          className="context-menu-list"
        >
          {this.props.options.map((option, i) =>
            option.type === "separator" ? (
              <li key={i} className="separator" role="separator" />
            ) : (
              option.type !== "empty" && (
                <ContextMenuItem
                  key={i}
                  option={option}
                  hideContext={this.hideContext}
                  keyboardAccess={this.props.keyboardAccess}
                />
              )
            )
          )}
        </ul>
      </span>
    );
  }
}

export class _ContextMenuItem extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onClick = this.onClick.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onKeyUp = this.onKeyUp.bind(this);
    this.focusFirst = this.focusFirst.bind(this);
  }

  onClick(event) {
    this.props.hideContext();
    this.props.option.onClick(event);
  }

  // Focus the first menu item if the menu was accessed via the keyboard.
  focusFirst(button) {
    if (this.props.keyboardAccess && button) {
      button.focus();
    }
  }

  // This selects the correct node based on the key pressed
  focusSibling(target, key) {
    const { parentNode } = target;
    const closestSiblingSelector =
      key === "ArrowUp" ? "previousSibling" : "nextSibling";
    if (!parentNode[closestSiblingSelector]) {
      return;
    }
    if (parentNode[closestSiblingSelector].firstElementChild) {
      parentNode[closestSiblingSelector].firstElementChild.focus();
    } else {
      parentNode[closestSiblingSelector][
        closestSiblingSelector
      ].firstElementChild.focus();
    }
  }

  onKeyDown(event) {
    const { option } = this.props;
    switch (event.key) {
      case "Tab":
        // tab goes down in context menu, shift + tab goes up in context menu
        // if we're on the last item, one more tab will close the context menu
        // similarly, if we're on the first item, one more shift + tab will close it
        if (
          (event.shiftKey && option.first) ||
          (!event.shiftKey && option.last)
        ) {
          this.props.hideContext();
        }
        break;
      case "ArrowUp":
      case "ArrowDown":
        event.preventDefault();
        this.focusSibling(event.target, event.key);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        this.props.hideContext();
        option.onClick();
        break;
      case "Escape":
        this.props.hideContext();
        break;
    }
  }

  // Prevents the default behavior of spacebar
  // scrolling the page & auto-triggering buttons.
  onKeyUp(event) {
    if (event.key === " ") {
      event.preventDefault();
    }
  }

  render() {
    const { option } = this.props;
    const className = [option.disabled ? "disabled" : ""].join(" ");
    return (
      <li role="presentation" className="context-menu-item">
        <button
          role="menuitem"
          className={className}
          onClick={this.onClick}
          onKeyDown={this.onKeyDown}
          onKeyUp={this.onKeyUp}
          ref={option.first ? this.focusFirst : null}
          aria-haspopup={option.ariaHasPopup || null}
        >
          <span data-l10n-id={option.string_id || option.id} />
        </button>
      </li>
    );
  }
}

export const ContextMenuItem = connect(state => ({
  Prefs: state.Prefs,
}))(_ContextMenuItem);
