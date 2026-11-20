/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render } from "@testing-library/react";
import { DSLinkMenu } from "content-src/components/DiscoveryStreamComponents/DSLinkMenu/DSLinkMenu";
import { PanelListItems } from "content-src/components/LinkMenu/PanelListItems";
import { WrapWithProvider } from "test/jest/test-utils";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import React from "react";

// Enzyme read the options off PanelListItems via wrapper.find().prop("options").
// Mocking it gives the same reach: getLinkMenuOptions maps each option key to one
// object, and some options are gated at render time, so the DOM item count is not
// a substitute for the array itself.
jest.mock("content-src/components/LinkMenu/PanelListItems", () => ({
  PanelListItems: jest.fn(() => null),
}));

// Bug 2051742 moved this menu to a persistent panel-list paired to a moz-button
// via menuId, so the options are read off PanelListItems rather than LinkMenu.
// getLinkMenuOptions maps each option key to one option object, so the length of
// PanelListItems' options reflects the selected option keys.
function renderMenu(props, state = INITIAL_STATE) {
  PanelListItems.mockClear();
  return render(
    <WrapWithProvider state={state}>
      <DSLinkMenu {...props} />
    </WrapWithProvider>
  );
}

describe("<DSLinkMenu>", () => {
  describe("DS link menu actions", () => {
    it("should parse args for fluent correctly", () => {
      const title = '"fluent"';
      const { container } = renderMenu({ title });
      const button = container.querySelector(
        "moz-button[data-l10n-id='newtab-menu-content-tooltip']"
      );
      expect(button).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ title })
      );
    });
  });

  describe("DS context menu options", () => {
    const ValidDSLinkMenuProps = { card_type: "organic" };

    const optionCount = () =>
      PanelListItems.mock.calls.at(-1)[0].options.length;

    it("should render a moz-button trigger paired to a panel-list", () => {
      const { container } = renderMenu(ValidDSLinkMenuProps);
      const button = container.querySelector("moz-button");
      const panelList = container.querySelector("panel-list");
      expect(button).toBeInTheDocument();
      expect(panelList).toBeInTheDocument();
      // moz-button opens the panel-list via the shared menuId.
      expect(button.getAttribute("menuId")).toBe(panelList.getAttribute("id"));
    });

    it("should render the built menu options via PanelListItems", () => {
      renderMenu(ValidDSLinkMenuProps);
      expect(PanelListItems).toHaveBeenCalled();
      expect(Array.isArray(PanelListItems.mock.calls.at(-1)[0].options)).toBe(
        true
      );
    });

    it("should pass the card's dwell label to Open in a New Window", () => {
      renderMenu({ ...ValidDSLinkMenuProps, dwellLabel: "story_sponsored" });
      const openInNewWindow = PanelListItems.mock.calls
        .at(-1)[0]
        .options.find(o => o.id === "newtab-menu-open-new-window");
      expect(openInNewWindow.action.data).toHaveProperty(
        "dwell_label",
        "story_sponsored"
      );
    });

    it("should build the correct menu options for recommended stories", () => {
      // CheckBookmark, Separator, OpenInNewWindow, OpenInPrivateWindow,
      // Separator, BlockUrl
      renderMenu(ValidDSLinkMenuProps);
      expect(optionCount()).toBe(6);
    });

    it("should add ReportContent when a section is defined", () => {
      renderMenu({
        ...ValidDSLinkMenuProps,
        section: "abc",
      });
      expect(optionCount()).toBe(7);
    });

    it("should build the correct menu options for SPOCs", () => {
      // BlockUrl, ManageSponsoredContent, OurSponsorsAndYourPrivacy
      renderMenu({
        ...ValidDSLinkMenuProps,
        card_type: "spoc",
      });
      expect(optionCount()).toBe(3);
    });

    const stateWithReporting = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "discoverystream.reportAds.enabled": true,
        },
      },
    };
    const reportAdProps = {
      ...ValidDSLinkMenuProps,
      card_type: "spoc",
      shim: { report: {} },
    };

    it("should add ReportAd for SPOCs when ad reporting is enabled", () => {
      renderMenu(reportAdProps, stateWithReporting);
      expect(optionCount()).toBe(4);
    });

    it("should mark ReportAd as opening a dialog", () => {
      renderMenu(reportAdProps, stateWithReporting);
      const reportAd = PanelListItems.mock.calls
        .at(-1)[0]
        .options.find(o => o.id === "newtab-menu-report-this-ad");
      expect(reportAd).toHaveProperty("ariaHasPopup", "dialog");
    });
  });

  it("should render the context menu position container", () => {
    const { container } = renderMenu({ index: 0 });
    expect(
      container.querySelector(".context-menu-position-container")
    ).toBeInTheDocument();
  });
});
