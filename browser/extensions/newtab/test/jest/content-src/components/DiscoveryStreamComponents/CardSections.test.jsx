/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { WIDGET_ROW_COMPONENTS } from "content-src/components/Widgets/WidgetsComponentRegistry.jsx";
import { WidgetLocationContext } from "content-src/components/Widgets/WidgetPlacement";
import { useContext } from "react";
import { render, fireEvent, createEvent } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import {
  CardSections,
  getOrphanTileIndexes,
} from "content-src/components/DiscoveryStreamComponents/CardSections/CardSections";

const PREF_SECTIONS_PERSONALIZATION_ENABLED =
  "discoverystream.sections.personalization.enabled";

const DEFAULT_PROPS = {
  type: "CardGrid",
  firstVisibleTimeStamp: null,
  ctaButtonSponsors: [""],
  anySectionsFollowed: false,
  data: {
    sections: [
      {
        data: [
          {
            id: "card-1",
            title: "Card 1",
            image_src: "image1.jpg",
            url: "https://example.com",
          },
          { id: "card-2" },
          { id: "card-3" },
          { id: "card-4" },
        ],
        receivedRank: 0,
        sectionKey: "section_key",
        title: "title",
        layout: {
          title: "layout_name",
          responsiveLayouts: [
            {
              columnCount: 1,
              tiles: [
                {
                  size: "large",
                  position: 0,
                  hasAd: false,
                  hasExcerpt: true,
                },
                {
                  size: "small",
                  position: 2,
                  hasAd: false,
                  hasExcerpt: false,
                },
                {
                  size: "medium",
                  position: 1,
                  hasAd: true,
                  hasExcerpt: true,
                },
                {
                  size: "small",
                  position: 3,
                  hasAd: false,
                  hasExcerpt: false,
                },
              ],
            },
          ],
        },
      },
    ],
  },
  feed: {
    embed_reference: null,
    url: "https://merino.services.mozilla.com/api/v1/curated-recommendations",
  },
};

// Renders the unconnected CardSections wrapped in a redux Provider (the
// component and its children rely on useSelector). Returns the render result
// plus the dispatch spy so tests can assert on dispatched actions.
function renderCardSections(props = {}, state) {
  const dispatch = jest.fn();
  const utils = render(
    <WrapWithProvider state={state}>
      <CardSections dispatch={dispatch} {...DEFAULT_PROPS} {...props} />
    </WrapWithProvider>
  );
  return { ...utils, dispatch };
}

// A followable single-breakpoint layout reused by the follow/unfollow test.
// Each tile maps a grid position to a size/excerpt.
const FOLLOWABLE_LAYOUT = {
  title: "layout_name",
  responsiveLayouts: [
    {
      columnCount: 1,
      tiles: [
        { size: "large", position: 0, hasAd: false, hasExcerpt: true },
        { size: "small", position: 2, hasAd: false, hasExcerpt: false },
        { size: "medium", position: 1, hasAd: true, hasExcerpt: true },
        { size: "small", position: 3, hasAd: false, hasExcerpt: false },
      ],
    },
  ],
};

describe("<CardSections />", () => {
  it("should render null if no data is provided", () => {
    const { container } = renderCardSections();
    // Verify the section exists normally, so the next assertion is unlikely
    // to be a false positive.
    expect(container.querySelector(".ds-section-wrapper")).toBeInTheDocument();

    const { container: emptyContainer } = renderCardSections({ data: null });
    expect(
      emptyContainer.querySelector(".ds-section-wrapper")
    ).not.toBeInTheDocument();
  });

  it("should render DSEmptyState if sections are falsey", () => {
    const { container } = renderCardSections({
      data: { ...DEFAULT_PROPS.data, sections: [] },
    });
    expect(container.querySelector(".ds-card-grid.empty")).toBeInTheDocument();
  });

  it("should render sections and DSCard components for valid data", () => {
    const { container } = renderCardSections();
    const { sections } = DEFAULT_PROPS.data;
    expect(container.querySelectorAll(".ds-section")).toHaveLength(
      sections.length
    );
    expect(container.querySelectorAll("article.ds-card")).toHaveLength(4);
    expect(container.querySelector(".section-title").textContent).toBe("title");
  });

  it("should send the rendered section order once until it changes", () => {
    const secondSection = {
      ...DEFAULT_PROPS.data.sections[0],
      sectionKey: "second_key",
    };
    const data = {
      sections: [DEFAULT_PROPS.data.sections[0], secondSection],
    };
    const orderActions = dispatch =>
      dispatch.mock.calls.filter(
        ([action]) => action.type === "CARD_SECTIONS_ORDER"
      );

    const dispatch = jest.fn();
    const { rerender } = render(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} data={data} />
      </WrapWithProvider>
    );
    rerender(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} data={data} />
      </WrapWithProvider>
    );
    expect(orderActions(dispatch)).toHaveLength(1);
    expect(orderActions(dispatch)[0][0].data).toEqual({
      sections: ["section_key", "second_key"],
    });

    rerender(
      <WrapWithProvider>
        <CardSections
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          data={{ sections: [secondSection, DEFAULT_PROPS.data.sections[0]] }}
        />
      </WrapWithProvider>
    );
    expect(orderActions(dispatch)).toHaveLength(2);
    expect(orderActions(dispatch)[1][0].data).toEqual({
      sections: ["second_key", "section_key"],
    });
  });

  it("should skip a section with no items available for that section", () => {
    // Verify the section exists normally, so the next assertion is unlikely
    // to be a false positive.
    const { container } = renderCardSections();
    expect(container.querySelector(".ds-section")).toBeInTheDocument();

    const { container: emptyContainer } = renderCardSections({
      data: {
        ...DEFAULT_PROPS.data,
        sections: [{ ...DEFAULT_PROPS.data.sections[0], data: [] }],
      },
    });
    expect(emptyContainer.querySelector(".ds-section")).not.toBeInTheDocument();
  });

  it("should render a placeholder", () => {
    const { container } = renderCardSections({
      data: {
        ...DEFAULT_PROPS.data,
        sections: [
          {
            ...DEFAULT_PROPS.data.sections[0],
            data: [{ placeholder: true }],
          },
        ],
      },
    });
    expect(container.querySelector(".ds-card.placeholder")).toBeInTheDocument();
  });

  // @experiment(remove) { bug 2078816 }
  it("tells a widget in a story section's first card where it renders", () => {
    const original = WIDGET_ROW_COMPONENTS.lists;
    WIDGET_ROW_COMPONENTS.lists = function Probe() {
      return (
        <div
          className="location-probe"
          data-location={useContext(WidgetLocationContext) ?? ""}
        />
      );
    };
    try {
      const state = {
        ...INITIAL_STATE,
        Prefs: {
          ...INITIAL_STATE.Prefs,
          values: {
            ...INITIAL_STATE.Prefs.values,
            "nova.enabled": true,
            "feeds.section.topstories": true,
            "feeds.system.topstories": true,
            "pageLayouts.variant": "widget-first-content-slot",
            "pageLayouts.widgetFirstContentSlot.widget": "lists",
            "widgets.enabled": true,
            "widgets.system.lists.enabled": true,
            "widgets.lists.enabled": true,
          },
        },
      };
      const { container } = renderCardSections(
        {
          data: {
            ...DEFAULT_PROPS.data,
            sections: [
              {
                ...DEFAULT_PROPS.data.sections[0],
                data: [
                  { type: "widget", widgetId: "lists" },
                  ...DEFAULT_PROPS.data.sections[0].data,
                ],
              },
            ],
          },
        },
        state
      );
      expect(
        container.querySelector(".section-widget .location-probe").dataset
          .location
      ).toBe("story_card");
    } finally {
      WIDGET_ROW_COMPONENTS.lists = original;
    }
  });

  // @experiment(remove) { bug 2078816 }
  it.each([
    ["crossword", ".crossword"],
    ["pictureOfTheDay", ".picture-of-the-day"],
  ])("should render %s large in its tile", (widgetId, selector) => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
          "feeds.section.topstories": true,
          "feeds.system.topstories": true,
          "pageLayouts.variant": "widget-first-content-slot",
          "pageLayouts.widgetFirstContentSlot.widget": widgetId,
          "widgets.enabled": true,
          [`widgets.system.${widgetId}.enabled`]: true,
          [`widgets.${widgetId}.enabled`]: true,
        },
      },
      FramedWidgets: {
        [widgetId]: { url: "https://example.mozgcp.net/" },
      },
    };
    const { container } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [
            {
              ...DEFAULT_PROPS.data.sections[0],
              data: [
                { type: "widget", widgetId },
                ...DEFAULT_PROPS.data.sections[0].data,
              ],
            },
          ],
        },
      },
      state
    );
    const cell = container.querySelector(".section-widget");
    expect(cell.className).toContain("col-1-large col-1-position-0");
    expect(cell.querySelector(`${selector}.large-widget`)).toBeInTheDocument();
    expect(container.querySelector("article.ds-card").className).toContain(
      "col-1-position-2"
    );
  });

  it("should pass correct props to DSCard", () => {
    const { container } = renderCardSections();
    const firstCard = container.querySelector("article.ds-card");
    expect(firstCard.querySelector(".title").textContent).toBe("Card 1");
    expect(firstCard.querySelector("img").getAttribute("src")).toBe(
      "image1.jpg"
    );
    expect(firstCard.querySelector("a.ds-card-link").getAttribute("href")).toBe(
      "https://example.com"
    );
  });

  it("should apply correct classNames and position from layout data", () => {
    const { container } = renderCardSections();
    const cards = container.querySelectorAll("article.ds-card");
    expect(cards[0].className).toContain(
      "col-1-large col-1-position-0 col-1-show-excerpt"
    );
    expect(cards[2].className).toContain(
      "col-1-small col-1-position-1 col-1-hide-excerpt"
    );
  });

  it("should apply correct class names for cards with and without excerpts", () => {
    const { container } = renderCardSections();
    const cards = container.querySelectorAll("article.ds-card");
    cards.forEach(card => {
      const { className } = card;
      if (className.includes("small") || className.includes("medium")) {
        expect(className).toContain("hide-excerpt");
        expect(className).not.toContain("show-excerpt");
      } else {
        // The other cards should show excerpts though!
        expect(className).toContain("show-excerpt");
        expect(className).not.toContain("hide-excerpt");
      }
    });
  });

  describe("breakpoints with different card counts", () => {
    // The render is sized to the breakpoint with the most tiles, so the two
    // cards past col-1's tile list exist only for col-4.
    const UNEVEN_LAYOUT = {
      title: "layout_name",
      responsiveLayouts: [
        {
          columnCount: 1,
          tiles: [
            { size: "medium", position: 0, hasExcerpt: false },
            { size: "medium", position: 1, hasExcerpt: false },
          ],
        },
        {
          columnCount: 4,
          tiles: [
            { size: "medium", position: 0, hasExcerpt: false },
            { size: "medium", position: 1, hasExcerpt: false },
            { size: "medium", position: 2, hasExcerpt: false },
            { size: "medium", position: 3, hasExcerpt: false },
          ],
        },
      ],
    };

    const renderUnevenSection = () =>
      renderCardSections({
        data: {
          sections: [
            { ...DEFAULT_PROPS.data.sections[0], layout: UNEVEN_LAYOUT },
          ],
        },
      });

    it("hides the extra cards at the breakpoint with no tile for them", () => {
      const cards =
        renderUnevenSection().container.querySelectorAll("article.ds-card");

      expect(cards[2]).toHaveClass("col-1-hidden");
      expect(cards[3]).toHaveClass("col-1-hidden");
    });

    it("leaves the cards a breakpoint does have tiles for visible", () => {
      const cards =
        renderUnevenSection().container.querySelectorAll("article.ds-card");

      expect(cards[0]).not.toHaveClass("col-1-hidden");
      expect(cards[1]).not.toHaveClass("col-1-hidden");
      cards.forEach(card => expect(card).not.toHaveClass("col-4-hidden"));
    });
  });

  it("should dispatch SECTION_PERSONALIZATION_UPDATE updates with follow and unfollow", () => {
    const fakeDate = "2020-01-01T00:00:00.000Z";
    jest.useFakeTimers().setSystemTime(new Date(fakeDate));

    // mock the pref for followed section
    const state = {
      ...INITIAL_STATE,
      DiscoveryStream: {
        ...INITIAL_STATE.DiscoveryStream,
        sectionPersonalization: {
          section_key_2: {
            isFollowed: true,
            isBlocked: false,
          },
        },
      },
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_SECTIONS_PERSONALIZATION_ENABLED]: true,
        },
      },
    };

    const { container, dispatch } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [
            {
              data: [
                {
                  title: "Card 1",
                  image_src: "image1.jpg",
                  url: "https://example.com",
                },
              ],
              receivedRank: 0,
              sectionKey: "section_key_1",
              title: "title",
              followable: true,
              layout: FOLLOWABLE_LAYOUT,
            },
            {
              data: [
                {
                  title: "Card 2",
                  image_src: "image2.jpg",
                  url: "https://example.com",
                },
              ],
              receivedRank: 0,
              sectionKey: "section_key_2",
              title: "title",
              followable: true,
              layout: FOLLOWABLE_LAYOUT,
            },
          ],
        },
      },
      state
    );

    dispatch.mockClear();
    // section_key_1 is not followed, so its button follows the section.
    fireEvent.click(container.querySelector(".section-follow moz-button"));
    // section_key_2 is followed, so its button unfollows the section.
    fireEvent.click(
      container.querySelector(".section-follow.following moz-button")
    );

    const [
      [followSetAction],
      [followEventAction],
      [followToastAction],
      [unfollowSetAction],
      [unfollowEventAction],
      [unfollowToastAction],
    ] = dispatch.mock.calls;

    expect(followSetAction).toEqual({
      type: "SECTION_PERSONALIZATION_SET",
      data: {
        section_key_2: {
          isFollowed: true,
          isBlocked: false,
        },
        section_key_1: {
          isFollowed: true,
          isBlocked: false,
          followedAt: fakeDate,
        },
      },
      meta: {
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
      },
    });

    expect(followEventAction).toEqual({
      type: "FOLLOW_SECTION",
      data: {
        section: "section_key_1",
        section_position: 0,
        event_source: "MOZ_BUTTON",
      },
      meta: {
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
        skipLocal: true,
      },
    });

    expect(followToastAction).toEqual({
      type: "SHOW_TOAST_MESSAGE",
      data: {
        toastId: "followSectionToast",
        showNotifications: true,
        toastData: { l10nId: "newtab-section-toast-follow", topic: "title" },
      },
      meta: {
        from: "ActivityStream:Main",
        to: "ActivityStream:Content",
        toTarget: "ActivityStream:Content",
        skipMain: true,
      },
    });

    expect(unfollowSetAction).toEqual({
      type: "SECTION_PERSONALIZATION_SET",
      data: {},
      meta: {
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
      },
    });

    expect(unfollowEventAction).toEqual({
      type: "UNFOLLOW_SECTION",
      data: {
        section: "section_key_2",
        section_position: 1,
        event_source: "MOZ_BUTTON",
      },
      meta: {
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
        skipLocal: true,
      },
    });

    expect(unfollowToastAction).toEqual({
      type: "SHOW_TOAST_MESSAGE",
      data: {
        toastId: "unfollowSectionToast",
        showNotifications: true,
        toastData: {
          l10nId: "newtab-section-toast-unfollow",
          topic: "title",
        },
      },
      meta: {
        from: "ActivityStream:Main",
        to: "ActivityStream:Content",
        toTarget: "ActivityStream:Content",
        skipMain: true,
      },
    });

    jest.useRealTimers();
  });

  it("should render <FollowSectionButtonHighlight> when conditions match", () => {
    const fakeMessageData = {
      content: {
        messageType: "FollowSectionButtonHighlight",
      },
    };

    const layout = {
      title: "layout_name",
      responsiveLayouts: [
        {
          columnCount: 1,
          tiles: [{ size: "large", position: 0, hasExcerpt: true }],
        },
      ],
    };

    const state = {
      ...INITIAL_STATE,
      DiscoveryStream: {
        ...INITIAL_STATE.DiscoveryStream,
        sectionPersonalization: {}, // no sections followed
      },
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_SECTIONS_PERSONALIZATION_ENABLED]: true,
        },
      },
      Messages: {
        isVisible: true,
        messageData: fakeMessageData,
      },
    };

    const { container } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [
            {
              data: [
                {
                  title: "Card 1",
                  image_src: "image1.jpg",
                  url: "https://example.com",
                },
              ],
              receivedRank: 0,
              sectionKey: "section_key_1",
              title: "title",
              followable: true,
              layout,
            },
            {
              data: [
                {
                  title: "Card 2",
                  image_src: "image2.jpg",
                  url: "https://example.com",
                },
              ],
              receivedRank: 0,
              sectionKey: "section_key_2",
              title: "title",
              followable: true,
              layout,
            },
          ],
        },
      },
      state
    );

    // Should only render for the first section (sectionPosition === 0).
    expect(
      container.querySelectorAll(".follow-section-button-highlight")
    ).toHaveLength(1);
  });

  it("should not render follow button when section.followable is false", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_SECTIONS_PERSONALIZATION_ENABLED]: true,
        },
      },
    };

    const { container } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [{ ...DEFAULT_PROPS.data.sections[0], followable: false }],
        },
      },
      state
    );

    expect(
      container.querySelectorAll(".section-follow moz-button")
    ).toHaveLength(0);
  });

  it("should render follow button when section.followable is undefined", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_SECTIONS_PERSONALIZATION_ENABLED]: true,
        },
      },
    };

    const { container } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [
            { ...DEFAULT_PROPS.data.sections[0], followable: undefined },
          ],
        },
      },
      state
    );

    expect(
      container.querySelectorAll(".section-follow moz-button")
    ).toHaveLength(1);
  });

  it("should render follow button when section.followable is true", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_SECTIONS_PERSONALIZATION_ENABLED]: true,
        },
      },
    };

    const { container } = renderCardSections(
      {
        data: {
          ...DEFAULT_PROPS.data,
          sections: [{ ...DEFAULT_PROPS.data.sections[0], followable: true }],
        },
      },
      state
    );

    expect(
      container.querySelectorAll(".section-follow moz-button")
    ).toHaveLength(1);
  });

  describe("Keyboard navigation", () => {
    // The card's tabIndex prop is forwarded onto its ".ds-card-link" anchor, so
    // reading the anchor's tabIndex is the DOM-observable equivalent of the old
    // test's DSCard.prop("tabIndex").
    const cardTabIndex = (container, index) =>
      container
        .querySelectorAll("article.ds-card")
        [index].querySelector("a.ds-card-link").tabIndex;

    beforeEach(() => {
      // Mock window.innerWidth to return a value that will make getActiveColumnLayout return "col-1"
      Object.defineProperty(window, "innerWidth", {
        writable: true,
        configurable: true,
        value: 500,
      });
    });

    it("should pass tabIndex={0} to the first card and tabIndex={-1} to other cards", () => {
      const { container } = renderCardSections();

      expect(cardTabIndex(container, 0)).toBe(0);
      expect(cardTabIndex(container, 1)).toBe(-1);
      expect(cardTabIndex(container, 2)).toBe(-1);
    });

    it("should assign tabIndex based on layout position, not recommendation index", () => {
      const { container } = renderCardSections({
        data: {
          sections: [
            {
              ...DEFAULT_PROPS.data.sections[0],
              data: [
                {
                  id: "rec-1",
                  title: "Card 1",
                  image_src: "image1.jpg",
                  url: "https://example.com/1",
                },
                {
                  id: "rec-2",
                  title: "Card 2",
                  image_src: "image2.jpg",
                  url: "https://example.com/2",
                },
              ],
              layout: {
                title: "layout_name",
                responsiveLayouts: [
                  {
                    columnCount: 1,
                    tiles: [
                      {
                        size: "medium",
                        position: 1,
                        hasAd: false,
                        hasExcerpt: true,
                      },
                      {
                        size: "small",
                        position: 0,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                    ],
                  },
                ],
              },
            },
          ],
        },
      });

      expect(cardTabIndex(container, 0)).toBe(-1);
      expect(cardTabIndex(container, 1)).toBe(0);
    });

    it("should update first tab target when the section receives focus after layout changes", () => {
      Object.defineProperty(window, "innerWidth", {
        writable: true,
        configurable: true,
        value: 500,
      });

      const { container } = renderCardSections({
        data: {
          sections: [
            {
              ...DEFAULT_PROPS.data.sections[0],
              data: [
                {
                  id: "rec-1",
                  title: "Card 1",
                  image_src: "image1.jpg",
                  url: "https://example.com/1",
                },
                {
                  id: "rec-2",
                  title: "Card 2",
                  image_src: "image2.jpg",
                  url: "https://example.com/2",
                },
              ],
              layout: {
                title: "layout_name",
                responsiveLayouts: [
                  {
                    columnCount: 1,
                    tiles: [
                      {
                        size: "medium",
                        position: 1,
                        hasAd: false,
                        hasExcerpt: true,
                      },
                      {
                        size: "small",
                        position: 0,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                    ],
                  },
                  {
                    columnCount: 2,
                    tiles: [
                      {
                        size: "small",
                        position: 0,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                      {
                        size: "medium",
                        position: 1,
                        hasAd: false,
                        hasExcerpt: true,
                      },
                    ],
                  },
                ],
              },
            },
          ],
        },
      });

      expect(cardTabIndex(container, 0)).toBe(-1);
      expect(cardTabIndex(container, 1)).toBe(0);

      window.innerWidth = 800;
      // The old test invoked the grid's onFocusCapture prop directly; firing a
      // real focus event runs the same syncLayoutOnFocus handler.
      fireEvent.focus(container.querySelector(".ds-section-grid.ds-card-grid"));

      expect(cardTabIndex(container, 0)).toBe(0);
      expect(cardTabIndex(container, 1)).toBe(-1);
    });

    it("should move the tab stop off a card the synced layout hides", () => {
      Object.defineProperty(window, "innerWidth", {
        writable: true,
        configurable: true,
        value: 500,
      });

      const { container } = renderCardSections({
        data: {
          sections: [
            {
              ...DEFAULT_PROPS.data.sections[0],
              data: [
                {
                  id: "rec-1",
                  title: "Card 1",
                  image_src: "image1.jpg",
                  url: "https://example.com/1",
                },
                {
                  id: "rec-2",
                  title: "Card 2",
                  image_src: "image2.jpg",
                  url: "https://example.com/2",
                },
                {
                  id: "rec-3",
                  title: "Card 3",
                  image_src: "image3.jpg",
                  url: "https://example.com/3",
                },
              ],
              layout: {
                title: "layout_name",
                responsiveLayouts: [
                  {
                    columnCount: 1,
                    tiles: [
                      {
                        size: "small",
                        position: 0,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                      {
                        size: "medium",
                        position: 1,
                        hasAd: false,
                        hasExcerpt: true,
                      },
                      {
                        size: "small",
                        position: 2,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                    ],
                  },
                  {
                    columnCount: 2,
                    tiles: [
                      {
                        size: "small",
                        position: 0,
                        hasAd: false,
                        hasExcerpt: false,
                      },
                      {
                        size: "medium",
                        position: 1,
                        hasAd: false,
                        hasExcerpt: true,
                      },
                    ],
                  },
                ],
              },
            },
          ],
        },
      });

      // Focus the third card (its onFocus prop is fired by focusing its anchor).
      fireEvent.focus(
        container
          .querySelectorAll("article.ds-card")[2]
          .querySelector("a.ds-card-link")
      );
      expect(cardTabIndex(container, 2)).toBe(0);

      window.innerWidth = 800;
      fireEvent.focus(container.querySelector(".ds-section-grid.ds-card-grid"));

      // col-2 has no tile at position 2, so CSS hides that card. The tab stop
      // cannot stay on it or the section becomes unreachable by Tab.
      expect(cardTabIndex(container, 0)).toBe(0);
      expect(cardTabIndex(container, 1)).toBe(-1);
      expect(cardTabIndex(container, 2)).toBe(-1);
    });

    it("should update focused index when onFocus is called", () => {
      const { container } = renderCardSections();

      // The old test read the second card's onFocus prop and invoked it; firing
      // a focus event on that card's anchor runs the same onCardFocus handler.
      fireEvent.focus(
        container
          .querySelectorAll("article.ds-card")[1]
          .querySelector("a.ds-card-link")
      );

      expect(cardTabIndex(container, 1)).toBe(0);
      expect(cardTabIndex(container, 0)).toBe(-1);
    });

    it("should preserve focused index when focus leaves section grid", () => {
      const { container } = renderCardSections();

      fireEvent.focus(
        container
          .querySelectorAll("article.ds-card")[1]
          .querySelector("a.ds-card-link")
      );

      expect(cardTabIndex(container, 1)).toBe(0);

      expect(cardTabIndex(container, 0)).toBe(-1);
      expect(cardTabIndex(container, 1)).toBe(0);
    });

    describe("layout observer", () => {
      afterEach(() => {
        delete globalThis.ResizeObserver;
      });

      it("starts observing the grid on first focus, and only once", () => {
        const observed = [];
        globalThis.ResizeObserver = class {
          observe(el) {
            observed.push(el);
          }
          disconnect() {}
        };
        const novaState = {
          ...INITIAL_STATE,
          Prefs: {
            ...INITIAL_STATE.Prefs,
            values: { ...INITIAL_STATE.Prefs.values, "nova.enabled": true },
          },
        };

        const { container } = renderCardSections({}, novaState);
        const grid = container.querySelector(".ds-section-grid.ds-card-grid");
        expect(observed).toHaveLength(0);

        fireEvent.focus(grid);
        fireEvent.focus(grid);

        expect(observed).toEqual([grid]);
      });
    });

    describe("handleCardKeyDown", () => {
      // The old test invoked the grid's onKeyDown prop with a fully mocked event
      // and asserted on the mocks (preventDefault, querySelector, link.focus).
      // With real DOM we fire the keydown on a real card and observe the
      // equivalent outcomes: the event is defaultPrevented and focus moves to
      // the neighbouring card's link.
      it("should navigate to next card with ArrowRight", () => {
        const { container } = renderCardSections();

        const currentLink = container
          .querySelector("article.ds-card.col-1-position-0")
          .querySelector("a.ds-card-link");
        const keyDownEvent = createEvent.keyDown(currentLink, {
          key: "ArrowRight",
        });
        fireEvent(currentLink, keyDownEvent);

        expect(keyDownEvent.defaultPrevented).toBe(true);
        const targetLink = container
          .querySelector("article.ds-card.col-1-position-1")
          .querySelector("a.ds-card-link");
        expect(document.activeElement).toBe(targetLink);
      });

      it("should navigate to previous card with ArrowLeft", () => {
        const { container } = renderCardSections();

        const currentLink = container
          .querySelector("article.ds-card.col-1-position-1")
          .querySelector("a.ds-card-link");
        const keyDownEvent = createEvent.keyDown(currentLink, {
          key: "ArrowLeft",
        });
        fireEvent(currentLink, keyDownEvent);

        expect(keyDownEvent.defaultPrevented).toBe(true);
        const targetLink = container
          .querySelector("article.ds-card.col-1-position-0")
          .querySelector("a.ds-card-link");
        expect(document.activeElement).toBe(targetLink);
      });
    });
  });

  describe("Daily Briefing v2 BriefingCard", () => {
    let state;

    const MOCK_HEADLINES = [
      {
        id: "h1",
        section: "daily_brief_section",
        isHeadline: true,
        url: "https://example.com/1",
        title: "Headline 1",
        publisher: "Publisher 1",
      },
      {
        id: "h2",
        section: "daily_brief_section",
        isHeadline: true,
        url: "https://example.com/2",
        title: "Headline 2",
        publisher: "Publisher 2",
      },
      {
        id: "h3",
        section: "daily_brief_section",
        isHeadline: true,
        url: "https://example.com/3",
        title: "Headline 3",
        publisher: "Publisher 3",
      },
    ];

    const createBriefingSectionProps = ({
      sectionKey = "daily_brief_section",
      allowsWidget = true,
    } = {}) => ({
      ...DEFAULT_PROPS,
      data: {
        sections: [
          {
            ...DEFAULT_PROPS.data.sections[0],
            sectionKey,
            layout: {
              responsiveLayouts: [
                {
                  columnCount: 1,
                  tiles: [{ position: 0, size: "medium", allowsWidget }],
                },
              ],
            },
          },
        ],
      },
    });

    beforeEach(() => {
      state = {
        ...INITIAL_STATE,
        DiscoveryStream: {
          ...INITIAL_STATE.DiscoveryStream,
          feeds: {
            data: {
              "https://merino.services.mozilla.com/api/v1/curated-recommendations":
                {
                  data: {
                    recommendations: [
                      ...MOCK_HEADLINES,
                      { id: "r1", isHeadline: false },
                    ],
                  },
                  lastUpdated: Date.now(),
                },
            },
          },
        },
        Prefs: {
          ...INITIAL_STATE.Prefs,
          values: {
            ...INITIAL_STATE.Prefs.values,
            "discoverystream.dailyBrief.enabled": true,
            "discoverystream.dailyBrief.sectionId": "daily_brief_section",
          },
        },
      };
    });

    it("should not render BriefingCard when fewer than 3 headlines available", () => {
      state.DiscoveryStream.feeds.data[
        "https://merino.services.mozilla.com/api/v1/curated-recommendations"
      ].data.recommendations = MOCK_HEADLINES.slice(0, 2);

      const { container } = renderCardSections(
        createBriefingSectionProps(),
        state
      );

      expect(container.querySelector(".briefing-card")).not.toBeInTheDocument();
      expect(
        container.querySelectorAll("article.ds-card").length
      ).toBeGreaterThanOrEqual(1);
    });

    it("should not render BriefingCard when section key doesn't match", () => {
      const { container } = renderCardSections(
        createBriefingSectionProps({ sectionKey: "other-section" }),
        state
      );

      expect(container.querySelector(".briefing-card")).not.toBeInTheDocument();
    });
  });
});

describe("<CardSections /> rendering and placeholders", () => {
  const DEFAULT_PROPS = {
    type: "CardGrid",
    firstVisibleTimeStamp: null,
    ctaButtonSponsors: [""],
    anySectionsFollowed: false,
    data: {
      sections: [
        {
          data: [
            {
              id: "card-1",
              title: "Card 1",
              image_src: "image1.jpg",
              url: "https://example.com",
            },
            { id: "card-2" },
            { id: "card-3" },
            { id: "card-4" },
          ],
          receivedRank: 0,
          sectionKey: "section_key",
          title: "title",
          layout: {
            title: "layout_name",
            responsiveLayouts: [
              {
                columnCount: 1,
                tiles: [
                  {
                    size: "large",
                    position: 0,
                    hasAd: false,
                    hasExcerpt: true,
                  },
                  {
                    size: "small",
                    position: 2,
                    hasAd: false,
                    hasExcerpt: false,
                  },
                  {
                    size: "medium",
                    position: 1,
                    hasAd: true,
                    hasExcerpt: true,
                  },
                  {
                    size: "small",
                    position: 3,
                    hasAd: false,
                    hasExcerpt: false,
                  },
                ],
              },
            ],
          },
        },
      ],
    },
    feed: {
      embed_reference: null,
      url: "https://merino.services.mozilla.com/api/v1/curated-recommendations",
    },
  };

  let dispatch;

  beforeEach(() => {
    dispatch = jest.fn();
  });

  afterEach(() => {
    jest.useRealTimers();
  });

  it("should render section wrapper when data is provided", () => {
    const { container } = render(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
    expect(container.querySelector(".ds-section-wrapper")).toBeInTheDocument();
  });

  it("should render null when data is null", () => {
    const { container } = render(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} data={null} />
      </WrapWithProvider>
    );
    expect(
      container.querySelector(".ds-section-wrapper")
    ).not.toBeInTheDocument();
  });

  it("should render DEFAULT_MAX_TILES placeholder cards when spocsLoading is true", () => {
    const sectionData = [];
    for (let i = 0; i < 12; i++) {
      sectionData.push({ id: `card-${i}`, url: `https://example.com/${i}` });
    }
    const sectionProps = {
      ...DEFAULT_PROPS,
      data: {
        ...DEFAULT_PROPS.data,
        sections: [
          {
            ...DEFAULT_PROPS.data.sections[0],
            sectionKey: "section_key_1",
            data: sectionData,
          },
          {
            ...DEFAULT_PROPS.data.sections[0],
            sectionKey: "section_key_2",
            data: sectionData,
          },
        ],
      },
    };

    const { container: baselineContainer } = render(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...sectionProps} />
      </WrapWithProvider>
    );
    expect(baselineContainer.querySelectorAll("article.ds-card")).toHaveLength(
      8
    );

    const { container } = render(
      <WrapWithProvider>
        <CardSections
          dispatch={dispatch}
          {...sectionProps}
          spocsLoading={true}
        />
      </WrapWithProvider>
    );
    expect(container.querySelectorAll(".ds-card.placeholder")).toHaveLength(24);
  });

  it("should render placeholders when responsiveLayouts is empty", () => {
    const { container } = render(
      <WrapWithProvider>
        <CardSections
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          data={{
            ...DEFAULT_PROPS.data,
            sections: [
              {
                ...DEFAULT_PROPS.data.sections[0],
                layout: { responsiveLayouts: [] },
                data: [{ placeholder: true }, { placeholder: true }],
              },
            ],
          }}
        />
      </WrapWithProvider>
    );
    expect(
      container.querySelectorAll(".ds-card.placeholder").length
    ).toBeGreaterThan(0);
  });
});

// Bug 2053264 - hide section layout cards that don't fill their final row.
// getOrphanTileIndexes walks the tiles row by row (a medium/large is 2 units
// tall, so it carries into the next row); the tiles left in an incomplete final
// row are the orphans to hide.
describe("getOrphanTileIndexes", () => {
  const tiles = sizes => sizes.map(size => ({ size }));

  it("returns no orphans when the tiles fill their rows exactly", () => {
    // 3 mediums across 3 columns fill one 2-row block flush.
    const cards = tiles(["medium", "medium", "medium"]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set());
  });

  it("sheds the trailing cards of an incomplete final row", () => {
    // 8 mediums at 3 columns: 6 fill two full blocks, the last 2 are a partial
    // row of 3.
    const cards = tiles(Array(8).fill("medium"));
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set([6, 7]));
  });

  it("sheds a partial final row of mixed sizes", () => {
    // 2 columns: the two mediums fill a row, the trailing small can't.
    const cards = tiles(["medium", "medium", "small"]);
    expect(getOrphanTileIndexes(cards, 2)).toEqual(new Set([2]));
  });

  it("treats an unknown size as a medium", () => {
    const cards = tiles(["medium", "mystery"]);
    expect(getOrphanTileIndexes(cards, 2)).toEqual(new Set());
  });

  it("keeps a full-width row of smalls under mediums", () => {
    // The 2053264 edge case: at 2 columns the two mediums stack over two smalls
    // that pack side by side. Both columns end at height 3 — a flush rectangle,
    // so nothing is hidden even though the smalls only half-fill the last block.
    const cards = tiles(["medium", "medium", "small", "small"]);
    expect(getOrphanTileIndexes(cards, 2)).toEqual(new Set());
  });

  it("keeps smalls that backfill under a medium in a mixed row", () => {
    // 3 columns: the medium takes col0 for two rows; the four smalls fill the
    // rest of both rows (two per remaining column). Columns end level at 2, so
    // nothing is orphaned — the case a per-row tally gets wrong.
    const cards = tiles(["medium", "small", "small", "small", "small"]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set());
  });

  it("keeps a full-width row of smalls on its own", () => {
    // 3 smalls at 3 columns is a complete 1-row-tall row.
    const cards = tiles(["small", "small", "small"]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set());
  });

  it("sheds a small that starts its own partial final row", () => {
    // 3 mediums fill the first block; the trailing small drops to a new row by
    // itself, leaving two empty columns beside it.
    const cards = tiles(["medium", "medium", "medium", "small"]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set([3]));
  });

  it("keeps a full-width row of smalls placed before taller cards", () => {
    // The two smalls fill row 0; the two mediums fill both rows below it.
    const cards = tiles(["small", "small", "medium", "medium"]);
    expect(getOrphanTileIndexes(cards, 2)).toEqual(new Set());
  });

  it("sheds a trailing card left partial after a leading row of smalls", () => {
    // Leading smalls fill row 0, two mediums fill the next block, the last
    // medium starts a new block alone.
    const cards = tiles(["small", "small", "medium", "medium", "medium"]);
    expect(getOrphanTileIndexes(cards, 2)).toEqual(new Set([4]));
  });

  it("keeps a full-width row of smalls sandwiched between medium blocks", () => {
    // 3 mediums, a full row of 3 smalls, then 3 more mediums — every row fills.
    const cards = tiles([
      ...Array(3).fill("medium"),
      ...Array(3).fill("small"),
      ...Array(3).fill("medium"),
    ]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set());
  });

  it("sheds only the straggler past a full trailing row of smalls", () => {
    // A medium block, then 4 smalls: the first 3 complete a row, the 4th is an
    // orphan on its own row.
    const cards = tiles([
      ...Array(3).fill("medium"),
      ...Array(4).fill("small"),
    ]);
    expect(getOrphanTileIndexes(cards, 3)).toEqual(new Set([6]));
  });

  it("sheds a trailing medium after a full row of larges", () => {
    // 4 columns: two larges fill the first block, the medium starts a new one.
    const cards = tiles(["large", "large", "medium"]);
    expect(getOrphanTileIndexes(cards, 4)).toEqual(new Set([2]));
  });
});
