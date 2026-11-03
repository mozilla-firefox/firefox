/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  openTrustedLink,
} = require("resource://devtools/client/shared/link.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  a,
  article,
  footer,
  h1,
  p,
  ul,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);

const Types = require("resource://devtools/client/application/src/types/index.js");
const Registration = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/Registration.js")
);

/**
 * This component handles the list of service workers displayed in the application panel
 * and also displays a suggestion to use about debugging for debugging other service
 * workers.
 */
class RegistrationList extends PureComponent {
  static get propTypes() {
    return {
      canDebugWorkers: PropTypes.bool.isRequired,
      registrations: Types.registrationArray.isRequired,
    };
  }

  render() {
    const { canDebugWorkers, registrations } = this.props;

    return [
      article(
        {
          className: "registrations-container",
          key: "registrations-container",
        },
        Localized(
          { id: "serviceworker-list-header" },
          h1({
            className: "app-page__title",
          })
        ),
        ul(
          { className: "registrations-container__list" },
          registrations.map(registration =>
            Registration({
              key: registration.id,
              isDebugEnabled: canDebugWorkers,
              registration,
              className: "registrations-container__item",
            })
          )
        )
      ),

      footer(
        { className: "aboutdebugging-plug" },
        Localized(
          {
            id: "serviceworker-list-aboutdebugging",
            key: "serviceworkerlist-footer",
            a: a({
              className: "aboutdebugging-plug__link",
              onClick: () => openTrustedLink("about:debugging#workers"),
            }),
          },
          p({})
        )
      ),
    ];
  }
}

// Exports
module.exports = RegistrationList;
