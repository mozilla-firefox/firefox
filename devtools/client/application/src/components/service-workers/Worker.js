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
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const {
  a,
  img,
  p,
  section,
  span,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const {
  getUnicodeUrlPath,
} = require("resource://devtools/client/shared/unicode-url.js");

const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);
const {
  l10n,
} = require("resource://devtools/client/application/src/modules/l10n.js");

const {
  services,
} = require("resource://devtools/client/application/src/modules/application-services.js");
const Types = require("resource://devtools/client/application/src/types/index.js");

const {
  startWorker,
} = require("resource://devtools/client/application/src/actions/workers.js");

const UIButton = createFactory(
  require("resource://devtools/client/application/src/components/ui/UIButton.js")
);

/**
 * This component is dedicated to display a worker, more accurately a service worker, in
 * the list of workers displayed in the application panel. It displays information about
 * the worker as well as action links and buttons to interact with the worker (e.g. debug,
 * unregister, update etc...).
 */
class Worker extends PureComponent {
  static get propTypes() {
    return {
      isDebugEnabled: PropTypes.bool.isRequired,
      worker: PropTypes.shape(Types.worker).isRequired,
      // this prop get automatically injected via `connect`
      dispatch: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.debug = this.debug.bind(this);
    this.viewSource = this.viewSource.bind(this);
    this.start = this.start.bind(this);
  }

  debug() {
    if (!this.isRunning()) {
      console.log("Service workers cannot be debugged if they are not running");
      return;
    }

    services.openWorkerInDebugger(this.props.worker.workerDescriptorFront);
  }

  viewSource() {
    if (!this.isRunning()) {
      console.log(
        "Service workers cannot be inspected if they are not running"
      );
      return;
    }

    services.viewWorkerSource(this.props.worker.workerDescriptorFront);
  }

  start() {
    if (!this.isActive() || this.isRunning()) {
      console.log("Running or inactive service workers cannot be started");
      return;
    }

    this.props.dispatch(startWorker(this.props.worker));
  }

  isRunning() {
    // We know the worker is running if it has a worker actor.
    return !!this.props.worker.workerDescriptorFront;
  }

  isActive() {
    return this.props.worker.state === Ci.nsIServiceWorkerInfo.STATE_ACTIVATED;
  }

  getLocalizedStatus() {
    if (this.isActive() && this.isRunning()) {
      return l10n.getString("serviceworker-worker-status-running");
    } else if (this.isActive()) {
      return l10n.getString("serviceworker-worker-status-stopped");
    }
    // NOTE: this is already localized by the service worker front
    // (strings are in debugger.properties)
    return this.props.worker.stateText;
  }

  getClassNameForStatus() {
    const { state } = this.props.worker;

    switch (state) {
      case Ci.nsIServiceWorkerInfo.STATE_PARSED:
      case Ci.nsIServiceWorkerInfo.STATE_INSTALLING:
        return "worker__status--installing";
      case Ci.nsIServiceWorkerInfo.STATE_INSTALLED:
      case Ci.nsIServiceWorkerInfo.STATE_ACTIVATING:
        return "worker__status--waiting";
      case Ci.nsIServiceWorkerInfo.STATE_ACTIVATED:
        return "worker__status--active";
    }

    return "worker__status--default";
  }

  formatSource(source) {
    const parts = source.split("/");
    return getUnicodeUrlPath(parts[parts.length - 1]);
  }

  renderInspectLink(url) {
    // avoid rendering the inspect link if sw is not running
    const isDisabled = !this.isRunning();
    // view source instead of debugging when debugging sw is not available
    const callbackFn = this.props.isDebugEnabled ? this.debug : this.viewSource;

    const sourceUrl = span(
      { className: "js-source-url" },
      this.formatSource(url)
    );

    return isDisabled
      ? sourceUrl
      : a(
          {
            onClick: callbackFn,
            title: url,
            href: "#",
            className: "js-inspect-link",
          },
          sourceUrl,
          "\u00A0", // &nbsp;
          Localized(
            {
              id: "serviceworker-worker-inspect-icon",
              attrs: {
                alt: true,
              },
            },
            img({
              src: "chrome://devtools/skin/images/application-debug.svg",
            })
          )
        );
  }

  renderStartButton() {
    // avoid rendering the button at all for workers that are either running,
    // or in a state that prevents them from starting (like waiting)
    if (this.isRunning() || !this.isActive()) {
      return null;
    }

    return Localized(
      { id: "serviceworker-worker-start3" },
      UIButton({
        onClick: this.start,
        className: `js-start-button`,
        size: "micro",
      })
    );
  }

  render() {
    const { worker } = this.props;
    const statusText = this.getLocalizedStatus();
    const statusClassName = this.getClassNameForStatus();

    return section(
      { className: "worker js-sw-worker" },
      p(
        { className: "worker__icon" },
        img({
          className: "worker__icon-image",
          src: "chrome://devtools/skin/images/debugging-workers.svg",
        })
      ),
      p({ className: "worker__source" }, this.renderInspectLink(worker.url)),
      p(
        { className: "worker__misc" },
        span(
          { className: `js-worker-status worker__status ${statusClassName}` },
          statusText
        ),
        " ",
        this.renderStartButton()
      )
    );
  }
}

const mapDispatchToProps = dispatch => ({ dispatch });
module.exports = connect(mapDispatchToProps)(Worker);
