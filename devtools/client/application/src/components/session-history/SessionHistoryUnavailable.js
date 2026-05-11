/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  article,
  aside,
  div,
  h1,
  img,
  p,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);

/**
 * This component displays help information when session history diagrams
 * aren't supported for the current target.
 */
class SessionHistoryUnavailable extends PureComponent {
  render() {
    return article(
      { className: "app-page__icon-container js-session-history-unavailable" },
      aside(
        {},
        Localized(
          {
            id: "sidebar-item-session-history",
            attrs: {
              alt: true,
            },
          },
          img({
            className: "app-page__icon",
            src: "chrome://devtools/skin/images/application-session-history.svg",
          })
        )
      ),
      div(
        {},
        Localized(
          {
            id: "session-history-unavailable",
          },
          h1({ className: "app-page__title" })
        ),
        Localized({ id: "session-history-target-unsupported" }, p({}))
      )
    );
  }
}

// Exports
module.exports = SessionHistoryUnavailable;
