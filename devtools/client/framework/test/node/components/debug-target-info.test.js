/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Unit tests for the DebugTargetInfo component.
 */

const renderer = require("react-test-renderer");
const React = require("resource://devtools/client/shared/vendor/react.mjs");
const DebugTargetInfo = React.createFactory(
  require("resource://devtools/client/framework/components/DebugTargetInfo.js")
);
const {
  CONNECTION_TYPES,
} = require("resource://devtools/client/shared/remote-debugging/constants.js");
const DESCRIPTOR_TYPES = require("resource://devtools/client/fronts/descriptors/descriptor-types.js");

/**
 * Stub for the L10N property expected by the DebugTargetInfo component.
 */
const stubL10N = {
  getStr: id => id,
  getFormatStr: (id, ...args) => [id, ...args].join("-"),
};

const findByClassName = (testInstance, className) => {
  return testInstance.findAll(node => {
    return node.props.className && node.props.className.includes(className);
  });
};

function buildProps(base, extraDebugTargetData) {
  const props = Object.assign({}, base);
  Object.assign(props.debugTargetData, extraDebugTargetData);
  return props;
}

const TEST_TOOLBOX = {
  target: {
    name: "Test Tab Name",
    url: "http://some.target/url",
    targetForm: {
      traits: {
        navigation: true,
      },
    },
    getTrait: trait => {
      return TEST_TOOLBOX.target.targetForm.traits[trait];
    },
  },
  doc: {},
};

const TEST_TOOLBOX_NO_NAME = {
  target: {
    url: "http://some.target/without/a/name",
    targetForm: {
      traits: {
        navigation: true,
      },
    },
    getTrait: trait => {
      return TEST_TOOLBOX.target.targetForm.traits[trait];
    },
  },
  doc: {},
};

const USB_DEVICE_DESCRIPTION = {
  deviceName: "usbDeviceName",
  icon: "chrome://browser/content/logos/firefox.svg",
  name: "usbRuntimeBrandName",
  version: "1.0.0",
};

const THIS_FIREFOX_DEVICE_DESCRIPTION = {
  icon: "chrome://browser/content/logos/firefox.svg",
  version: "1.0.0",
  name: "thisFirefoxRuntimeBrandName",
};

const USB_TARGET_INFO = {
  debugTargetData: {
    connectionType: CONNECTION_TYPES.USB,
    runtimeInfo: USB_DEVICE_DESCRIPTION,
    descriptorType: DESCRIPTOR_TYPES.TAB,
    descriptorName: "Usb debugging context",
  },
  toolbox: TEST_TOOLBOX,
  L10N: stubL10N,
};

const THIS_FIREFOX_TARGET_INFO = {
  debugTargetData: {
    connectionType: CONNECTION_TYPES.THIS_FIREFOX,
    runtimeInfo: THIS_FIREFOX_DEVICE_DESCRIPTION,
    descriptorType: DESCRIPTOR_TYPES.TAB,
    descriptorName: "This firefox first tab",
  },
  toolbox: TEST_TOOLBOX,
  L10N: stubL10N,
};

const THIS_FIREFOX_NO_NAME_TARGET_INFO = {
  debugTargetData: {
    connectionType: CONNECTION_TYPES.THIS_FIREFOX,
    runtimeInfo: THIS_FIREFOX_DEVICE_DESCRIPTION,
    descriptorType: DESCRIPTOR_TYPES.TAB,
    /* Avoid passing a name via 'descriptorName' attribute */
  },
  toolbox: TEST_TOOLBOX_NO_NAME,
  L10N: stubL10N,
};

describe("DebugTargetInfo component", () => {
  describe("Connection info", () => {
    it("displays connection info for USB Release target", () => {
      const component = renderer.create(DebugTargetInfo(USB_TARGET_INFO));
      expect(
        findByClassName(component.root, "qa-connection-info").length
      ).toEqual(1);
    });

    it("renders the expected snapshot for USB Release target", () => {
      const component = renderer.create(DebugTargetInfo(USB_TARGET_INFO));
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("hides the connection info for This Firefox target", () => {
      const component = renderer.create(
        DebugTargetInfo(THIS_FIREFOX_TARGET_INFO)
      );
      expect(
        findByClassName(component.root, "qa-connection-info").length
      ).toEqual(0);
    });
  });

  describe("Target title", () => {
    it("displays the target title if the target of the Toolbox has a name", () => {
      const component = renderer.create(
        DebugTargetInfo(THIS_FIREFOX_TARGET_INFO)
      );
      expect(
        findByClassName(component.root, "qa-descriptor-title").length
      ).toEqual(1);
    });

    it("renders the expected snapshot for This Firefox target", () => {
      const component = renderer.create(
        DebugTargetInfo(THIS_FIREFOX_TARGET_INFO)
      );
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("doesn't display the target title if the target of the Toolbox has no name", () => {
      const component = renderer.create(
        DebugTargetInfo(THIS_FIREFOX_NO_NAME_TARGET_INFO)
      );
      expect(
        findByClassName(component.root, "qa-descriptor-title").length
      ).toEqual(0);
    });

    it("renders the expected snapshot for a Toolbox with an unnamed target", () => {
      const component = renderer.create(
        DebugTargetInfo(THIS_FIREFOX_NO_NAME_TARGET_INFO)
      );
      expect(component.toJSON()).toMatchSnapshot();
    });
  });

  describe("Target icon", () => {
    it("renders the expected snapshot for a tab target", () => {
      const props = buildProps(USB_TARGET_INFO, {
        descriptorType: DESCRIPTOR_TYPES.TAB,
      });
      const component = renderer.create(DebugTargetInfo(props));
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("renders the expected snapshot for a worker target", () => {
      const props = buildProps(USB_TARGET_INFO, {
        descriptorType: DESCRIPTOR_TYPES.WORKER,
      });
      const component = renderer.create(DebugTargetInfo(props));
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("renders the expected snapshot for an extension target", () => {
      const props = buildProps(USB_TARGET_INFO, {
        descriptorType: DESCRIPTOR_TYPES.EXTENSION,
      });
      const component = renderer.create(DebugTargetInfo(props));
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("renders the expected snapshot for an local extension target", () => {
      const props = buildProps(THIS_FIREFOX_TARGET_INFO, {
        descriptorType: DESCRIPTOR_TYPES.EXTENSION,
      });
      const component = renderer.create(DebugTargetInfo(props));
      expect(component.toJSON()).toMatchSnapshot();
    });

    it("renders the expected snapshot for a process target", () => {
      const props = buildProps(USB_TARGET_INFO, {
        descriptorType: DESCRIPTOR_TYPES.PROCESS,
      });
      const component = renderer.create(DebugTargetInfo(props));
      expect(component.toJSON()).toMatchSnapshot();
    });
  });

  describe("Always on top button", () => {
    it("displays always on top button for local webextension target", () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(THIS_FIREFOX_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.EXTENSION,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(1);
    });

    it(`does not display "Always on top" button for remote webextension toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(USB_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.EXTENSION,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for local tab toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(THIS_FIREFOX_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.TAB,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for remote tab toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(USB_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.TAB,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for local worker toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(THIS_FIREFOX_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.WORKER,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for remote worker toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(USB_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.WORKER,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for local process toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(THIS_FIREFOX_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.PROCESS,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });

    it(`does not display "Always on top" button for remote process toolbox`, () => {
      const component = renderer.create(
        DebugTargetInfo(
          buildProps(USB_TARGET_INFO, {
            descriptorType: DESCRIPTOR_TYPES.PROCESS,
          })
        )
      );
      expect(
        findByClassName(component.root, "toolbox-always-on-top").length
      ).toEqual(0);
    });
  });
});
