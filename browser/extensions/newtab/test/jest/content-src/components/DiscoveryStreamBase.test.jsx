/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render } from "@testing-library/react";
import {
  _DiscoveryStreamBase as DiscoveryStreamBase,
  isAllowedCSS,
} from "content-src/components/DiscoveryStreamBase/DiscoveryStreamBase";
import React from "react";

// The old Enzyme test used shallow() and matched children by React component
// identity (e.g. .type() === CardGrid). RTL renders the full tree, so the heavy
// child components are mocked to render an identifiable marker we can query in
// the DOM. HorizontalRule is intentionally left un-mocked because it renders a
// trivial <hr class="ds-hr">.
jest.mock("content-src/components/TopSites/TopSites", () => ({
  TopSites: () => <div data-testid="topsites-mock" />,
}));
jest.mock(
  "content-src/components/DiscoveryStreamComponents/CardGrid/CardGrid",
  () => ({
    CardGrid: () => <div data-testid="cardgrid-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/CardSections/CardSections",
  () => ({
    CardSections: () => <div data-testid="cardsections-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/Navigation/Navigation",
  () => ({
    Navigation: () => <div data-testid="navigation-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/SectionTitle/SectionTitle",
  () => ({
    SectionTitle: () => <div data-testid="sectiontitle-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/Highlights/Highlights",
  () => ({
    Highlights: () => <div data-testid="highlights-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/PrivacyLink/PrivacyLink",
  () => ({
    PrivacyLink: () => <div data-testid="privacylink-mock" />,
  })
);
jest.mock(
  "content-src/components/DiscoveryStreamComponents/ReportContent/ReportContent",
  () => ({
    ReportContent: () => <div data-testid="reportcontent-mock" />,
  })
);
jest.mock("content-src/components/Widgets/Widgets", () => ({
  Widgets: ({ placement }) => (
    <div data-testid="widgets-mock" data-placement={placement ?? ""} />
  ),
}));
jest.mock(
  "content-src/components/CollapsibleSection/CollapsibleSection",
  () => ({
    CollapsibleSection: ({ children }) => (
      <div data-testid="content-feed">{children}</div>
    ),
  })
);
jest.mock("content-src/components/ErrorBoundary/ErrorBoundary", () => ({
  ErrorBoundary: ({ children }) => <>{children}</>,
}));
jest.mock("content-src/components/MessageWrapper/MessageWrapper", () => ({
  MessageWrapper: ({ children }) => (
    <div data-testid="message-wrapper">{children}</div>
  ),
}));
jest.mock(
  "content-src/components/ExternalComponentWrapper/ExternalComponentWrapper",
  () => ({
    ExternalComponentWrapper: ({ type, className }) => (
      <div
        data-testid="asrouter-message"
        data-type={type}
        className={className}
      />
    ),
  })
);

describe("<isAllowedCSS>", () => {
  it("should allow colors", () => {
    expect(isAllowedCSS("color", "red")).toBe(true);
  });

  it("should allow chrome urls", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("chrome://global/skin/icons/info.svg")`
      )
    ).toBe(true);
  });

  it("should allow chrome urls", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("chrome://browser/skin/history.svg")`
      )
    ).toBe(true);
  });

  it("should allow allowed https urls", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("https://img-getpocket.cdn.mozilla.net/media/image.png")`
      )
    ).toBe(true);
  });

  it("should disallow other https urls", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("https://mozilla.org/media/image.png")`
      )
    ).toBe(false);
  });

  it("should disallow other protocols", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("ftp://mozilla.org/media/image.png")`
      )
    ).toBe(false);
  });

  it("should allow allowed multiple valid urls", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("https://img-getpocket.cdn.mozilla.net/media/image.png"), url("chrome://browser/skin/history.svg")`
      )
    ).toBe(true);
  });

  it("should disallow if any invaild", () => {
    expect(
      isAllowedCSS(
        "background-image",
        `url("chrome://browser/skin/history.svg"), url("ftp://mozilla.org/media/image.png")`
      )
    ).toBe(false);
  });
});

describe("<DiscoveryStreamBase>", () => {
  let wrapper;
  let instanceRef;

  function mountComponent(props = {}, prefs = {}) {
    const defaultProps = {
      layout: [],
      feeds: { loaded: true },
      spocs: {
        loaded: true,
        data: { spocs: null },
      },
      ...props,
    };
    instanceRef = React.createRef();
    return render(
      <DiscoveryStreamBase
        ref={instanceRef}
        locale="en-US"
        dispatch={jest.fn()}
        DiscoveryStream={defaultProps}
        Prefs={{
          values: {
            "feeds.section.topstories": true,
            "feeds.system.topstories": true,
            "feeds.topsites": true,
            ...prefs,
          },
        }}
        App={{
          locale: "en-US",
        }}
        document={{
          documentElement: { lang: "en-US" },
        }}
        Sections={[
          {
            id: "topstories",
            learnMore: { link: {} },
            pref: {},
          },
        ]}
      />
    );
  }

  beforeEach(() => {
    wrapper = mountComponent();
  });

  it("should render something if spocs are not loaded", () => {
    wrapper = mountComponent({
      spocs: { loaded: false, data: { spocs: null } },
    });

    expect(wrapper.container).not.toBeEmptyDOMElement();
  });

  it("should render something if feeds are not loaded", () => {
    wrapper = mountComponent({ feeds: { loaded: false } });

    expect(wrapper.container).not.toBeEmptyDOMElement();
  });

  it("should render nothing with no layout", () => {
    expect(wrapper.container).not.toBeEmptyDOMElement();
    expect(wrapper.queryByTestId("content-feed")).not.toBeInTheDocument();
  });

  it("should render a HorizontalRule component", () => {
    wrapper = mountComponent({
      layout: [{ components: [{ type: "HorizontalRule" }] }],
    });

    expect(
      wrapper.container.querySelector(".ds-column-grid .ds-hr")
    ).toBeInTheDocument();
  });

  it("should render a CardGrid component", () => {
    wrapper = mountComponent({
      layout: [{ components: [{ properties: {}, type: "CardGrid" }] }],
    });

    expect(
      wrapper.container.querySelector(
        ".ds-column-grid [data-testid='cardgrid-mock']"
      )
    ).toBeInTheDocument();
  });

  it("should render a Navigation component", () => {
    wrapper = mountComponent({
      layout: [{ components: [{ properties: {}, type: "Navigation" }] }],
    });

    expect(
      wrapper.container.querySelector(
        ".ds-column-grid [data-testid='navigation-mock']"
      )
    ).toBeInTheDocument();
  });

  it("should render a SectionTitle component", () => {
    wrapper = mountComponent({
      layout: [{ components: [{ properties: {}, type: "SectionTitle" }] }],
    });

    expect(
      wrapper.container.querySelector(
        ".ds-column-grid [data-testid='sectiontitle-mock']"
      )
    ).toBeInTheDocument();
  });

  it("should render TopSites", () => {
    wrapper = mountComponent({
      layout: [{ components: [{ properties: {}, type: "TopSites" }] }],
    });

    expect(
      wrapper.container.querySelector(
        ".ds-top-sites [data-testid='topsites-mock']"
      )
    ).toBeInTheDocument();
  });

  it("should not render ReportContent when no reporting is enabled", () => {
    expect(wrapper.queryByTestId("reportcontent-mock")).not.toBeInTheDocument();
  });

  it("should render ReportContent when sponsored tile reporting is enabled", () => {
    wrapper = mountComponent(
      {},
      { "discoverystream.reportAds.tiles.enabled": true }
    );

    expect(wrapper.getByTestId("reportcontent-mock")).toBeInTheDocument();
  });

  describe("#onStyleMount", () => {
    let parseStub;

    beforeEach(() => {
      parseStub = jest.spyOn(JSON, "parse").mockImplementation(() => undefined);
    });

    afterEach(() => {
      parseStub.mockRestore();
    });

    it("should return if no style", () => {
      expect(instanceRef.current.onStyleMount()).toBeUndefined();
      expect(parseStub).not.toHaveBeenCalled();
    });

    it("should insert rules", () => {
      const sheetStub = { insertRule: jest.fn(), cssRules: [{}] };
      parseStub.mockReturnValue([
        [
          null,
          {
            ".ds-message": "margin-bottom: -20px",
          },
          null,
          null,
        ],
      ]);
      instanceRef.current.onStyleMount({ sheet: sheetStub, dataset: {} });

      expect(sheetStub.insertRule).toHaveBeenCalledTimes(1);
      expect(sheetStub.insertRule).toHaveBeenCalledWith(
        "DUMMY#CSS.SELECTOR {}"
      );
    });
  });
});

describe("<DiscoveryStreamBase> ASRouterNewTabMessage positions", () => {
  // Layout includes TopSites (extracted and rendered separately) plus a
  // HorizontalRule so that layoutRender is non-empty after extraction,
  // causing the CollapsibleSection (content feed) to render.
  const POSITION_TEST_LAYOUT = [
    { components: [{ properties: {}, type: "TopSites" }] },
    { components: [{ type: "HorizontalRule" }] },
  ];

  const BASE_PREFS = {
    "feeds.section.topstories": true,
    "feeds.system.topstories": true,
    "feeds.topsites": true,
    "widgets.system.enabled": true,
    "nova.enabled": false,
  };

  function mountForPositionTest(messagesProps, prefsOverrides = {}) {
    return render(
      <DiscoveryStreamBase
        locale="en-US"
        DiscoveryStream={{
          layout: POSITION_TEST_LAYOUT,
          feeds: { loaded: true },
          spocs: { loaded: true, data: { spocs: null } },
        }}
        Messages={messagesProps}
        Prefs={{ values: { ...BASE_PREFS, ...prefsOverrides } }}
        App={{ locale: "en-US" }}
        document={{ documentElement: { lang: "en-US" } }}
        Sections={[{ id: "topstories", learnMore: { link: {} }, pref: {} }]}
        dispatch={jest.fn()}
      />
    );
  }

  function makeMessages({ position, isVisible = true } = {}) {
    return {
      isVisible,
      messageData: {
        content: {
          messageType: "ASRouterNewTabMessage",
          ...(position !== undefined ? { position } : {}),
        },
      },
    };
  }

  // Returns the index of an element in a pre-order traversal of the container,
  // standing in for Enzyme's children() document-order comparisons.
  function preOrderIndex(container, element) {
    return [...container.querySelectorAll("*")].indexOf(element);
  }

  function findPositionIndices(container) {
    const messageElement = container.querySelector(
      "[data-testid='asrouter-message']"
    );
    const topSitesElement = container.querySelector(".ds-layout-topsites");
    const widgetsElement = container.querySelector(".ds-layout-widgets");
    const contentFeedElement = container.querySelector(
      "[data-testid='content-feed']"
    );

    return {
      messageIdx: messageElement
        ? preOrderIndex(container, messageElement)
        : -1,
      topSitesIdx: topSitesElement
        ? preOrderIndex(container, topSitesElement)
        : -1,
      widgetsIdx: widgetsElement
        ? preOrderIndex(container, widgetsElement)
        : -1,
      contentFeedIdx: contentFeedElement
        ? preOrderIndex(container, contentFeedElement)
        : -1,
    };
  }

  it("does not render ASRouterNewTabMessage when there is no message", () => {
    const { container } = mountForPositionTest(null);
    expect(
      container.querySelectorAll("[data-testid='asrouter-message']")
    ).toHaveLength(0);
  });

  it("does not render ASRouterNewTabMessage when isVisible is false", () => {
    const { container } = mountForPositionTest(
      makeMessages({ isVisible: false })
    );
    expect(
      container.querySelectorAll("[data-testid='asrouter-message']")
    ).toHaveLength(0);
  });

  it("renders exactly one ASRouterNewTabMessage for any configured position", () => {
    for (const position of [
      // ABOVE_TOPSITES is intentionally skipped, since for non-Nova, it's rendered
      // by Base.jsx, since it makes it easier for browser_asrouter_newtab_message
      // to test it that way.
      "ABOVE_WIDGETS",
      "ABOVE_CONTENT_FEED",
    ]) {
      const { container, unmount } = mountForPositionTest(
        makeMessages({ position })
      );
      expect(
        container.querySelectorAll("[data-testid='asrouter-message']")
      ).toHaveLength(1);
      unmount();
    }
  });

  it("renders ASRouterNewTabMessage after TopSites and before Widgets for ABOVE_WIDGETS", () => {
    const { container } = mountForPositionTest(
      makeMessages({ position: "ABOVE_WIDGETS" })
    );
    const { messageIdx, topSitesIdx, widgetsIdx } =
      findPositionIndices(container);

    expect(topSitesIdx).toBeGreaterThan(-1);
    expect(widgetsIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(topSitesIdx);
    expect(messageIdx).toBeLessThan(widgetsIdx);
  });

  it("renders ASRouterNewTabMessage after Widgets and before the content feed for ABOVE_CONTENT_FEED", () => {
    const { container } = mountForPositionTest(
      makeMessages({ position: "ABOVE_CONTENT_FEED" })
    );
    const { messageIdx, widgetsIdx, contentFeedIdx } =
      findPositionIndices(container);

    expect(widgetsIdx).toBeGreaterThan(-1);
    expect(contentFeedIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(widgetsIdx);
    expect(messageIdx).toBeLessThan(contentFeedIdx);
  });

  // @nova-cleanup(remove-conditional): Delete this test; it only exists to cover the !novaEnabled guard in DiscoveryStreamBase.
  // When Nova is enabled, Base.jsx is responsible for rendering the
  // ASRouterNewTabMessages, rather than DiscoveryStreamBase.
  it("does not render ASRouterNewTabMessage when Nova is enabled", () => {
    const { container } = mountForPositionTest(
      makeMessages({ position: "ABOVE_TOPSITES" }),
      {
        "nova.enabled": true,
      }
    );
    expect(
      container.querySelectorAll("[data-testid='asrouter-message']")
    ).toHaveLength(0);
  });

  // Bug 2061741: the side-by-side layouts need one element to place beside the
  // widgets and to paint a panel behind.
  describe("layout-content-column", () => {
    it("wraps the content feed but not the widgets", () => {
      const { container } = mountForPositionTest(null);
      const column = container.querySelector(".layout-content-column");

      expect(column).not.toBeNull();
      expect(
        column.querySelector("[data-testid='content-feed']")
      ).not.toBeNull();
      expect(column.querySelector(".ds-layout-widgets")).toBeNull();
      expect(container.querySelector(".ds-layout-widgets")).not.toBeNull();
    });

    // Base.jsx hands this in as a prop, so the slot sits inside the column.
    it("contains the aboveContentFeed slot", () => {
      const { container } = render(
        <DiscoveryStreamBase
          locale="en-US"
          DiscoveryStream={{
            layout: POSITION_TEST_LAYOUT,
            feeds: { loaded: true },
            spocs: { loaded: true, data: { spocs: null } },
          }}
          Messages={null}
          Prefs={{ values: { ...BASE_PREFS, "nova.enabled": true } }}
          App={{ locale: "en-US" }}
          document={{ documentElement: { lang: "en-US" } }}
          Sections={[{ id: "topstories", learnMore: { link: {} }, pref: {} }]}
          dispatch={jest.fn()}
          aboveContentFeed={<div data-testid="above-content-feed" />}
        />
      );
      const column = container.querySelector(".layout-content-column");

      expect(
        column.querySelector("[data-testid='above-content-feed']")
      ).not.toBeNull();
    });
  });
  describe("thematic spaces", () => {
    const THEMATIC_PREFS = {
      ...BASE_PREFS,
      "nova.enabled": true,
      "pageLayouts.variant": "spaces-thematic-v1",
      "widgets.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.lists.enabled": true,
    };

    function mountThematic(spacesConfig, prefOverrides = {}) {
      return render(
        <DiscoveryStreamBase
          locale="en-US"
          DiscoveryStream={{
            layout: POSITION_TEST_LAYOUT,
            feeds: {
              loaded: true,
              data: {
                "https://example.com/feed": {
                  data: { sections: [{ sectionKey: "top_stories_section" }] },
                },
              },
            },
            spocs: { loaded: true, data: { spocs: null } },
          }}
          Messages={null}
          Prefs={{
            values: {
              ...THEMATIC_PREFS,
              ...prefOverrides,
              trainhopConfig: { spaces: spacesConfig },
            },
          }}
          App={{ locale: "en-US" }}
          document={{ documentElement: { lang: "en-US" } }}
          Sections={[
            { id: "topstories", learnMore: { link: {} }, pref: {} },
            { id: "highlights", enabled: true },
          ]}
          dispatch={jest.fn()}
        />
      );
    }

    const CONFIG = {
      order: ["home", "widgets"],
      default: "home",
      spaces: {
        home: { label: "Home" },
        widgets: { label: "Widgets", widgets: ["lists"] },
      },
    };

    it("renders a widgets-only space without a feed column", () => {
      const { container } = mountThematic(CONFIG);
      const home = container.querySelector("#spaces-panel-home");
      const widgets = container.querySelector("#spaces-panel-widgets");

      expect(home.querySelector(".layout-content-column")).not.toBeNull();
      expect(home.querySelector(".ds-layout-widgets")).toBeNull();
      expect(widgets.querySelector(".layout-content-column")).toBeNull();
      expect(widgets.querySelector(".ds-layout-widgets")).not.toBeNull();
    });

    it("shows the arrows alongside the tablist when the config asks", () => {
      const { container } = mountThematic({ ...CONFIG, arrows: true });

      expect(container.querySelectorAll(".spaces-arrow-rail")).toHaveLength(2);
      expect(container.querySelector('[role="tablist"]')).not.toBeNull();
    });

    it("renders Recent activity in the default space when none is marked", () => {
      const { container } = mountThematic(CONFIG);

      expect(
        container.querySelector("#spaces-panel-home .layout-highlights-column")
      ).not.toBeNull();
    });

    it("renders Recent activity only in the space marked for it", () => {
      const { container } = mountThematic(
        {
          ...CONFIG,
          order: ["mystuff", ...CONFIG.order],
          spaces: {
            ...CONFIG.spaces,
            mystuff: { label: "My Stuff", activity: true },
          },
        },
        { "feeds.section.highlights": true }
      );
      const mystuff = container.querySelector("#spaces-panel-mystuff");

      expect(mystuff.querySelector(".layout-highlights-column")).not.toBeNull();
      expect(mystuff.querySelector(".layout-content-column")).toBeNull();
      expect(
        container.querySelectorAll(".layout-highlights-column")
      ).toHaveLength(1);
    });

    it("tells each space's widgets whether it is Home or the library", () => {
      const { container } = mountThematic(
        { ...CONFIG, widgetLibrary: "widgets" },
        {
          "widgets.home": '["lists"]',
          "widgets.system.focusTimer.enabled": true,
          "widgets.focusTimer.enabled": true,
        }
      );
      const placementIn = id =>
        container
          .querySelector(`#spaces-panel-${id} [data-testid='widgets-mock']`)
          .getAttribute("data-placement");

      expect(placementIn("home")).toBe("home");
      expect(placementIn("widgets")).toBe("library");
    });

    it("gives the widgets no placement without a library", () => {
      const { container } = mountThematic(CONFIG);

      expect(
        container
          .querySelector("#spaces-panel-widgets [data-testid='widgets-mock']")
          .getAttribute("data-placement")
      ).toBe("");
    });

    it("shows only the tablist otherwise", () => {
      const { container } = mountThematic(CONFIG);

      expect(container.querySelector(".spaces-arrow-rail")).toBeNull();
      expect(container.querySelector('[role="tablist"]')).not.toBeNull();
    });
  });
});
