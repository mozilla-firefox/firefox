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
  REGISTRATION_SINGLE_WORKER,
  REGISTRATION_MULTIPLE_WORKERS,
} = require("resource://devtools/client/application/test/node/fixtures/data/constants.js");

const Registration = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/Registration.js")
);

describe("Registration", () => {
  it("Renders the expected snapshot for a registration with a worker", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Registration({
        isDebugEnabled: true,
        registration: REGISTRATION_SINGLE_WORKER,
        store,
      })
    ).dive();

    expect(wrapper).toMatchSnapshot();
    // ensure that we do have the proper amount of workers
    expect(wrapper.find("Connect(Worker)")).toHaveLength(1);
  });

  it("Renders the expected snapshot for a registration with multiple workers", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Registration({
        isDebugEnabled: true,
        registration: REGISTRATION_MULTIPLE_WORKERS,
        store,
      })
    ).dive();

    expect(wrapper).toMatchSnapshot();
    // ensure that we do have the proper amount of workers
    expect(wrapper.find("Connect(Worker)")).toHaveLength(2);
  });

  it("Renders the expected snapshot when sw debugging is disabled", () => {
    const store = setupStore({});

    const wrapper = shallow(
      Registration({
        isDebugEnabled: false,
        registration: REGISTRATION_SINGLE_WORKER,
        store,
      })
    ).dive();

    expect(wrapper).toMatchSnapshot();
  });

  it("Removes the ending forward slash from the scope, when present", () => {
    const store = setupStore({});

    const registration = Object.assign({}, REGISTRATION_SINGLE_WORKER, {
      scope: "https://example.com/something/",
    });

    const wrapper = shallow(
      Registration({
        isDebugEnabled: false,
        registration,
        store,
      })
    ).dive();

    const scopeEl = wrapper.find(".js-sw-scope");
    expect(scopeEl.text()).toBe("example.com/something");
  });
});
