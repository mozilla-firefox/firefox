import { render, fireEvent, act } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import {
  TopSite,
  TopSiteLink,
  TopSiteAddButton,
  TopSitePlaceholder,
  _TopSiteList as TopSiteList,
} from "content-src/components/TopSites/TopSite";
import { buildTopSitesList } from "content-src/components/TopSites/TopSiteListContainer";
import { INTERSECTION_RATIO } from "content-src/components/TopSites/TopSiteImpressionWrapper";
import { PanelListItems } from "content-src/components/LinkMenu/PanelListItems";
import { actionTypes as at } from "common/Actions.mjs";
import {
  TOP_SITES_DEFAULT_ROWS,
  TOP_SITES_MAX_SITES_PER_ROW,
} from "common/Reducers.sys.mjs";
import React from "react";

const perfSvc = {
  mark() {},
  getMostRecentAbsMarkStartByName() {},
};

// Mirrors the legacy shared DEFAULT_PROPS spread into the <TopSiteList> tests.
// The unconnected _TopSiteList reads App/Prefs off props, so they are supplied
// directly here.
const DEFAULT_PROPS = {
  Prefs: { values: { featureConfig: {} } },
  TopSites: { initialized: true, rows: [] },
  App: {
    isForStartupCache: false,
  },
  TopSitesRows: TOP_SITES_DEFAULT_ROWS,
  TopSitesMaxSitesPerRow: TOP_SITES_MAX_SITES_PER_ROW,
  topSiteIconType: () => "no_image",
  dispatch() {},
  perfSvc,
};

// Walks the React element tree returned by a class instance's render() and
// returns the first element whose type matches `type`. Lets us read the props
// passed to a child component (e.g. PanelListItems) without mounting it, mirroring the
// legacy enzyme wrapper.find(Component).props().
function findElementByType(node, type) {
  if (Array.isArray(node)) {
    for (const child of node) {
      const found = findElementByType(child, type);
      if (found) {
        return found;
      }
    }
    return null;
  }
  if (!node || typeof node !== "object") {
    return null;
  }
  if (node.type === type) {
    return node;
  }
  const { children } = node.props || {};
  const childArray = Array.isArray(children) ? children : [children];
  for (const child of childArray) {
    const found = findElementByType(child, type);
    if (found) {
      return found;
    }
  }
  return null;
}

const DEFAULT_LINK = {
  url: "https://example.com",
  hostname: "example.com",
  title: "Example",
  label: "Example",
  iconType: "no_image",
};

// Unique urls keep React keys distinct when the rows are rendered.
const makeRows = n =>
  Array.from({ length: n }, (_, i) => ({
    ...DEFAULT_LINK,
    url: `https://example${i}.com`,
  }));

const LIST_PROPS = {
  dispatch: jest.fn(),
  onDragEvent: jest.fn(),
  topSiteIconType: () => "no_image",
  topSitesMaxSitesPerRow: 8,
  TopSitesRows: 1,
  App: { isForStartupCache: {} },
  Prefs: { values: {} },
};

function renderList(sites, props = {}) {
  return render(
    <WrapWithProvider>
      <TopSiteList {...LIST_PROPS} sites={sites} {...props} />
    </WrapWithProvider>
  );
}

describe("<TopSiteLink>", () => {
  let link;
  beforeEach(() => {
    link = { url: "https://foo.com", screenshot: "foo.jpg", hostname: "foo" };
  });
  it("should add the right url", () => {
    link.url = "https://www.foobar.org";
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelector("a")).toHaveAttribute(
      "href",
      "https://www.foobar.org"
    );
  });
  it("should not add the url to the href if it a search shortcut", () => {
    link.searchTopSite = true;
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelector("a").hasAttribute("href")).toBe(false);
  });
  it("should have rtl direction automatically set for text", () => {
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelector("[dir='auto']")).toBeInTheDocument();
  });
  it("should render a title", () => {
    const { container } = render(<TopSiteLink link={link} title="foobar" />);
    expect(container.querySelector(".title").textContent).toBe("foobar");
  });
  it("should have only the title as the text of the link", () => {
    const { container } = render(<TopSiteLink link={link} title="foobar" />);
    expect(container.querySelector("a").textContent).toBe("foobar");
  });
  it("should render the pin icon for pinned links", () => {
    link.isPinned = true;
    link.pinnedIndex = 7;
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelectorAll(".icon-pin-small")).toHaveLength(1);
  });
  it("should not render the pin icon for non pinned links", () => {
    link.isPinned = false;
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelectorAll(".icon-pin-small")).toHaveLength(0);
  });
  it("should render the first letter of the title as a fallback for missing icons", () => {
    const { container } = render(<TopSiteLink link={link} title={"foo"} />);
    expect(container.querySelector(".icon-wrapper")).toHaveAttribute(
      "data-fallback",
      "f"
    );
  });
  it("should render the tippy top icon if provided and not a small icon", () => {
    link.tippyTopIcon = "foo.png";
    link.backgroundColor = "#FFFFFF";
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelectorAll(".screenshot")).toHaveLength(0);
    expect(container.querySelectorAll(".default-icon")).toHaveLength(0);
    const tippyTop = container.querySelector(".rich-icon");
    expect(tippyTop.style.backgroundImage).toBe("url(foo.png)");
    // The DOM reads the color back as rgb(), not the input hex "#FFFFFF".
    expect(tippyTop.style.backgroundColor).toBe("rgb(255, 255, 255)");
  });
  it("should render a rich icon if provided and not a small icon", () => {
    link.favicon = "foo.png";
    link.faviconSize = 196;
    link.backgroundColor = "#FFFFFF";
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelectorAll(".screenshot")).toHaveLength(0);
    expect(container.querySelectorAll(".default-icon")).toHaveLength(0);
    const richIcon = container.querySelector(".rich-icon");
    expect(richIcon.style.backgroundImage).toBe("url(foo.png)");
    expect(richIcon.style.backgroundColor).toBe("rgb(255, 255, 255)");
  });
  it("should not render a rich icon if it is smaller than 96x96", () => {
    link.favicon = "foo.png";
    link.faviconSize = 48;
    link.backgroundColor = "#FFFFFF";
    const { container } = render(<TopSiteLink link={link} />);
    expect(container.querySelectorAll(".default-icon")).toHaveLength(1);
    expect(container.querySelectorAll(".rich-icon")).toHaveLength(0);
  });
  describe("#getFaviconURL", () => {
    const IMAGE_URL = "https://images.example/tile.png";
    const OHTTP_URL = `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(IMAGE_URL)}`;

    function getFaviconURL(linkProps, ohttpSponsoredImagesEnabled = true) {
      return new TopSiteLink({
        link: { ...link, favicon: IMAGE_URL, ...linkProps },
        ohttpSponsoredImagesEnabled,
      }).getFaviconURL();
    }

    it("loads sponsored tile images over OHTTP when enabled", () => {
      expect(getFaviconURL({ sponsored_position: 1 })).toBe(OHTTP_URL);
    });

    it("loads sponsored tile images directly when disabled", () => {
      expect(getFaviconURL({ sponsored_position: 1 }, false)).toBe(IMAGE_URL);
    });

    it("loads images for shortcuts that are not sponsored directly", () => {
      expect(getFaviconURL({})).toBe(IMAGE_URL);
    });

    it("loads images that are not https directly", () => {
      const dataURL = "data:image/png;base64,AAAA";
      expect(getFaviconURL({ sponsored_position: 1, favicon: dataURL })).toBe(
        dataURL
      );
    });
  });

  it("should apply just the default class name to the outer link if props.className is falsey", () => {
    const { container } = render(<TopSiteLink className={false} />);
    expect(container.querySelector("li")).toHaveClass("top-site-outer");
  });
  it("should add props.className to the outer link element", () => {
    const { container } = render(<TopSiteLink className="foo bar" />);
    expect(container.querySelector("li")).toHaveClass(
      "top-site-outer",
      "foo",
      "bar"
    );
  });
  describe("#_allowDrop", () => {
    let ref;
    let event;
    let rerender;
    beforeEach(() => {
      event = {
        dataTransfer: {
          types: ["text/topsite-index"],
        },
      };
      ref = React.createRef();
      ({ rerender } = render(
        <TopSiteLink isDraggable={true} onDragEvent={() => {}} ref={ref} />
      ));
    });
    it("should be droppable for basic case", () => {
      const result = ref.current._allowDrop(event);
      expect(result).toBe(true);
    });
    it("should not be droppable for sponsored_position", () => {
      rerender(
        <TopSiteLink
          isDraggable={true}
          onDragEvent={() => {}}
          link={{ sponsored_position: 1 }}
          ref={ref}
        />
      );
      const result = ref.current._allowDrop(event);
      expect(result).toBe(false);
    });
    it("should not be droppable for link.type", () => {
      rerender(
        <TopSiteLink
          isDraggable={true}
          onDragEvent={() => {}}
          link={{ type: "SPOC" }}
          ref={ref}
        />
      );
      const result = ref.current._allowDrop(event);
      expect(result).toBe(false);
    });
  });
  describe("#onDragEvent", () => {
    let ref;
    let rerender;
    let simulate;
    beforeEach(() => {
      ref = React.createRef();
      ({ rerender } = render(
        <TopSiteLink isDraggable={true} onDragEvent={() => {}} ref={ref} />
      ));
      simulate = type => {
        const event = {
          dataTransfer: { setData() {}, types: { includes() {} } },
          preventDefault() {
            this.prevented = true;
          },
          target: { blur() {} },
          type,
        };
        ref.current.onDragEvent(event);
        return event;
      };
    });
    it("should allow clicks without dragging", () => {
      simulate("mousedown");
      simulate("mouseup");

      const event = simulate("click");

      expect(event.prevented).toBeFalsy();
    });
    it("should prevent clicks after dragging", () => {
      simulate("mousedown");
      simulate("dragstart");
      simulate("dragenter");
      simulate("drop");
      simulate("dragend");
      simulate("mouseup");

      const event = simulate("click");

      expect(event.prevented).toBeTruthy();
    });
    it("should allow clicks after dragging then clicking", () => {
      simulate("mousedown");
      simulate("dragstart");
      simulate("dragenter");
      simulate("drop");
      simulate("dragend");
      simulate("mouseup");
      simulate("click");

      simulate("mousedown");
      simulate("mouseup");

      const event = simulate("click");

      expect(event.prevented).toBeFalsy();
    });
    it("should prevent dragging with sponsored_position from dragstart", () => {
      const preventDefault = jest.fn();
      // eslint-disable-next-line no-shadow
      const blur = jest.fn();
      rerender(
        <TopSiteLink
          isDraggable={true}
          onDragEvent={() => {}}
          link={{ sponsored_position: 1 }}
          ref={ref}
        />
      );
      ref.current.onDragEvent({
        type: "dragstart",
        preventDefault,
        target: { blur },
      });
      expect(preventDefault).toHaveBeenCalledTimes(1);
      expect(blur).toHaveBeenCalledTimes(1);
      expect(ref.current.dragged).toBeUndefined();
    });
    it("should prevent dragging with link.shim from dragstart", () => {
      const preventDefault = jest.fn();
      // eslint-disable-next-line no-shadow
      const blur = jest.fn();
      rerender(
        <TopSiteLink
          isDraggable={true}
          onDragEvent={() => {}}
          link={{ type: "SPOC" }}
          ref={ref}
        />
      );
      ref.current.onDragEvent({
        type: "dragstart",
        preventDefault,
        target: { blur },
      });
      expect(preventDefault).toHaveBeenCalledTimes(1);
      expect(blur).toHaveBeenCalledTimes(1);
      expect(ref.current.dragged).toBeUndefined();
    });
  });

  describe("#generateColor", () => {
    let colors;
    beforeEach(() => {
      colors = "#0090ED,#FF4F5F,#2AC3A2";
    });

    it("should generate a random color but always pick the same color for the same string", () => {
      const { container } = render(
        <TopSiteLink colors={colors} title={"food"} link={link} />
      );

      const iconWrapper = container.querySelector(".icon-wrapper");
      expect(iconWrapper).toHaveAttribute("data-fallback", "f");
      // colors.split(",")[1] === "#FF4F5F"
      expect(iconWrapper.style.backgroundColor).toBe("rgb(255, 79, 95)");
      expect(true).toBe(true);
    });

    it("should generate a different random color", () => {
      const { container } = render(
        <TopSiteLink colors={colors} title={"fam"} link={link} />
      );

      // colors.split(",")[2] === "#2AC3A2"
      expect(
        container.querySelector(".icon-wrapper").style.backgroundColor
      ).toBe("rgb(42, 195, 162)");
      expect(true).toBe(true);
    });

    it("should generate a third random color", () => {
      const { container } = render(
        <TopSiteLink colors={colors} title={"foo"} />
      );

      const iconWrapper = container.querySelector(".icon-wrapper");
      expect(iconWrapper).toHaveAttribute("data-fallback", "f");
      // colors.split(",")[0] === "#0090ED"
      expect(iconWrapper.style.backgroundColor).toBe("rgb(0, 144, 237)");
      expect(true).toBe(true);
    });
  });
});

describe("<TopSite>", () => {
  let link;
  beforeEach(() => {
    link = { url: "https://foo.com", screenshot: "foo.jpg", hostname: "foo" };
  });

  // Build IntersectionObserver class with the arg `entries` for the intersect callback.
  function buildIntersectionObserver(entries) {
    return class {
      constructor(callback) {
        this.callback = callback;
      }

      observe() {
        this.callback(entries);
      }

      unobserve() {}
    };
  }

  // The legacy test shallow-rendered <TopSite>; RTL does a full mount, so its
  // connected children need a redux Provider. A ref exposes the instance for the
  // white-box PanelListItems-prop assertions.
  function renderTopSite(props, ref) {
    return render(
      <WrapWithProvider>
        <TopSite
          dispatch={jest.fn()}
          onDragEvent={jest.fn()}
          {...props}
          ref={ref}
        />
      </WrapWithProvider>
    );
  }

  it("should render a TopSite", () => {
    const { container } = renderTopSite({ link });
    expect(container.querySelector(".top-site-outer")).toBeInTheDocument();
  });

  it("should render a shortened title based off the url", () => {
    link.url = "https://www.foobar.org";
    link.hostname = "foobar";
    link.eTLD = "org";
    const { container } = renderTopSite({ link });

    expect(container.querySelector(".title-label").textContent).toBe("foobar");
  });

  it("should parse args for fluent correctly", () => {
    const title = '"fluent"';
    link.hostname = title;

    const { container } = renderTopSite({ link });
    const button = container.querySelector(
      "button[data-l10n-id='newtab-menu-content-tooltip']"
    );
    expect(button).toHaveAttribute("data-l10n-args", JSON.stringify({ title }));
  });

  it("should have .active class, on top-site-outer if context menu is open", () => {
    const { container } = renderTopSite({ link, index: 1, activeIndex: 1 });
    expect(container.querySelector(".top-site-outer")).toHaveClass("active");
  });
  it("should not add .active class, on top-site-outer if context menu is closed", () => {
    const { container } = renderTopSite({ link, index: 1 });
    expect(container.querySelector(".top-site-outer")).not.toHaveClass(
      "active"
    );
  });
  it("should render a plain button menu trigger alongside a panel-list", () => {
    const { container } = renderTopSite({ link, index: 0 });
    const button = container.querySelector("button.context-menu-button");
    expect(button).toBeInTheDocument();
    expect(container.querySelector("panel-list")).toBeInTheDocument();
    expect(button).toHaveAttribute("aria-haspopup", "menu");
    // Bug 2062844: a plain button rather than moz-button, so the tile can tell
    // keyboard focus from pointer focus with :focus-visible.
    expect(container.querySelector("moz-button")).not.toBeInTheDocument();
  });
  it("should toggle the panel-list from the menu button", () => {
    const ref = React.createRef();
    const { container } = renderTopSite({ link, index: 0 }, ref);
    const toggle = jest.fn();
    ref.current.panelListRef.current = { toggle };
    const button = container.querySelector("button.context-menu-button");

    // Opens on mousedown, not click: panel-list hides on any document mousedown
    // outside the panel, so a click-based toggle would reopen it immediately.
    fireEvent.mouseDown(button, { button: 0 });
    expect(toggle).toHaveBeenCalledTimes(1);

    // A non-primary button should not open it.
    fireEvent.mouseDown(button, { button: 2 });
    expect(toggle).toHaveBeenCalledTimes(1);

    // Keyboard activation passes the event so panel-list can tell it apart from
    // a pointer open and focus the first item.
    fireEvent.keyDown(button, { key: "Enter" });
    expect(toggle).toHaveBeenCalledTimes(2);
    expect(toggle.mock.calls[1][0]).toMatchObject({ type: "keydown" });
  });
  it("should register the menu button as the panel-list's popover invoker", () => {
    const ref = React.createRef();
    renderTopSite({ link, index: 0 }, ref);

    // panel-list is a popover="auto". Without the invoker relationship the
    // platform light-dismisses it on pointerup, immediately after our mousedown
    // opened it, because a click on the trigger counts as a click outside the
    // popover. This cannot be covered by a synthesizeMouse test: light dismiss
    // only runs for trusted pointer events.
    expect(ref.current.menuButtonRef.current.popoverTargetElement).toBe(
      ref.current.panelListRef.current
    );
  });
  it("should open the menu from a synthetic click", () => {
    const ref = React.createRef();
    const { container } = renderTopSite({ link, index: 0 }, ref);
    const toggle = jest.fn();
    ref.current.panelListRef.current = { toggle };
    const button = container.querySelector("button.context-menu-button");

    // A programmatic .click() produces no mousedown and carries detail 0. Tests
    // and callers rely on it, so it has to open the menu.
    fireEvent.click(button, { detail: 0 });
    expect(toggle).toHaveBeenCalledTimes(1);

    // A real mouse click was already handled on mousedown, so the click that
    // follows it must not toggle the menu back closed.
    fireEvent.click(button, { detail: 1 });
    expect(toggle).toHaveBeenCalledTimes(1);
  });
  it("should render the menu options via PanelListItems", () => {
    const ref = React.createRef();
    renderTopSite({ link, index: 0 }, ref);
    const itemsProps = findElementByType(
      ref.current.render(),
      PanelListItems
    ).props;
    expect(Array.isArray(itemsProps.options)).toBe(true);
    expect(itemsProps.options.length).toBeGreaterThanOrEqual(1);
  });
  it("should render the correct menu options for an organic top site", () => {
    const { container } = renderTopSite({ link, index: 0 });
    // TOP_SITES_CONTEXT_MENU_OPTIONS has two Separators, rendered as <hr>.
    expect(container.querySelectorAll("hr")).toHaveLength(2);
    expect(
      container.querySelectorAll("panel-item").length
    ).toBeGreaterThanOrEqual(1);
  });
  it("should record impressions for visible organic Top Sites", () => {
    const dispatch = jest.fn();
    renderTopSite({
      link,
      index: 3,
      dispatch,
      IntersectionObserver: buildIntersectionObserver([
        {
          isIntersecting: true,
          intersectionRatio: INTERSECTION_RATIO,
        },
      ]),
      document: {
        visibilityState: "visible",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
    });

    expect(dispatch).toHaveBeenCalledTimes(1);

    const [[action]] = dispatch.mock.calls;
    expect(action.type).toBe(at.TOP_SITES_ORGANIC_IMPRESSION_STATS);

    expect(action.data.type).toBe("impression");
    expect(action.data.source).toBe("newtab");
    expect(action.data.position).toBe(3);
  });
  it("should record impressions for visible sponsored Top Sites", () => {
    const dispatch = jest.fn();
    renderTopSite({
      link: Object.assign({}, link, {
        sponsored_position: 2,
        sponsored_tile_id: 12345,
        sponsored_impression_url: "https://impression.example.com/",
      }),
      index: 3,
      dispatch,
      IntersectionObserver: buildIntersectionObserver([
        {
          isIntersecting: true,
          intersectionRatio: INTERSECTION_RATIO,
        },
      ]),
      document: {
        visibilityState: "visible",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
    });

    expect(dispatch).toHaveBeenCalledTimes(1);

    const [[action]] = dispatch.mock.calls;
    expect(action.type).toBe(at.TOP_SITES_SPONSORED_IMPRESSION_STATS);

    expect(action.data.type).toBe("impression");
    expect(action.data.tile_id).toBe(12345);
    expect(action.data.source).toBe("newtab");
    expect(action.data.position).toBe(3);
    expect(action.data.reporting_url).toBe("https://impression.example.com/");
    expect(action.data.advertiser_name).toBe("foo");
  });

  describe("#onLinkClick", () => {
    function clickLink(props) {
      const dispatch = jest.fn();
      const { container } = renderTopSite({ ...props, dispatch });
      // Ignore any impression pings fired while mounting; only the click matters.
      dispatch.mockClear();
      fireEvent.click(container.querySelector("a.top-site-button"));
      return dispatch;
    }

    it("should call dispatch when the link is clicked", () => {
      const dispatch = clickLink({ link, index: 3 });

      const [[userEvent]] = dispatch.mock.calls;
      expect(userEvent.type).toBe(at.TELEMETRY_USER_EVENT);
      expect(userEvent.data.event).toBe("CLICK");
      expect(userEvent.data.source).toBe("TOP_SITES");
      expect(userEvent.data.action_position).toBe(3);

      const [, [openLink]] = dispatch.mock.calls;
      expect(openLink.type).toBe(at.OPEN_LINK);

      // Organic Top Site click event.
      const [, , [organicClick]] = dispatch.mock.calls;
      expect(organicClick.type).toBe(at.TOP_SITES_ORGANIC_IMPRESSION_STATS);
      expect(organicClick.data.type).toBe("click");
      expect(organicClick.data.source).toBe("newtab");
      expect(organicClick.data.position).toBe(3);
    });
    it("should dispatch a UserEventAction with the right data", () => {
      const dispatch = clickLink({
        link: Object.assign({}, link, {
          iconType: "rich_icon",
          isPinned: true,
        }),
        index: 3,
      });

      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.TELEMETRY_USER_EVENT);
      expect(action.data.event).toBe("CLICK");
      expect(action.data.source).toBe("TOP_SITES");
      expect(action.data.action_position).toBe(3);
      expect(action.data.value.card_type).toBe("pinned");
      expect(action.data.value.icon_type).toBe("rich_icon");
    });
    it("should dispatch a UserEventAction with the right data for search top site", () => {
      const siteInfo = {
        iconType: "tippytop",
        isPinned: true,
        searchTopSite: true,
        hostname: "google",
        label: "@google",
      };
      const dispatch = clickLink({
        link: Object.assign({}, link, siteInfo),
        index: 3,
      });

      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.TELEMETRY_USER_EVENT);
      expect(action.data.event).toBe("CLICK");
      expect(action.data.source).toBe("TOP_SITES");
      expect(action.data.action_position).toBe(3);
      expect(action.data.value.card_type).toBe("search");
      expect(action.data.value.icon_type).toBe("tippytop");
      expect(action.data.value.search_vendor).toBe("google");
    });
    it("should dispatch a UserEventAction with the right data for SPOC top site", () => {
      const siteInfo = {
        id: 1,
        iconType: "custom_screenshot",
        type: "SPOC",
        pos: 1,
        label: "test advertiser",
        shim: { click: "shim_click_id" },
      };
      const dispatch = clickLink({
        link: Object.assign({}, link, siteInfo),
        index: 0,
      });

      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.TELEMETRY_USER_EVENT);
      expect(action.data.event).toBe("CLICK");
      expect(action.data.source).toBe("TOP_SITES");
      expect(action.data.action_position).toBe(0);
      expect(action.data.value.card_type).toBe("spoc");
      expect(action.data.value.icon_type).toBe("custom_screenshot");

      // Pocket SPOC click event.
      const [, , [pocketClick]] = dispatch.mock.calls;
      expect(pocketClick.type).toBe(at.TELEMETRY_IMPRESSION_STATS);
      expect(pocketClick.data.click).toBe(0);
      expect(pocketClick.data.source).toBe("TOP_SITES");

      const [, , , [dsUserEvent]] = dispatch.mock.calls;
      expect(dsUserEvent.type).toBe(at.DISCOVERY_STREAM_USER_EVENT);
      expect(dsUserEvent.data.event).toBe("CLICK");
      expect(dsUserEvent.data.action_position).toBe(1);
      expect(dsUserEvent.data.source).toBe("TOP_SITES");
      expect(dsUserEvent.data.value.card_type).toBe("spoc");
      expect(dsUserEvent.data.value.tile_id).toBe(1);
      expect(dsUserEvent.data.value.shim).toBe("shim_click_id");

      // Topsite SPOC click event.
      const [, , , , [sponsoredClick]] = dispatch.mock.calls;
      expect(sponsoredClick.type).toBe(at.TOP_SITES_SPONSORED_IMPRESSION_STATS);
      expect(sponsoredClick.data.type).toBe("click");
      expect(sponsoredClick.data.tile_id).toBe(1);
      expect(sponsoredClick.data.source).toBe("newtab");
      expect(sponsoredClick.data.position).toBe(1);
      expect(sponsoredClick.data.advertiser_name).toBe("test advertiser");
    });
    it("should dispatch OPEN_LINK with the right data", () => {
      const dispatch = clickLink({
        link: Object.assign({}, link, { typedBonus: true }),
        index: 3,
      });

      const [, [action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.OPEN_LINK);
      expect(action.data.typedBonus).toBe(true);
    });
    it("should label an organic tile's OPEN_LINK for dwell time", () => {
      const dispatch = clickLink({ link, index: 3 });

      const [, [action]] = dispatch.mock.calls;
      expect(action.data).toHaveProperty("dwell_label", "topsite_organic");
      expect(action.data).toHaveProperty("is_sponsored", false);
    });
    it("should label a sponsored tile's OPEN_LINK for dwell time", () => {
      const dispatch = clickLink({
        link: Object.assign({}, link, {
          sponsored_position: 1,
          sponsored_tile_id: 12345,
        }),
        index: 3,
      });

      const [, [action]] = dispatch.mock.calls;
      expect(action.data).toHaveProperty("dwell_label", "topsite_sponsored");
      expect(action.data).toHaveProperty("is_sponsored", true);
    });
    it("should label a SPOC tile's OPEN_LINK for dwell time", () => {
      const dispatch = clickLink({
        link: Object.assign({}, link, { type: "SPOC" }),
        index: 3,
      });

      const [, [action]] = dispatch.mock.calls;
      expect(action.data).toHaveProperty("dwell_label", "topsite_sponsored");
      // A Pocket-sponsored top site sets no sponsored_tile_id, which is why
      // the label cannot be derived from is_sponsored.
      expect(action.data).toHaveProperty("is_sponsored", false);
    });
  });
});

describe("buildTopSitesList Add button placement", () => {
  function getSites(rows, { rowsCount = 1, perRow = 8 } = {}) {
    return buildTopSitesList(rows, rowsCount, perRow);
  }

  function addButtonIndex(sites) {
    return sites.findIndex(site => site?.isAddButton);
  }

  it("places the Add button in the first slot when there are no shortcuts", () => {
    expect(addButtonIndex(getSites([]))).toBe(0);
  });

  it("keeps the Add button visible after the first shortcut is added", () => {
    // Regression for Bug 2046956: starting from zero shortcuts and adding one
    // used to default the Add button to the last slot, where pinning the new
    // shortcut pushed the button out of bounds and hid it.
    expect(addButtonIndex(getSites([DEFAULT_LINK]))).toBe(1);
  });

  it("places the Add button right after the last shortcut", () => {
    expect(
      addButtonIndex(getSites([DEFAULT_LINK, DEFAULT_LINK, DEFAULT_LINK]))
    ).toBe(3);
  });

  it("keeps the Add button when the grid is full but can still grow a row", () => {
    expect(
      addButtonIndex(getSites(makeRows(8), { rowsCount: 1, perRow: 8 }))
    ).toBe(8);
  });

  it("drops the Add button when the grid is full at the max rows", () => {
    expect(
      addButtonIndex(getSites(makeRows(32), { rowsCount: 4, perRow: 8 }))
    ).toBe(-1);
  });
});

describe("<TopSiteAddButton>", () => {
  it("renders an in-grid tile with a large button", () => {
    const { container } = render(
      <TopSiteAddButton inGrid={true} index={3} dispatch={jest.fn()} />
    );
    const tile = container.querySelector("li.add-button-tile");
    expect(tile).toBeInTheDocument();
    const button = tile.querySelector("moz-button.add-button");
    expect(button).toBeInTheDocument();
    expect(button.getAttribute("size")).toBe("large");
  });

  it("renders a hover overlay (default size) when not in-grid", () => {
    const { container } = render(
      <TopSiteAddButton index={8} dispatch={jest.fn()} />
    );
    const overlay = container.querySelector("div.add-button-hidden");
    expect(overlay).toBeInTheDocument();
    expect(
      container.querySelector("li.add-button-tile")
    ).not.toBeInTheDocument();
    const button = overlay.querySelector("moz-button.add-button");
    expect(button).toBeInTheDocument();
    expect(button.hasAttribute("size")).toBe(false);
  });

  it("carries the slot's responsive hide class on the in-grid tile", () => {
    const { container } = render(
      <TopSiteAddButton
        inGrid={true}
        className="hide-for-small"
        index={3}
        dispatch={jest.fn()}
      />
    );
    expect(
      container.querySelector("li.add-button-tile.hide-for-small")
    ).toBeInTheDocument();
  });

  it("dispatches TOP_SITES_EDIT with its index when clicked", () => {
    const dispatch = jest.fn();
    const { container } = render(
      <TopSiteAddButton inGrid={true} index={7} dispatch={dispatch} />
    );
    fireEvent.click(container.querySelector("moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.TOP_SITES_EDIT,
      data: { index: 7 },
    });
  });
});

describe("<TopSiteList> Add button rendering", () => {
  it("renders the add button in-grid when the row isn't full", () => {
    const { container } = renderList(buildTopSitesList(makeRows(2), 1, 8));
    expect(container.querySelector(".add-button-tile")).toBeInTheDocument();
    expect(
      container.querySelector(".add-button-hidden")
    ).not.toBeInTheDocument();
  });

  it("renders the add button as a hover overlay when the row is full", () => {
    const { container } = renderList(buildTopSitesList(makeRows(8), 1, 8));
    expect(container.querySelector(".add-button-hidden")).toBeInTheDocument();
    expect(container.querySelector(".add-button-tile")).not.toBeInTheDocument();
  });

  // Bug 2084671: When 2 rows are enabled, at 6 columns, the in-grid tile wraps to row 2
  // and relies on .add-button-tile's min-height to reserve that row's height.
  it("renders the in-grid add button and wraps when 2 rows are enabled and 6 shortcuts fill a narrow row", () => {
    const { container } = renderList(buildTopSitesList(makeRows(6), 2, 8), {
      TopSitesRows: 2,
      Prefs: { values: { "nova.enabled": true } },
    });
    const addTile = container.querySelector(
      ".top-sites-list > li.add-button-tile"
    );
    expect(addTile).toBeInTheDocument();
    expect(addTile).not.toHaveClass("nova-hide-for-xs");
    expect(
      container.querySelector(".add-button-hidden")
    ).not.toBeInTheDocument();
  });
});

describe("<TopSiteList> arrow-key navigation", () => {
  // The flat focus order onKeyDown walks: each tile's link plus either
  // add-button variant (both carry .add-button), in DOM order.
  function focusTargetsFor(sites) {
    const { container } = renderList(sites);
    return [...container.querySelectorAll("a, .add-button")];
  }

  it("moves focus to the next tile on ArrowRight", () => {
    const targets = focusTargetsFor(buildTopSitesList(makeRows(3), 1, 8));
    act(() => targets[0].focus());
    fireEvent.keyDown(targets[0], { key: "ArrowRight" });
    expect(document.activeElement).toBe(targets[1]);
  });

  it("moves focus to the previous tile on ArrowLeft", () => {
    const targets = focusTargetsFor(buildTopSitesList(makeRows(3), 1, 8));
    act(() => targets[1].focus());
    fireEvent.keyDown(targets[1], { key: "ArrowLeft" });
    expect(document.activeElement).toBe(targets[0]);
  });

  it("steps onto the in-grid add button from the last tile", () => {
    // 2 tiles + a free slot: the add button renders in-grid as the last target.
    const targets = focusTargetsFor(buildTopSitesList(makeRows(2), 1, 8));
    const addButton = targets.at(-1);
    expect(addButton).toHaveClass("add-button");

    act(() => targets[1].focus());
    fireEvent.keyDown(targets[1], { key: "ArrowRight" });
    expect(document.activeElement).toBe(addButton);
  });

  it("steps onto the full-row overlay add button and back", () => {
    // Full row: the add button renders as the nested overlay, still the last
    // target — reached and left the same way as the in-grid variant.
    const targets = focusTargetsFor(buildTopSitesList(makeRows(8), 1, 8));
    const overlay = targets.at(-1);
    const lastTile = targets.at(-2);
    expect(overlay).toHaveClass("add-button");

    act(() => lastTile.focus());
    fireEvent.keyDown(lastTile, { key: "ArrowRight" });
    expect(document.activeElement).toBe(overlay);

    fireEvent.keyDown(overlay, { key: "ArrowLeft" });
    expect(document.activeElement).toBe(lastTile);
  });

  it("wraps from the last focus target to the first and back", () => {
    const targets = focusTargetsFor(buildTopSitesList(makeRows(3), 1, 8));
    const [first] = targets;
    const last = targets.at(-1);

    act(() => last.focus());
    fireEvent.keyDown(last, { key: "ArrowRight" });
    expect(document.activeElement).toBe(first);

    fireEvent.keyDown(first, { key: "ArrowLeft" });
    expect(document.activeElement).toBe(last);
  });
});

describe("TopSiteLink #_allowDrop with grouped pins", () => {
  const topsiteDrag = { dataTransfer: { types: ["text/topsite-index"] } };
  const makeLink = link => new TopSiteLink({ groupedPinsEnabled: true, link });

  it("allows dropping on a pinned tile", () => {
    expect(makeLink({ isPinned: true })._allowDrop(topsiteDrag)).toBe(true);
  });

  it("rejects dropping on a frecent (non-pinned) tile", () => {
    expect(makeLink({ isPinned: false })._allowDrop(topsiteDrag)).toBe(false);
  });

  it("stays rejected on a frecent tile even with a stale dragged flag", () => {
    // Regression: `dragged` is only reset on the next mousedown, so a cancelled
    // drag used to leave it true here and wrongly mark this slot droppable.
    const link = makeLink({ isPinned: false });
    link.dragged = true;
    expect(link._allowDrop(topsiteDrag)).toBe(false);
  });
});

describe("<TopSiteList>", () => {
  const APP = { isForStartupCache: { App: false } };

  it("should render a TopSiteList element", () => {
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList {...DEFAULT_PROPS} sites={[]} App={APP} />
      </WrapWithProvider>
    );
    expect(container.querySelector(".top-sites-list")).toBeInTheDocument();
  });
  it("should render a TopSite for each link with the right url", () => {
    const rows = [{ url: "https://foo.com" }, { url: "https://bar.com" }];
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList {...DEFAULT_PROPS} sites={rows} App={APP} />
      </WrapWithProvider>
    );
    const links = container.querySelectorAll("a.top-site-button[href]");
    expect(links).toHaveLength(2);
    rows.forEach((row, i) => expect(links[i]).toHaveAttribute("href", row.url));
  });
  it("should render the sliced list it is given", () => {
    const rows = [];
    for (
      let i = 0;
      i < TOP_SITES_DEFAULT_ROWS * TOP_SITES_MAX_SITES_PER_ROW + 3;
      i++
    ) {
      rows.push({ url: `https://foo${i}.com` });
    }
    const sites = buildTopSitesList(
      rows,
      TOP_SITES_DEFAULT_ROWS,
      TOP_SITES_MAX_SITES_PER_ROW
    );
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList
          {...DEFAULT_PROPS}
          sites={sites}
          TopSitesRows={TOP_SITES_DEFAULT_ROWS}
          App={APP}
        />
      </WrapWithProvider>
    );
    expect(container.querySelectorAll("a.top-site-button[href]")).toHaveLength(
      TOP_SITES_DEFAULT_ROWS * TOP_SITES_MAX_SITES_PER_ROW
    );
  });
  it("should render an Add button when the list includes one", () => {
    const rows = [{ url: "https://foo.com" }, { url: "https://bar.com" }];
    const sites = buildTopSitesList(rows, 1, TOP_SITES_MAX_SITES_PER_ROW);
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList
          {...DEFAULT_PROPS}
          sites={sites}
          TopSitesRows={1}
          App={APP}
        />
      </WrapWithProvider>
    );
    expect(container.querySelectorAll("a.top-site-button[href]")).toHaveLength(
      2
    );
    expect(container.querySelectorAll(".add-button")).toHaveLength(1);
  });
  it("should fill sponsored top sites with placeholders while rendering for startup cache", () => {
    const rows = [
      { url: "https://sponsored01.com", sponsored_position: 1 },
      { url: "https://sponsored02.com", sponsored_position: 2 },
      { url: "https://sponsored03.com", type: "SPOC" },
      { url: "https://foo.com" },
      { url: "https://bar.com" },
    ];
    const sites = buildTopSitesList(rows, 1, TOP_SITES_MAX_SITES_PER_ROW);
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList
          {...DEFAULT_PROPS}
          sites={sites}
          TopSitesRows={1}
          App={{ isForStartupCache: { TopSites: true } }}
        />
      </WrapWithProvider>
    );
    expect(container.querySelectorAll("a.top-site-button[href]")).toHaveLength(
      2
    );
    expect(container.querySelectorAll(".placeholder")).toHaveLength(3);
  });
  it("should add a className hide-for-narrow to sites after 6/row", () => {
    const rows = [];
    for (let i = 0; i < TOP_SITES_MAX_SITES_PER_ROW; i++) {
      rows.push({ url: `https://foo${i}.com` });
    }
    const sites = buildTopSitesList(rows, 1, TOP_SITES_MAX_SITES_PER_ROW);
    const { container } = render(
      <WrapWithProvider>
        <TopSiteList
          {...DEFAULT_PROPS}
          sites={sites}
          TopSitesRows={1}
          App={APP}
        />
      </WrapWithProvider>
    );
    expect(container.querySelectorAll("li.hide-for-narrow")).toHaveLength(2);
  });

  describe("Keyboard navigation", () => {
    let listRef;
    let instance;
    let focusTargets;
    let rerender;
    let rows;

    beforeEach(() => {
      rows = [
        { url: "https://foo.com" },
        { url: "https://bar.com" },
        { url: "https://baz.com" },
      ];
      listRef = React.createRef();
      ({ rerender } = render(
        <WrapWithProvider>
          <TopSiteList
            {...DEFAULT_PROPS}
            sites={rows}
            App={APP}
            ref={listRef}
          />
        </WrapWithProvider>
      ));
      instance = listRef.current;

      // The flat focus order onKeyDown walks: tile links and the add button
      // (both matched by "a, .add-button") in DOM order.
      focusTargets = [
        { focus: jest.fn(), tabIndex: -1 },
        { focus: jest.fn(), tabIndex: -1 },
        { focus: jest.fn(), tabIndex: -1 },
      ];
      instance.focusRef = { querySelectorAll: () => focusTargets };
    });

    it("should navigate to the next focus target with ArrowRight", () => {
      instance.onKeyDown({ key: "ArrowRight", target: focusTargets[0] });

      expect(focusTargets[1].focus).toHaveBeenCalledTimes(1);
      expect(focusTargets[1].tabIndex).toBe(0);
    });

    it("should navigate to the previous focus target with ArrowLeft", () => {
      instance.onKeyDown({ key: "ArrowLeft", target: focusTargets[1] });

      expect(focusTargets[0].focus).toHaveBeenCalledTimes(1);
      expect(focusTargets[0].tabIndex).toBe(0);
    });

    it("should reset the roving focus index when the row count changes", () => {
      // A stale focusedIndex (from a now-removed row) would leave the row with
      // no tab stop; changing the row count re-homes it to the first tile.
      act(() => {
        instance.setState({ focusedIndex: 15 });
      });
      rerender(
        <WrapWithProvider>
          <TopSiteList
            {...DEFAULT_PROPS}
            sites={rows}
            App={APP}
            TopSitesRows={2}
            ref={listRef}
          />
        </WrapWithProvider>
      );
      expect(instance.state.focusedIndex).toBe(0);
    });
  });

  describe("OHTTP sponsored tile images", () => {
    const OHTTP_PREF = "unifiedAds.ohttp.enabled";
    const TILE_IMAGES_PREF = "unifiedAds.tiles.ohttpImages.enabled";

    // The ohttpSponsoredImagesEnabled prop value that gets passed to the tiles.
    function ohttpSponsoredImagesEnabled(values) {
      const tree = new TopSiteList({
        ...LIST_PROPS,
        Prefs: { values },
        sites: [DEFAULT_LINK],
      }).render();
      return findElementByType(tree, TopSite).props.ohttpSponsoredImagesEnabled;
    }

    it("is enabled by the local pref while MARS uses OHTTP", () => {
      expect(
        ohttpSponsoredImagesEnabled({
          [TILE_IMAGES_PREF]: true,
          [OHTTP_PREF]: true,
        })
      ).toBe(true);
    });

    it("is enabled by trainhopConfig while MARS uses OHTTP", () => {
      expect(
        ohttpSponsoredImagesEnabled({
          trainhopConfig: { ohttpSponsoredTileImages: { enabled: true } },
          [OHTTP_PREF]: true,
        })
      ).toBe(true);
    });

    it("is disabled when MARS does not use OHTTP", () => {
      expect(
        ohttpSponsoredImagesEnabled({
          [TILE_IMAGES_PREF]: true,
          trainhopConfig: { ohttpSponsoredTileImages: { enabled: true } },
          [OHTTP_PREF]: false,
        })
      ).toBe(false);
    });

    it("is disabled when neither the pref nor trainhopConfig enables it", () => {
      expect(ohttpSponsoredImagesEnabled({ [OHTTP_PREF]: true })).toBe(false);
    });
  });
});

describe("TopSiteAddButton", () => {
  it("should dispatch a TOP_SITES_EDIT action when the addbutton is clicked", () => {
    const dispatch = jest.fn();
    const { container } = render(
      <TopSiteAddButton dispatch={dispatch} index={7} />
    );

    fireEvent.click(container.querySelector("moz-button"));

    expect(dispatch).toHaveBeenCalledTimes(1);
    expect(dispatch).toHaveBeenCalledWith({
      type: at.TOP_SITES_EDIT,
      data: { index: 7 },
    });
  });

  it("renders an in-grid tile with a large button", () => {
    const { container } = render(
      <TopSiteAddButton inGrid={true} index={3} dispatch={jest.fn()} />
    );
    const button = container.querySelector("li.add-button-tile moz-button");
    expect(button).toBeInTheDocument();
    expect(button).toHaveAttribute("size", "large");
  });

  it("renders a hover overlay (default size) when not in-grid", () => {
    const { container } = render(
      <TopSiteAddButton index={8} dispatch={jest.fn()} />
    );
    const overlay = container.querySelector("div.add-button-hidden");
    expect(overlay).toBeInTheDocument();
    expect(
      container.querySelector("li.add-button-tile")
    ).not.toBeInTheDocument();
    expect(overlay.querySelector("moz-button").hasAttribute("size")).toBe(
      false
    );
  });
});

describe("<TopSitePlaceholder>", () => {
  it("should render a placeholder top-site tile", () => {
    const { container } = render(<TopSitePlaceholder />);
    expect(
      container.querySelector("li.top-site-outer.placeholder")
    ).toBeInTheDocument();
  });

  it("should merge props.className with the placeholder class", () => {
    const { container } = render(<TopSitePlaceholder className="foo" />);
    expect(
      container.querySelector("li.top-site-outer.placeholder.foo")
    ).toBeInTheDocument();
  });
});
