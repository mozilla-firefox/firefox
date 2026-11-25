/* eslint-disable no-shadow */
import { actionTypes as at } from "common/Actions.mjs";
import { ContextMenu } from "content-src/components/ContextMenu/ContextMenu";
import { LinkMenuOptions } from "content-src/lib/link-menu-options";
import { _LinkMenu as LinkMenu } from "content-src/components/LinkMenu/LinkMenu";
import { WrapWithProvider } from "test/jest/test-utils";
import { render } from "@testing-library/react";
import React from "react";

// The legacy Enzyme test used shallow() and inspected the props that _LinkMenu
// passes to its ContextMenu child (the `options` array -- with each option's
// onClick/action/impression/userEvent/first/last fields -- and the `onUpdate`
// callback). Those values live only in React props, so there is no DOM/RTL
// equivalent to read them. We render the unconnected _LinkMenu inside a redux
// Provider (its ContextMenuItem descendants are connected) and reach the
// instance via a ref (white-box). `instance.render().props` then returns exactly
// what the legacy `wrapper.find(ContextMenu).props()` returned.
function mountLinkMenu(props) {
  const linkMenuRef = React.createRef();
  render(
    <WrapWithProvider>
      <LinkMenu ref={linkMenuRef} {...props} />
    </WrapWithProvider>
  );
  return linkMenuRef.current;
}

describe("<LinkMenu>", () => {
  let wrapper;
  beforeEach(() => {
    wrapper = mountLinkMenu({
      site: { url: "" },
      options: ["CheckPinTopSite", "CheckBookmark", "OpenInNewWindow"],
      dispatch: () => {},
    });
  });
  it("should render a ContextMenu element", () => {
    expect(wrapper.render().type).toBe(ContextMenu);
  });
  it("should pass onUpdate, and options to ContextMenu", () => {
    expect(wrapper.render().type).toBe(ContextMenu);
    const contextMenuProps = wrapper.render().props;
    ["onUpdate", "options"].forEach(prop =>
      expect(contextMenuProps).toHaveProperty(prop)
    );
  });
  it("should give ContextMenu the correct tabbable options length for a11y", () => {
    const { options } = wrapper.render().props;
    const [firstItem] = options;
    const lastItem = options[options.length - 1];

    // first item should have {first: true}
    expect(firstItem.first).toBe(true);
    expect(!firstItem.last).toBeTruthy();

    // last item should have {last: true}
    expect(lastItem.last).toBe(true);
    expect(!lastItem.first).toBeTruthy();

    // middle items should have neither
    for (let i = 1; i < options.length - 1; i++) {
      expect(!options[i].first && !options[i].last).toBeTruthy();
    }
  });
  it("should show the correct options for default sites", () => {
    wrapper = mountLinkMenu({
      site: { url: "", isDefault: true },
      options: ["CheckBookmark"],
      source: "TOP_SITES",
      isPrivateBrowsingEnabled: true,
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    let i = 0;
    expect(options[i++].id).toBe("newtab-menu-pin");
    expect(options[i++].id).toBe("newtab-menu-edit-topsites");
    expect(options[i++].id).toBe("newtab-menu-add-topsite");
    expect(options[i++].type).toBe("separator");
    expect(options[i++].id).toBe("newtab-menu-open-new-window");
    expect(options[i++].id).toBe("newtab-menu-open-new-private-window");
    expect(options[i++].type).toBe("separator");
    expect(options[i++].id).toBe("newtab-menu-dismiss");
    expect(options.length).toBe(i);
    // Double check that delete options are not included for default top sites
    options
      .filter(o => o.type !== "separator")
      .forEach(o => {
        expect(["newtab-menu-delete-history"]).not.toContain(o.id);
      });
  });
  it("AddTopSite option opens the New Shortcut form", () => {
    const { action } = LinkMenuOptions.AddTopSite();
    expect(action.type).toBe(at.TOP_SITES_EDIT);
    expect(action.data.index).toBe(-1);
  });
  it("should show Unpin option for a pinned site if CheckPinTopSite in options list", () => {
    wrapper = mountLinkMenu({
      site: { url: "", isPinned: true },
      source: "TOP_SITES",
      options: ["CheckPinTopSite"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-unpin")
    ).toBeDefined();
  });
  it("should show Pin option for an unpinned site if CheckPinTopSite in options list", () => {
    wrapper = mountLinkMenu({
      site: { url: "", isPinned: false },
      source: "TOP_SITES",
      options: ["CheckPinTopSite"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(options.find(o => o.id && o.id === "newtab-menu-pin")).toBeDefined();
  });
  it("should show Unbookmark option for a bookmarked site if CheckBookmark in options list", () => {
    wrapper = mountLinkMenu({
      site: { url: "", bookmarkGuid: 1234 },
      source: "TOP_SITES",
      options: ["CheckBookmark"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-remove-bookmark")
    ).toBeDefined();
  });
  it("should show Bookmark option for an unbookmarked site if CheckBookmark in options list", () => {
    wrapper = mountLinkMenu({
      site: { url: "", bookmarkGuid: 0 },
      source: "TOP_SITES",
      options: ["CheckBookmark"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-bookmark")
    ).toBeDefined();
  });
  it("should show Open File option for a downloaded item", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download", path: "foo" },
      source: "HIGHLIGHTS",
      options: ["OpenFile"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-open-file")
    ).toBeDefined();
  });
  it("should show Show File option for a downloaded item on a default platform", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download", path: "foo" },
      source: "HIGHLIGHTS",
      options: ["ShowFile"],
      platform: "default",
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-show-file")
    ).toBeDefined();
  });
  it("should show Copy Downlad Link option for a downloaded item when CopyDownloadLink", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download" },
      source: "HIGHLIGHTS",
      options: ["CopyDownloadLink"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-copy-download-link")
    ).toBeDefined();
  });
  it("should show Go To Download Page option for a downloaded item when GoToDownloadPage", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download", referrer: "foo" },
      source: "HIGHLIGHTS",
      options: ["GoToDownloadPage"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-go-to-download-page")
    ).toBeDefined();
    expect(options[0].disabled).toBe(false);
  });
  it("should show Go To Download Page option as disabled for a downloaded item when GoToDownloadPage if no referrer exists", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download", referrer: null },
      source: "HIGHLIGHTS",
      options: ["GoToDownloadPage"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-go-to-download-page")
    ).toBeDefined();
    expect(options[0].disabled).toBe(true);
  });
  it("should show Remove Download Link option for a downloaded item when RemoveDownload", () => {
    wrapper = mountLinkMenu({
      site: { url: "", type: "download" },
      source: "HIGHLIGHTS",
      options: ["RemoveDownload"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    expect(
      options.find(o => o.id && o.id === "newtab-menu-remove-download")
    ).toBeDefined();
  });
  it("should show Edit option", () => {
    const props = { url: "foo", label: "label" };
    const index = 5;
    wrapper = mountLinkMenu({
      site: props,
      index: 5,
      source: "TOP_SITES",
      options: ["EditTopSite"],
      dispatch: () => {},
    });
    const { options } = wrapper.render().props;
    const option = options.find(
      o => o.id && o.id === "newtab-menu-edit-topsites"
    );
    expect(option).toBeDefined();
    expect(option.action.data.index).toBe(index);
  });
  describe(".onClick", () => {
    const FAKE_EVENT = {};
    const FAKE_INDEX = 3;
    const FAKE_SOURCE = "TOP_SITES";
    const FAKE_SITE = {
      bookmarkGuid: 1234,
      hostname: "foo",
      path: "foo",
      pocket_id: "1234",
      referrer: "https://foo.com/ref",
      title: "bar",
      type: "bookmark",
      typedBonus: true,
      url: "https://foo.com",
      sponsored_tile_id: 12345,
      card_type: "organic",
    };
    const dispatch = jest.fn();
    const propOptions = [
      "ShowFile",
      "CopyDownloadLink",
      "GoToDownloadPage",
      "RemoveDownload",
      "Separator",
      "ShowPrivacyInfo",
      "RemoveBookmark",
      "AddBookmark",
      "OpenInNewWindow",
      "OpenInPrivateWindow",
      "BlockUrl",
      "DeleteUrl",
      "PinTopSite",
      "UnpinTopSite",
      "WebExtDismiss",
    ];
    const expectedActionData = {
      "newtab-menu-remove-bookmark": FAKE_SITE.bookmarkGuid,
      "newtab-menu-bookmark": {
        url: FAKE_SITE.url,
        title: FAKE_SITE.title,
        type: FAKE_SITE.type,
      },
      "newtab-menu-open-new-window": {
        card_type: FAKE_SITE.card_type,
        referrer: FAKE_SITE.referrer,
        typedBonus: FAKE_SITE.typedBonus,
        url: FAKE_SITE.url,
        event_source: "CONTEXT_MENU",
        topic: undefined,
        tile_id: undefined,
        scheduled_corpus_item_id: undefined,
        corpus_item_id: undefined,
        received_rank: undefined,
        recommended_at: undefined,
        format: undefined,
        is_pocket_card: false,
        is_sponsored: true,
      },
      "newtab-menu-open-new-private-window": {
        card_type: FAKE_SITE.card_type,
        url: FAKE_SITE.url,
        referrer: FAKE_SITE.referrer,
        event_source: "CONTEXT_MENU",
        topic: undefined,
        tile_id: undefined,
        scheduled_corpus_item_id: undefined,
        corpus_item_id: undefined,
        received_rank: undefined,
        recommended_at: undefined,
        format: undefined,
        is_pocket_card: false,
      },
      "newtab-menu-dismiss": [
        {
          url: FAKE_SITE.url,
          pocket_id: FAKE_SITE.pocket_id,
          tile_id: 12345,
          scheduled_corpus_item_id: undefined,
          corpus_item_id: undefined,
          recommended_at: undefined,
          received_rank: undefined,
          isSponsoredTopSite: undefined,
          type: "bookmark",
          card_type: FAKE_SITE.card_type,
          position: 3,
          is_pocket_card: false,
        },
      ],
      menu_action_webext_dismiss: {
        source: "TOP_SITES",
        url: FAKE_SITE.url,
        action_position: 3,
      },
      "newtab-menu-delete-history": {
        url: FAKE_SITE.url,
        pocket_id: FAKE_SITE.pocket_id,
        forceBlock: FAKE_SITE.bookmarkGuid,
        original_url: FAKE_SITE.original_url,
      },
      "newtab-menu-pin": { site: FAKE_SITE, index: FAKE_INDEX },
      "newtab-menu-unpin": { site: { url: FAKE_SITE.url } },
      "newtab-menu-show-file": { url: FAKE_SITE.url },
      "newtab-menu-copy-download-link": { url: FAKE_SITE.url },
      "newtab-menu-go-to-download-page": { url: FAKE_SITE.referrer },
      "newtab-menu-remove-download": { url: FAKE_SITE.url },
    };
    const { options } = mountLinkMenu({
      site: FAKE_SITE,
      siteInfo: { value: { card_type: FAKE_SITE.type } },
      dispatch,
      index: FAKE_INDEX,
      isPrivateBrowsingEnabled: true,
      platform: "default",
      options: propOptions,
      source: FAKE_SOURCE,
      shouldSendImpressionStats: true,
    }).render().props;
    afterEach(() => dispatch.mockClear());
    options
      .filter(o => o.type !== "separator")
      .forEach(option => {
        it(`should fire a ${option.action.type} action for ${option.id} with the expected data`, () => {
          option.onClick(FAKE_EVENT);

          if (option.impression && option.userEvent) {
            expect(dispatch).toHaveBeenCalledTimes(3);
          } else if (option.impression || option.userEvent) {
            expect(dispatch).toHaveBeenCalledTimes(2);
          } else {
            expect(dispatch).toHaveBeenCalledTimes(1);
          }

          // option.action is dispatched
          expect(dispatch).toHaveBeenNthCalledWith(1, option.action);

          // option.action has correct data
          // (delete is a special case as it dispatches a nested DIALOG_OPEN-type action)
          // in the case of this FAKE_SITE, we send a bookmarkGuid therefore we also want
          // to block this if we delete it
          if (option.id === "newtab-menu-delete-history") {
            expect(option.action.data.onConfirm[0].data).toEqual(
              expectedActionData[option.id]
            );
            // Test UserEvent send correct meta about item deleted
            expect(option.action.data.onConfirm[1].data.action_position).toBe(
              FAKE_INDEX
            );
            expect(option.action.data.onConfirm[1].data.source).toBe(
              FAKE_SOURCE
            );
          } else {
            expect(option.action.data).toEqual(expectedActionData[option.id]);
          }
        });
        it(`should fire a UserEvent action for ${option.id} if configured`, () => {
          if (option.userEvent) {
            option.onClick(FAKE_EVENT);
            const [, [action]] = dispatch.mock.calls;
            // isUserEventAction: no Joi/chai schema plugin under jest, so we
            // assert the action is a UserEvent telemetry ping by its type.
            expect(action.type).toBe(at.TELEMETRY_USER_EVENT);
            expect(action.data.source).toBe(FAKE_SOURCE);
            expect(action.data.action_position).toBe(FAKE_INDEX);
            expect(action.data.value.card_type).toBe(FAKE_SITE.type);
          }
        });
        it(`should send impression stats for ${option.id}`, () => {
          if (option.impression) {
            option.onClick(FAKE_EVENT);
            const [, , [action]] = dispatch.mock.calls;
            expect(action).toEqual(option.impression);
          }
        });
      });
    it(`should not send impression stats if not configured`, () => {
      const { options: fakeOptions } = mountLinkMenu({
        site: FAKE_SITE,
        dispatch,
        index: FAKE_INDEX,
        options: propOptions,
        source: FAKE_SOURCE,
        shouldSendImpressionStats: false,
      }).render().props;

      fakeOptions
        .filter(o => o.type !== "separator")
        .forEach(option => {
          if (option.impression) {
            option.onClick(FAKE_EVENT);
            expect(dispatch).toHaveBeenCalledTimes(2);
            expect(dispatch.mock.calls[0][0]).not.toBe(option.impression);
            expect(dispatch.mock.calls[1][0]).not.toBe(option.impression);
            dispatch.mockClear();
          }
        });
    });
    it(`should pin a SPOC with all of the site details sent`, () => {
      const pinSpocTopSite = "PinTopSite";
      const { options: spocOptions } = mountLinkMenu({
        site: FAKE_SITE,
        siteInfo: { value: { card_type: FAKE_SITE.type } },
        dispatch,
        index: FAKE_INDEX,
        isPrivateBrowsingEnabled: true,
        platform: "default",
        options: [pinSpocTopSite],
        source: FAKE_SOURCE,
        shouldSendImpressionStats: true,
      }).render().props;

      const [pinSpocOption] = spocOptions;
      pinSpocOption.onClick(FAKE_EVENT);

      if (pinSpocOption.impression && pinSpocOption.userEvent) {
        expect(dispatch).toHaveBeenCalledTimes(3);
      } else if (pinSpocOption.impression || pinSpocOption.userEvent) {
        expect(dispatch).toHaveBeenCalledTimes(2);
      } else {
        expect(dispatch).toHaveBeenCalledTimes(1);
      }

      // option.action is dispatched
      expect(dispatch).toHaveBeenNthCalledWith(1, pinSpocOption.action);

      expect(pinSpocOption.action.data).toEqual({
        site: FAKE_SITE,
        index: FAKE_INDEX,
      });
    });
    it(`should create a proper BLOCK_URL action for a sponsored tile`, () => {
      const site = {
        hostname: "foo",
        path: "foo",
        referrer: "https://foo.com/ref",
        title: "bar",
        type: "bookmark",
        typedBonus: true,
        url: "https://foo.com",
        sponsored_position: 1,
      };
      const { options: blockOptions } = mountLinkMenu({
        site,
        siteInfo: { value: { card_type: site.type } },
        dispatch,
        index: FAKE_INDEX,
        isPrivateBrowsingEnabled: true,
        platform: "default",
        options: ["BlockUrl"],
        source: FAKE_SOURCE,
        shouldSendImpressionStats: true,
      }).render().props;
      const [blockUrlOption] = blockOptions;

      blockUrlOption.onClick(FAKE_EVENT);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch).toHaveBeenNthCalledWith(1, blockUrlOption.action);
      const expected = {
        url: site.url,
        pocket_id: undefined,
        tile_id: undefined,
        scheduled_corpus_item_id: undefined,
        corpus_item_id: undefined,
        recommended_at: undefined,
        received_rank: undefined,
        advertiser_name: site.hostname,
        isSponsoredTopSite: 1,
        type: "bookmark",
        card_type: undefined,
        position: 3,
        is_pocket_card: false,
      };
      expect(blockUrlOption.action.data[0]).toEqual(expected);
    });
    it(`should create a proper BLOCK_URL action for a pocket item`, () => {
      const site = {
        hostname: "foo",
        path: "foo",
        referrer: "https://foo.com/ref",
        title: "bar",
        type: "CardGrid",
        typedBonus: true,
        url: "https://foo.com",
      };
      const { options: blockOptions } = mountLinkMenu({
        site,
        siteInfo: { value: { card_type: site.type } },
        dispatch,
        index: FAKE_INDEX,
        isPrivateBrowsingEnabled: true,
        platform: "default",
        options: ["BlockUrl"],
        source: FAKE_SOURCE,
        shouldSendImpressionStats: true,
      }).render().props;
      const [blockUrlOption] = blockOptions;

      blockUrlOption.onClick(FAKE_EVENT);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch).toHaveBeenNthCalledWith(1, blockUrlOption.action);
      const expected = {
        url: site.url,
        pocket_id: undefined,
        tile_id: undefined,
        scheduled_corpus_item_id: undefined,
        corpus_item_id: undefined,
        recommended_at: undefined,
        received_rank: undefined,
        isSponsoredTopSite: undefined,
        type: "CardGrid",
        card_type: undefined,
        position: 3,
        is_pocket_card: true,
      };
      expect(blockUrlOption.action.data[0]).toEqual(expected);
    });
  });

  describe("OpenInNewWindow dwell time", () => {
    function openInNewWindow(site) {
      const dispatch = jest.fn();
      const { options } = mountLinkMenu({
        site,
        dispatch,
        index: 3,
        options: ["OpenInNewWindow"],
        source: "CARDGRID",
      }).render().props;
      options[0].onClick({});
      const [[action], [userEvent]] = dispatch.mock.calls;
      return { action, userEvent };
    }

    it("should give a story's window and its user event the same dwell_id", () => {
      const site = { url: "https://example.com", dwell_label: "story_organic" };
      const first = openInNewWindow(site);
      const second = openInNewWindow(site);

      expect(first.action.data.dwell_label).toBe("story_organic");
      expect(first.action.data.dwell_id).toBeDefined();
      expect(first.userEvent.data.value.dwell_id).toBe(
        first.action.data.dwell_id
      );
      expect(second.action.data.dwell_id).not.toBe(first.action.data.dwell_id);
    });

    it("should label a top site's window without a dwell_id", () => {
      const { action, userEvent } = openInNewWindow({
        url: "https://example.com",
        dwell_label: "topsite_organic",
      });

      expect(action.data.dwell_label).toBe("topsite_organic");
      expect(action.data).not.toHaveProperty("dwell_id");
      expect(userEvent.data.value).not.toHaveProperty("dwell_id");
    });

    it("should leave an unlabelled window out of dwell time", () => {
      const { action, userEvent } = openInNewWindow({
        url: "https://example.com",
      });

      expect(action.data).not.toHaveProperty("dwell_label");
      expect(action.data).not.toHaveProperty("dwell_id");
      expect(userEvent.data.value).not.toHaveProperty("dwell_id");
    });
  });

  describe("ReportTileAd", () => {
    function clickReportTileAd(site) {
      const dispatch = jest.fn();
      const { options } = mountLinkMenu({
        site,
        dispatch,
        index: 2,
        options: ["ReportTileAd"],
        source: "TOP_SITES",
      }).render().props;
      options[0].onClick({});
      const [[action]] = dispatch.mock.calls;
      return action;
    }

    it("should open the ad report dialog with the sponsored tile's data", () => {
      const action = clickReportTileAd({
        url: "https://sponsor.example.com",
        block_key: "block-key-1",
        sponsored_position: 3,
        sponsored_report_url: "https://ads.mozilla.org/v1/report",
      });

      expect(action.type).toBe(at.REPORT_AD_OPEN);
      expect(action.data).toEqual({
        block_key: "block-key-1",
        card_type: "spoc",
        isSponsoredTopSite: 3,
        position: 2,
        reporting_url: "https://ads.mozilla.org/v1/report",
        url: "https://sponsor.example.com",
      });
    });

    it("should use the shim report url for SPOC tiles", () => {
      const action = clickReportTileAd({
        url: "https://sponsor.example.com",
        type: "SPOC",
        sponsored_position: 1,
        shim: { report: "https://ads.mozilla.org/v1/spoc-report" },
      });

      expect(action.data.reporting_url).toBe(
        "https://ads.mozilla.org/v1/spoc-report"
      );
    });
  });
});
