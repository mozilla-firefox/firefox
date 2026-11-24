/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Import libs
const { shallow } = require("enzyme");
const { createFactory } = require("react");

// Import constants
const {
  SINGLE_WORKER_DEFAULT_DOMAIN_LIST,
  MULTIPLE_WORKER_LIST,
} = require("resource://devtools/client/application/test/node/fixtures/data/constants.js");

const RegistrationList = createFactory(
  require("resource://devtools/client/application/src/components/service-workers/RegistrationList.js")
);

/**
 * Test for RegistrationList.js component
 */
describe("RegistrationList", () => {
  it("renders the expected snapshot for a list with a single registration", () => {
    const wrapper = shallow(
      RegistrationList({
        registrations: SINGLE_WORKER_DEFAULT_DOMAIN_LIST,
        canDebugWorkers: true,
      })
    );
    expect(wrapper).toMatchSnapshot();
  });

  it("renders the expected snapshot for a multiple registration list", () => {
    const wrapper = shallow(
      RegistrationList({
        registrations: MULTIPLE_WORKER_LIST,
        canDebugWorkers: true,
      })
    );
    expect(wrapper).toMatchSnapshot();
  });
});
