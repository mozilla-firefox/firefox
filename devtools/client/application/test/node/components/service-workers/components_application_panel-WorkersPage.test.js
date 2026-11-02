/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Import libs
const { shallow } = require("enzyme");
const { createFactory } = require("react");

// Import fixtures
const {
  EMPTY_WORKER_LIST,
  SINGLE_WORKER_DEFAULT_DOMAIN_LIST,
  SINGLE_WORKER_DIFFERENT_DOMAIN_LIST,
  MULTIPLE_WORKER_LIST,
  MULTIPLE_WORKER_MIXED_DOMAINS_LIST,
} = require("resource://devtools/client/application/test/node/fixtures/data/constants.js");

// Import setupStore with imported & combined reducers
const {
  setupStore,
} = require("resource://devtools/client/application/test/node/helpers.js");

// Import component
const WorkersPage = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/WorkersPage.js")
);

/**
 * Test for App.js component
 */
describe("WorkersPage", () => {
  const baseState = {
    workers: { list: [], canDebugWorkers: true },
    page: { domain: "example.com" },
  };

  function buildStoreWithWorkers(workerList) {
    const workers = { list: workerList, canDebugWorkers: true };
    const state = Object.assign({}, baseState, { workers });
    return setupStore(state);
  }

  it("renders an empty list if there are no workers", () => {
    const store = buildStoreWithWorkers(EMPTY_WORKER_LIST);
    const wrapper = shallow(WorkersPage({ store })).dive();

    expect(wrapper).toMatchSnapshot();
  });

  it("it renders a list with a single element if there's just 1 worker", () => {
    const store = buildStoreWithWorkers(SINGLE_WORKER_DEFAULT_DOMAIN_LIST);
    const wrapper = shallow(WorkersPage({ store })).dive();

    expect(wrapper).toMatchSnapshot();
  });

  it("renders a list with multiple elements when there are multiple workers", () => {
    const store = buildStoreWithWorkers(MULTIPLE_WORKER_LIST);
    const wrapper = shallow(WorkersPage({ store })).dive();

    expect(wrapper).toMatchSnapshot();
  });

  it("filters out workers from diferent domains", () => {
    const store = buildStoreWithWorkers(MULTIPLE_WORKER_MIXED_DOMAINS_LIST);
    const wrapper = shallow(WorkersPage({ store })).dive();

    expect(wrapper).toMatchSnapshot();
  });

  it(
    "filters out workers from different domains and renders an empty list when " +
      "there is none left",
    () => {
      const store = buildStoreWithWorkers(SINGLE_WORKER_DIFFERENT_DOMAIN_LIST);
      const wrapper = shallow(WorkersPage({ store })).dive();

      expect(wrapper).toMatchSnapshot();
    }
  );
});
