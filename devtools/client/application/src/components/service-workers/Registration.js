/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");

const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const {
  article,
  aside,
  h2,
  header,
  li,
  p,
  time,
  ul,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const {
  getUnicodeUrl,
} = require("resource://devtools/client/shared/unicode-url.js");

const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);

const Types = require("resource://devtools/client/application/src/types/index.js");

const {
  unregisterWorker,
} = require("resource://devtools/client/application/src/actions/workers.js");

const UIButton = createFactory(
  require("resource://devtools/client/application/src/components/ui/UIButton.js")
);

const Worker = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/Worker.js")
);

/**
 * This component is dedicated to display a service worker registration, along
 * the list of attached workers to it.
 * It displays information about the registration as well as an Unregister
 * button.
 */
class Registration extends PureComponent {
  static get propTypes() {
    return {
      className: PropTypes.string,
      isDebugEnabled: PropTypes.bool.isRequired,
      registration: PropTypes.shape(Types.registration).isRequired,
      // this prop get automatically injected via `connect`
      dispatch: PropTypes.func.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.unregister = this.unregister.bind(this);
  }

  unregister() {
    this.props.dispatch(unregisterWorker(this.props.registration));
  }

  isActive() {
    const { workers } = this.props.registration;
    return workers.some(
      x => x.state === Ci.nsIServiceWorkerInfo.STATE_ACTIVATED
    );
  }

  formatScope(scope) {
    const [, remainder] = getUnicodeUrl(scope).split("://");
    // remove the last slash from the url, if present
    // or return the full scope if there's no remainder
    return remainder ? remainder.replace(/\/$/, "") : scope;
  }

  render() {
    const { registration, isDebugEnabled, className } = this.props;

    const unregisterButton = this.isActive()
      ? Localized(
          { id: "serviceworker-worker-unregister" },
          UIButton({
            onClick: this.unregister,
            className: "js-unregister-button",
          })
        )
      : null;

    const lastUpdated = registration.lastUpdateTime
      ? Localized(
          {
            id: "serviceworker-worker-updated",
            // XXX: $date should normally be a Date object, but we pass the timestamp as a
            // workaround. See Bug 1465718. registration.lastUpdateTime is in microseconds,
            // convert to a valid timestamp in milliseconds by dividing by 1000.
            $date: registration.lastUpdateTime / 1000,
            time: time({ className: "js-sw-updated" }),
          },
          p({ className: "registration__updated-time" })
        )
      : null;

    const scope = h2(
      {
        title: registration.scope,
        className: "registration__scope js-sw-scope devtools-ellipsis-text",
      },
      this.formatScope(registration.scope)
    );

    return li(
      { className: className ? className : "" },
      article(
        { className: "registration js-sw-container" },
        header({ className: "registration__header" }, scope, lastUpdated),
        aside({ className: "registration__controls" }, unregisterButton),
        // render list of workers
        ul(
          { className: "registration__workers" },
          registration.workers.map(worker => {
            return li(
              {
                key: worker.id,
                className: "registration__workers-item",
              },
              Worker({
                worker,
                isDebugEnabled,
              })
            );
          })
        )
      )
    );
  }
}

const mapDispatchToProps = dispatch => ({ dispatch });
module.exports = connect(mapDispatchToProps)(Registration);
