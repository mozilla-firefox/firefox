/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Test utils.
const expect = require("expect");
const { render } = require("enzyme");

// Components under test.
const WarningGroup = require("resource://devtools/client/webconsole/components/Output/message-types/WarningGroup.js");
const {
  MESSAGE_SOURCE,
  MESSAGE_TYPE,
} = require("resource://devtools/client/webconsole/constants.js");
const {
  ConsoleMessage,
} = require("resource://devtools/client/webconsole/types.js");
const {
  createWarningGroupMessage,
} = require("resource://devtools/client/webconsole/utils/messages.js");

// Test fakes.
const {
  stubPreparedMessages,
} = require("resource://devtools/client/webconsole/test/node/fixtures/stubs/index.js");
const serviceContainer = require("resource://devtools/client/webconsole/test/node/fixtures/serviceContainer.js");
const mockMessage = ConsoleMessage({
  messageText: "this is a warning group",
  source: MESSAGE_SOURCE.CONSOLE_FRONTEND,
  timeStamp: Date.now(),
});

describe("WarningGroup component:", () => {
  it("renders", () => {
    const wrapper = render(
      WarningGroup({
        message: mockMessage,
        serviceContainer,
        timestampsVisible: true,
        badge: 42,
      })
    );

    const {
      timestampString,
    } = require("resource://devtools/shared/webconsole/l10n.js");
    expect(wrapper.find(".timestamp").text()).toBe(
      timestampString(mockMessage.timeStamp)
    );
    expect(wrapper.find(".message-body").text()).toBe(
      "this is a warning group 42"
    );
    expect(wrapper.find(".arrow[aria-expanded=false]")).toExist();
  });

  it("does have an expanded arrow when `open` prop is true", () => {
    const wrapper = render(
      WarningGroup({
        message: mockMessage,
        serviceContainer,
        open: true,
        badge: 42,
      })
    );

    expect(wrapper.find(".arrow[aria-expanded=true]")).toExist();
  });

  it("does not have a timestamp when timestampsVisible prop is falsy", () => {
    const wrapper = render(
      WarningGroup({
        message: mockMessage,
        serviceContainer,
        timestampsVisible: false,
        badge: 42,
      })
    );

    expect(wrapper.find(".timestamp").length).toBe(0);
  });

  it("renders Content Blocking Group message", () => {
    const firstMessage = stubPreparedMessages.get(
      "ReferenceError: asdf is not defined"
    );
    firstMessage.messageText =
      "The resource at “https://evil.com” was blocked.";
    firstMessage.category = "cookieBlockedPermission";
    const type = MESSAGE_TYPE.CONTENT_BLOCKING_GROUP;
    const message = createWarningGroupMessage(
      `${firstMessage.type}-${firstMessage.innerWindowID}`,
      type,
      firstMessage
    );

    const wrapper = render(
      WarningGroup({
        message,
        serviceContainer,
        badge: 24,
      })
    );

    expect(wrapper.find(".message-body").text()).toBe(
      "The resource at “<URL>” was blocked. 24"
    );
    expect(wrapper.find(".arrow[aria-expanded=false]")).toExist();
  });
});
