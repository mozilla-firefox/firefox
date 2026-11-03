import React from "react";
import { shallow } from "enzyme";
import { GlobalOverrider } from "tests/unit/utils";
import { MobileDownloads } from "content-src/components/MobileDownloads";

describe("Multistage AboutWelcome MobileDownloads module", () => {
  let globals;
  let sandbox;

  beforeEach(async () => {
    globals = new GlobalOverrider();
    globals.set({
      AWFinish: () => Promise.resolve(),
      AWSendToDeviceEmailsSupported: () => Promise.resolve(),
    });
    sandbox = sinon.createSandbox();
  });

  afterEach(() => {
    sandbox.restore();
    globals.restore();
  });

  describe("Mobile Downloads component", () => {
    const MOBILE_DOWNLOADS_PROPS = {
      data: {
        QR_code: {
          image_url:
            "chrome://browser/content/asrouter/assets/desktop-to-mobile-non-eu-QR.svg",
          alt_text: "Scan the QR code",
        },
        email: {
          link_text: "Email yourself a link",
        },
      },
      handleAction: () => {
        window.AWFinish();
      },
    };

    it("should render MobileDownloads", () => {
      const wrapper = shallow(<MobileDownloads {...MOBILE_DOWNLOADS_PROPS} />);

      assert.ok(wrapper.exists());
    });

    it("should handle action on email button click", () => {
      const wrapper = shallow(<MobileDownloads {...MOBILE_DOWNLOADS_PROPS} />);

      const stub = sandbox.stub(global, "AWFinish");
      wrapper.find("button.email-link").simulate("click");

      assert.calledOnce(stub);
    });
  });
});
