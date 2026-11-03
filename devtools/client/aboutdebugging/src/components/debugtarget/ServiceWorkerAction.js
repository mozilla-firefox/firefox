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

const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);

const {
  getCurrentRuntimeDetails,
} = require("resource://devtools/client/aboutdebugging/src/modules/runtimes-state-helper.js");

const InspectAction = createFactory(
  require("resource://devtools/client/aboutdebugging/src/components/debugtarget/InspectAction.js")
);

const Types = require("resource://devtools/client/aboutdebugging/src/types/index.js");
const {
  SERVICE_WORKER_STATUSES,
} = require("resource://devtools/client/aboutdebugging/src/constants.js");

/**
 * This component displays buttons for service worker.
 */
class ServiceWorkerAction extends PureComponent {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      // Provided by redux state
      runtimeDetails: Types.runtimeDetails.isRequired,
      target: Types.debugTarget.isRequired,
    };
  }

  _renderInspectAction() {
    const { status } = this.props.target.details;
    const shallRenderInspectAction =
      status === SERVICE_WORKER_STATUSES.RUNNING ||
      status === SERVICE_WORKER_STATUSES.REGISTERING;

    if (!shallRenderInspectAction) {
      return null;
    }

    const { canDebugServiceWorkers } = this.props.runtimeDetails;
    return Localized(
      {
        id: "about-debugging-worker-inspect-action-disabled",
        attrs: {
          // Show an explanatory title only if the action is disabled.
          title: !canDebugServiceWorkers,
        },
      },
      InspectAction({
        disabled: !canDebugServiceWorkers,
        dispatch: this.props.dispatch,
        target: this.props.target,
      })
    );
  }

  _getStatusLocalizationId(status) {
    switch (status) {
      case SERVICE_WORKER_STATUSES.REGISTERING.toLowerCase():
        return "about-debugging-worker-status-registering";
      case SERVICE_WORKER_STATUSES.RUNNING.toLowerCase():
        return "about-debugging-worker-status-running";
      case SERVICE_WORKER_STATUSES.STOPPED.toLowerCase():
        return "about-debugging-worker-status-stopped";
      default:
        // Assume status is stopped for unknown status value.
        return "about-debugging-worker-status-stopped";
    }
  }

  _renderStatus() {
    const status = this.props.target.details.status.toLowerCase();
    const statusClassName =
      status === SERVICE_WORKER_STATUSES.RUNNING.toLowerCase()
        ? "service-worker-action__status--running"
        : "";

    return Localized(
      {
        id: this._getStatusLocalizationId(status),
      },
      dom.span(
        {
          className: `service-worker-action__status qa-worker-status ${statusClassName}`,
        },
        status
      )
    );
  }

  render() {
    return dom.div(
      {
        className: "service-worker-action",
      },
      this._renderStatus(),
      this._renderInspectAction()
    );
  }
}

const mapStateToProps = state => {
  return {
    runtimeDetails: getCurrentRuntimeDetails(state.runtimes),
  };
};

module.exports = connect(mapStateToProps)(ServiceWorkerAction);
