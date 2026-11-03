/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Import libs
const { shallow } = require("enzyme");
const { createFactory } = require("react");
// Import test helpers
const {
  setupStore,
} = require("resource://devtools/client/application/test/node/helpers.js");

const {
  WORKER_RUNNING,
  WORKER_STOPPED,
  WORKER_WAITING,
} = require("resource://devtools/client/application/test/node/fixtures/data/constants.js");

const Worker = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/Worker.js")
);

describe("Worker", () => {
  it("Renders the expected snapshot for a running worker", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Worker({
        isDebugEnabled: true,
        worker: WORKER_RUNNING,
        store,
      })
    ).dive();

    // ensure proper status
    expect(wrapper.find(".js-worker-status").text()).toBe(
      "serviceworker-worker-status-running"
    );
    // check that Start button is not available
    expect(wrapper.find(".js-start-button")).toHaveLength(0);

    expect(wrapper).toMatchSnapshot();
  });

  it("Renders the expected snapshot for a stopped worker", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Worker({
        isDebugEnabled: true,
        worker: WORKER_STOPPED,
        store,
      })
    ).dive();

    // ensure proper status
    expect(wrapper.find(".js-worker-status").text()).toBe(
      "serviceworker-worker-status-stopped"
    );
    // check that Start button is available
    expect(wrapper.find(".js-start-button")).toHaveLength(1);
    // check that inspect link does not exist
    expect(wrapper.find(".js-inspect-link")).toHaveLength(0);

    expect(wrapper).toMatchSnapshot();
  });

  it("Renders the start button even if debugging workers is disabled", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Worker({
        isDebugEnabled: false,
        worker: WORKER_STOPPED,
        store,
      })
    ).dive();

    // ensure proper status
    expect(wrapper.find(".js-worker-status").text()).toBe(
      "serviceworker-worker-status-stopped"
    );
    // check that Start button is available
    expect(wrapper.find(".js-start-button")).toHaveLength(1);
  });

  it("Renders the expected snapshot for a non-active worker", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Worker({
        isDebugEnabled: true,
        worker: WORKER_WAITING,
        store,
      })
    ).dive();

    // ensure proper status
    // NOTE: since non-active status are localized directly in the front, not
    //       in the panel, we don't expect a localization ID here
    expect(wrapper.find(".js-worker-status").text()).toBe("installed");
    // check that Start button is not available
    expect(wrapper.find(".js-start-button")).toHaveLength(0);
    // check that Debug link does not exist
    expect(wrapper.find(".js-inspect-link")).toHaveLength(0);

    expect(wrapper).toMatchSnapshot();
  });
});
