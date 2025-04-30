import { shallow } from "enzyme";
import { AdBannerContextMenu } from "content-src/components/DiscoveryStreamComponents/AdBannerContextMenu/AdBannerContextMenu";
import { LinkMenu } from "content-src/components/LinkMenu/LinkMenu";
import React from "react";

describe("<AdBannerContextMenu>", () => {
  let wrapper;

  describe("Ad banner context menu options", () => {
    const props = {
      spoc: { url: "https://www.test.com/", shim: "aaabbbcccddd" },
      position: 1,
      type: "billboard",
      prefs: {},
    };

    beforeEach(() => {
      wrapper = shallow(<AdBannerContextMenu {...props} />);
    });

    it("should render a context menu button", () => {
      assert.ok(wrapper.exists());
      assert.ok(
        wrapper.find("moz-button").exists(),
        "context menu button exists"
      );

      // Make sure the menu wrapper has the correct default styles
      assert.isFalse(
        wrapper.find("div.ads-context-menu").hasClass("context-menu-open")
      );
    });

    it("should render a context menu button with hover styles when context menu is open", () => {
      let button = wrapper.find("moz-button");
      button.simulate("click", {
        preventDefault: () => {},
      });

      // Make sure the menu wrapper adds an extra classname when the menu is open
      assert.isTrue(
        wrapper.find("div.ads-context-menu").hasClass("context-menu-open")
      );
    });

    it("should render LinkMenu when context menu button is clicked", () => {
      let button = wrapper.find("moz-button");
      button.simulate("click", {
        preventDefault: () => {},
      });
      assert.equal(wrapper.find(LinkMenu).length, 1);
    });

    it("should render LinkMenu when context menu is accessed with the 'Enter' key", () => {
      let button = wrapper.find("moz-button");

      button.simulate("keydown", { key: "Enter", preventDefault: () => {} });

      assert.equal(wrapper.find(LinkMenu).length, 1);
    });

    it("should render LinkMenu when context menu is accessed with the 'Space' key", () => {
      let button = wrapper.find("moz-button");

      button.simulate("keydown", { key: " ", preventDefault: () => {} });

      assert.equal(wrapper.find(LinkMenu).length, 1);
    });

    it("should pass props to LinkMenu", () => {
      wrapper.find("moz-button").simulate("click", {
        preventDefault: () => {},
      });
      const linkMenuProps = wrapper.find(LinkMenu).props();
      [
        "onUpdate",
        "dispatch",
        "keyboardAccess",
        "options",
        "shouldSendImpressionStats",
        "userEvent",
        "site",
        "index",
        "source",
      ].forEach(prop => assert.property(linkMenuProps, prop));
    });

    it("should pass through the correct menu options to LinkMenu for ad banners with reporting INCLUDED", () => {
      const propsWithReporting = {
        ...props,
        showAdReporting: true,
      };
      wrapper = shallow(<AdBannerContextMenu {...propsWithReporting} />);
      wrapper.find("moz-button").simulate("click", {
        preventDefault: () => {},
      });
      const linkMenuProps = wrapper.find(LinkMenu).props();

      const linkMenuOptions = [
        "BlockAdUrl",
        "ReportAd",
        "ManageSponsoredContent",
        "OurSponsorsAndYourPrivacy",
      ];

      assert.deepEqual(linkMenuProps.options, linkMenuOptions);
    });

    it("should pass through correct menu options to LinkMenu for ad banner with reporting EXCLUDED", () => {
      const propsWithoutReporting = {
        ...props,
        showAdReporting: false,
      };

      wrapper = shallow(<AdBannerContextMenu {...propsWithoutReporting} />);
      wrapper.find("moz-button").simulate("click", {
        preventDefault: () => {},
      });
      const linkMenuProps = wrapper.find(LinkMenu).props();

      const linkMenuOptions = [
        "BlockAdUrl",
        "ManageSponsoredContent",
        "OurSponsorsAndYourPrivacy",
      ];

      assert.deepEqual(linkMenuProps.options, linkMenuOptions);
    });
  });
});
