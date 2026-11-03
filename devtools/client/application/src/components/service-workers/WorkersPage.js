/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  section,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const Types = require("resource://devtools/client/application/src/types/index.js");
const RegistrationList = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/RegistrationList.js")
);
const RegistrationListEmpty = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/RegistrationListEmpty.js")
);

class WorkersPage extends PureComponent {
  static get propTypes() {
    return {
      // mapped from state
      canDebugWorkers: PropTypes.bool.isRequired,
      domain: PropTypes.string.isRequired,
      registrations: Types.registrationArray.isRequired,
    };
  }

  render() {
    const { canDebugWorkers, domain, registrations } = this.props;

    // Filter out workers from other domains
    const domainWorkers = registrations.filter(
      x => !!x.workers.length && new URL(x.workers[0].url).hostname === domain
    );
    const isListEmpty = domainWorkers.length === 0;

    return section(
      {
        className: `app-page js-service-workers-page ${
          isListEmpty ? "app-page--empty" : ""
        }`,
      },
      isListEmpty
        ? RegistrationListEmpty({})
        : RegistrationList({
            canDebugWorkers,
            registrations: domainWorkers,
          })
    );
  }
}

function mapStateToProps(state) {
  return {
    canDebugWorkers: state.workers.canDebugWorkers,
    domain: state.page.domain,
    registrations: state.workers.list,
  };
}

// Exports
module.exports = connect(mapStateToProps)(WorkersPage);
