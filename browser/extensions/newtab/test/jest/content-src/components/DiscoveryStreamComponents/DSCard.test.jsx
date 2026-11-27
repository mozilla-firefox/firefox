/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render, act } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import {
  _DSCard as DSCard,
  DSCard as ConnectedDSCard,
  readTimeFromWordCount,
  DSSource,
  PlaceholderDSCard,
} from "content-src/components/DiscoveryStreamComponents/DSCard/DSCard";
import { SafeAnchor } from "content-src/components/DiscoveryStreamComponents/SafeAnchor/SafeAnchor";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import React from "react";

const DEFAULT_PROPS = {
  url: "https://example.com",
  title: "title",
  raw_image_src: "https://picsum.photos/200",
  icon_src: "https://picsum.photos/200",
  App: {
    isForStartupCache: false,
  },
  DiscoveryStream: INITIAL_STATE.DiscoveryStream,
  Prefs: INITIAL_STATE.Prefs,
};

describe("<DSCard>", () => {
  let wrapper;
  let dispatch;
  // The old Enzyme test reached into the component instance via
  // `wrapper.instance()`; there is no DOM equivalent for that, so we hold a
  // white-box ref to the unconnected _DSCard instance instead.
  let cardRef;
  let currentProps;

  // Equivalent of Enzyme's `wrapper.setProps`: merge new props and re-render the
  // same tree so the ref keeps pointing at the same instance.
  function setProps(newProps) {
    currentProps = { ...currentProps, ...newProps };
    wrapper.rerender(
      <WrapWithProvider>
        <DSCard dispatch={dispatch} ref={cardRef} {...currentProps} />
      </WrapWithProvider>
    );
  }

  beforeEach(() => {
    dispatch = jest.fn();
    cardRef = React.createRef();
    currentProps = { ...DEFAULT_PROPS };
    wrapper = render(
      <WrapWithProvider>
        <DSCard dispatch={dispatch} ref={cardRef} {...currentProps} />
      </WrapWithProvider>
    );
    act(() => {
      cardRef.current.setState({ isSeen: true });
    });
  });

  it("should render", () => {
    const { container } = wrapper;
    expect(container.querySelector(".ds-card")).toBeInTheDocument();
  });

  it("should render a SafeAnchor", () => {
    setProps({ url: "https://foo.com" });

    const { container } = wrapper;
    const anchor = container.querySelector("a.ds-card-link");
    expect(anchor).toBeInTheDocument();
    expect(anchor).toHaveAttribute("href", "https://foo.com");
  });

  it("should label an organic card link for dwell time", () => {
    const renderSpy = jest.spyOn(SafeAnchor.prototype, "render");
    act(() => cardRef.current.forceUpdate());
    expect(renderSpy.mock.contexts.at(-1).props).toHaveProperty(
      "dwellLabel",
      "story_organic"
    );
    renderSpy.mockRestore();
  });

  it("should label a sponsored card link for dwell time", () => {
    const renderSpy = jest.spyOn(SafeAnchor.prototype, "render");
    setProps({ flightId: "12345" });
    expect(renderSpy.mock.contexts.at(-1).props).toHaveProperty(
      "dwellLabel",
      "story_sponsored"
    );
    renderSpy.mockRestore();
  });

  it("should pair a menu Open in a New Window with its click for dwell time", () => {
    // DSLinkMenu is connected, so it dispatches through the store rather than
    // the dispatch prop.
    const store = createStore(combineReducers(reducers), INITIAL_STATE);
    const storeDispatch = jest.spyOn(store, "dispatch");
    const { container } = render(
      <Provider store={store}>
        <DSCard dispatch={dispatch} {...DEFAULT_PROPS} />
      </Provider>
    );
    const item = container
      .querySelector("[data-l10n-id='newtab-menu-open-new-window']")
      .closest("panel-item");
    storeDispatch.mockClear();
    act(() => item.click());

    const [[open], [click]] = storeDispatch.mock.calls;
    expect(open.data).toHaveProperty("dwell_label", "story_organic");
    expect(open.data.dwell_id).toBeDefined();
    expect(click.data.value.dwell_id).toBe(open.data.dwell_id);
  });

  it("should pass onLinkClick prop", () => {
    expect(typeof cardRef.current.onLinkClick).toBe("function");
  });

  it("should pass isSponsored=false when flightId is not provided", () => {
    const { container } = wrapper;
    expect(container.querySelector("a.ds-card-link")).toHaveAttribute(
      "data-is-sponsored-link",
      "false"
    );
  });

  it("should pass isSponsored=true when flightId is provided", () => {
    setProps({ flightId: "12345" });
    const { container } = wrapper;
    expect(container.querySelector("a.ds-card-link")).toHaveAttribute(
      "data-is-sponsored-link",
      "true"
    );
  });

  it("should render DSLinkMenu", () => {
    // Note: <DSLinkMenu> component moved from a direct child element of `.ds-card`. See Bug 1893936
    const { container } = wrapper;
    expect(container.querySelector(".context-menu-button")).toBeInTheDocument();
  });

  it("should start with no .active class", () => {
    const { container } = wrapper;
    expect(container.querySelector(".active")).not.toBeInTheDocument();
  });

  it("should render badges for pocket, bookmark when not a spoc element ", () => {
    cardRef = React.createRef();
    wrapper = render(
      <WrapWithProvider>
        <DSCard context_type="bookmark" {...DEFAULT_PROPS} ref={cardRef} />
      </WrapWithProvider>
    );
    act(() => {
      cardRef.current.setState({ isSeen: true });
    });

    const { container } = wrapper;
    expect(container.querySelector(".status-message")).toBeInTheDocument();
  });

  it("should render the Editors' Pick badge only when showEditorsPickBadge is set", () => {
    act(() => {
      cardRef.current.setState({ isSeen: true });
    });
    const badgeSelector = "moz-badge.ds-card-editors-pick-badge";
    expect(wrapper.container.querySelector(badgeSelector)).toBeNull();

    setProps({ showEditorsPickBadge: true });
    expect(wrapper.container.querySelector(badgeSelector)).toHaveAttribute(
      "data-l10n-id",
      "newtab-editors-pick-badge"
    );
  });

  it("should render Sponsored Context for a spoc element", () => {
    const sponsorContext = "Sponsored by Foo";
    cardRef = React.createRef();
    wrapper = render(
      <WrapWithProvider>
        <DSCard
          context_type="bookmark"
          context={sponsorContext}
          {...DEFAULT_PROPS}
          ref={cardRef}
        />
      </WrapWithProvider>
    );
    act(() => {
      cardRef.current.setState({ isSeen: true });
    });

    const { container } = wrapper;
    expect(container.querySelector(".status-message")).not.toBeInTheDocument();
    expect(container.querySelector(".story-sponsored-label")).toHaveTextContent(
      sponsorContext
    );
  });

  it("should render time to read", () => {
    const discoveryStream = {
      ...INITIAL_STATE.DiscoveryStream,
      readTime: true,
    };
    cardRef = React.createRef();
    wrapper = render(
      <WrapWithProvider>
        <DSCard
          time_to_read={4}
          {...DEFAULT_PROPS}
          DiscoveryStream={discoveryStream}
          Prefs={INITIAL_STATE.Prefs}
          ref={cardRef}
        />
      </WrapWithProvider>
    );
    act(() => {
      cardRef.current.setState({ isSeen: true });
    });

    const { container } = wrapper;
    const timeToRead = container.querySelector(
      ".time-to-read [data-l10n-id='newtab-label-source-read-time']"
    );
    expect(timeToRead).toBeInTheDocument();
    expect(timeToRead.getAttribute("data-l10n-args")).toContain(
      '"timeToRead":4'
    );
  });

  describe("doesLinkTopicMatchSelectedTopic", () => {
    it("should return 'not-set' when selectedTopics is not set", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        topic: "bar",
        selectedTopics: "",
        availableTopics: "foo, bar, baz, qux",
      });
      const matchesSelectedTopic =
        cardRef.current.doesLinkTopicMatchSelectedTopic();
      expect(matchesSelectedTopic).toEqual("not-set");
    });

    it("should return 'topic-not-selectable' when topic is not in availableTopics", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        topic: "qux",
        selectedTopics: "foo, bar, baz",
        availableTopics: "foo, bar, baz",
      });
      const matchesSelectedTopic =
        cardRef.current.doesLinkTopicMatchSelectedTopic();
      expect(matchesSelectedTopic).toEqual("topic-not-selectable");
    });

    it("should return 'true' when topic is in selectedTopics", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        topic: "qux",
        selectedTopics: "foo, bar, baz, qux",
        availableTopics: "foo, bar, baz, qux",
      });
      const matchesSelectedTopic =
        cardRef.current.doesLinkTopicMatchSelectedTopic();
      expect(matchesSelectedTopic).toEqual("true");
    });

    it("should return 'false' when topic is NOT in selectedTopics", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        topic: "qux",
        selectedTopics: "foo, bar, baz",
        availableTopics: "foo, bar, baz, qux",
      });
      const matchesSelectedTopic =
        cardRef.current.doesLinkTopicMatchSelectedTopic();
      expect(matchesSelectedTopic).toEqual("false");
    });
  });

  describe("onLinkClick", () => {
    let fakeWindow;

    beforeEach(() => {
      dispatch = jest.fn();
      fakeWindow = {
        requestIdleCallback: jest.fn().mockReturnValue(1),
        cancelIdleCallback: jest.fn(),
        innerWidth: 1000,
        innerHeight: 900,
      };
      cardRef = React.createRef();
      currentProps = { ...DEFAULT_PROPS, windowObj: fakeWindow };
      wrapper = render(
        <WrapWithProvider>
          <DSCard dispatch={dispatch} ref={cardRef} {...currentProps} />
        </WrapWithProvider>
      );
    });

    it("should call dispatch with the correct events", () => {
      setProps({ id: "fooidx", pos: 1, type: "foo" });

      jest
        .spyOn(cardRef.current, "doesLinkTopicMatchSelectedTopic")
        .mockReturnValue(undefined);

      cardRef.current.onLinkClick();

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(
        ac.DiscoveryStreamUserEvent({
          event: "CLICK",
          source: "FOO",
          action_position: 1,
          value: {
            event_source: "card",
            card_type: "organic",
            recommendation_id: undefined,
            tile_id: "fooidx",
            scheduled_corpus_item_id: undefined,
            corpus_item_id: undefined,
            recommended_at: undefined,
            received_rank: undefined,
            topic: undefined,
            features: undefined,
            matches_selected_topic: undefined,
            selected_topics: undefined,
            attribution: undefined,
            format: "medium-card",
          },
        })
      );
    });

    it("should send the click payload as an open intent", () => {
      setProps({ id: "fooidx", pos: 9, type: "foo" });
      jest
        .spyOn(cardRef.current, "doesLinkTopicMatchSelectedTopic")
        .mockReturnValue(undefined);

      cardRef.current.onLinkOpenIntent({
        currentTarget: { href: "https://example.com/" },
      });

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(
        ac.OnlyToMain({
          type: at.BROWSER_LINK_OPEN_INTENT,
          data: {
            url: "https://example.com/",
            userEvent: cardRef.current.getClickEventData(),
          },
        })
      );
      expect(dispatch.mock.calls[0][0].data.userEvent.action_position).toBe(9);
    });

    it("should set the right card_type on spocs", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        flightId: 12345,
        format: "spoc",
      });
      jest
        .spyOn(cardRef.current, "doesLinkTopicMatchSelectedTopic")
        .mockReturnValue(undefined);
      cardRef.current.onLinkClick();

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(
        ac.DiscoveryStreamUserEvent({
          event: "CLICK",
          source: "FOO",
          action_position: 1,
          value: {
            event_source: "card",
            card_type: "spoc",
            recommendation_id: undefined,
            tile_id: "fooidx",
            scheduled_corpus_item_id: undefined,
            corpus_item_id: undefined,
            recommended_at: undefined,
            received_rank: undefined,
            topic: undefined,
            features: undefined,
            matches_selected_topic: undefined,
            selected_topics: undefined,
            attribution: undefined,
            format: "spoc",
          },
        })
      );
    });

    it("should call dispatch with a shim", () => {
      setProps({
        id: "fooidx",
        pos: 1,
        type: "foo",
        shim: {
          click: "click shim",
        },
      });

      jest
        .spyOn(cardRef.current, "doesLinkTopicMatchSelectedTopic")
        .mockReturnValue(undefined);
      cardRef.current.onLinkClick();

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(
        ac.DiscoveryStreamUserEvent({
          event: "CLICK",
          source: "FOO",
          action_position: 1,
          value: {
            event_source: "card",
            card_type: "organic",
            recommendation_id: undefined,
            tile_id: "fooidx",
            shim: "click shim",
            scheduled_corpus_item_id: undefined,
            corpus_item_id: undefined,
            recommended_at: undefined,
            received_rank: undefined,
            topic: undefined,
            features: undefined,
            matches_selected_topic: undefined,
            selected_topics: undefined,
            attribution: undefined,
            format: "medium-card",
          },
        })
      );
    });
  });

  describe("DSCard with CTA", () => {
    beforeEach(() => {
      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard {...DEFAULT_PROPS} ref={cardRef} />
        </WrapWithProvider>
      );
      act(() => {
        cardRef.current.setState({ isSeen: true });
      });
    });

    it("should render Default Meta", () => {
      const { container } = wrapper;
      expect(container.querySelector(".meta")).toBeInTheDocument();
    });
  });

  describe("DSCard with Intersection Observer", () => {
    beforeEach(() => {
      cardRef = React.createRef();
      // requestIdleCallback that does NOT run its callback keeps the card in the
      // placeholder (unseen) state, matching the old shallow render.
      const idleWindowStub = {
        requestIdleCallback: jest.fn().mockReturnValue(1),
        cancelIdleCallback: jest.fn(),
      };
      wrapper = render(
        <WrapWithProvider>
          <DSCard {...DEFAULT_PROPS} windowObj={idleWindowStub} ref={cardRef} />
        </WrapWithProvider>
      );
    });

    it("should render card when seen", () => {
      const { container } = wrapper;
      expect(
        container.querySelector("div.ds-card.placeholder")
      ).toBeInTheDocument();

      cardRef.current.observer = {
        unobserve: jest.fn(),
      };
      cardRef.current.placeholderElement = "element";

      act(() => {
        cardRef.current.onSeen([
          {
            isIntersecting: true,
          },
        ]);
      });

      expect(cardRef.current.state.isSeen).toBe(true);
      expect(
        container.querySelector("div.ds-card.placeholder")
      ).not.toBeInTheDocument();
      expect(container.querySelector("a.ds-card-link")).toBeInTheDocument();
      expect(cardRef.current.observer.unobserve).toHaveBeenCalledTimes(1);
      expect(cardRef.current.observer.unobserve).toHaveBeenCalledWith(
        "element"
      );
    });

    it("should setup proper placeholder ref for isSeen", () => {
      cardRef.current.setPlaceholderRef("element");
      expect(cardRef.current.placeholderElement).toEqual("element");
    });

    it("should setup observer on componentDidMount", () => {
      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard {...DEFAULT_PROPS} ref={cardRef} />
        </WrapWithProvider>
      );

      expect(!!cardRef.current.observer).toBe(true);
    });
  });

  describe("DSCard with Idle Callback", () => {
    let windowStub = {
      requestIdleCallback: jest.fn().mockReturnValue(1),
      cancelIdleCallback: jest.fn(),
    };
    beforeEach(() => {
      // RTL auto-unmounts after each test (calling componentWillUnmount, which
      // hits cancelIdleCallback), so reset the shared stub before each render.
      windowStub.requestIdleCallback.mockClear();
      windowStub.cancelIdleCallback.mockClear();
      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard windowObj={windowStub} {...DEFAULT_PROPS} ref={cardRef} />
        </WrapWithProvider>
      );
    });

    it("should call requestIdleCallback on componentDidMount", () => {
      expect(windowStub.requestIdleCallback).toHaveBeenCalledTimes(1);
    });

    it("should call cancelIdleCallback on componentWillUnmount", () => {
      act(() => {
        cardRef.current.componentWillUnmount();
      });
      expect(windowStub.cancelIdleCallback).toHaveBeenCalledTimes(1);
    });
  });

  describe("DSCard when rendered for about:home startup cache", () => {
    beforeEach(() => {
      const props = {
        App: {
          isForStartupCache: {
            App: true,
          },
        },
        DiscoveryStream: INITIAL_STATE.DiscoveryStream,
        Prefs: INITIAL_STATE.Prefs,
      };
      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard {...props} ref={cardRef} />
        </WrapWithProvider>
      );
    });

    it("should be set as isSeen automatically", () => {
      expect(cardRef.current.state.isSeen).toBe(true);
    });
  });

  describe("DSCard menu open states", () => {
    let cardNode;
    let fakeDocument;
    let fakeWindow;

    beforeEach(() => {
      fakeDocument = { l10n: { translateFragment: jest.fn() } };
      fakeWindow = {
        document: fakeDocument,
        requestIdleCallback: jest.fn().mockReturnValue(1),
        cancelIdleCallback: jest.fn(),
      };
      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard {...DEFAULT_PROPS} windowObj={fakeWindow} ref={cardRef} />
        </WrapWithProvider>
      );
      act(() => {
        cardRef.current.setState({ isSeen: true });
      });
      cardNode = wrapper.container.querySelector(".ds-card");
    });

    it("Should remove active on Menu Update", () => {
      // Add active class name to DSCard wrapper
      // to simulate menu open state
      cardNode.classList.add("active");
      expect(cardNode.className).toContain("active");

      cardRef.current.onMenuUpdate(false);

      expect(cardNode.className).not.toContain("active");
    });

    it("Should add active on Menu Show", async () => {
      await cardRef.current.onMenuShow();
      expect(cardNode.className).toContain("active");
    });

    it("Should add last-item to support resized window", async () => {
      fakeWindow.scrollMaxX = 20;
      await cardRef.current.onMenuShow();
      expect(cardNode.className).toContain("last-item");
      expect(cardNode.className).toContain("active");
    });

    it("should remove .active and .last-item classes", () => {
      const remove = jest.fn();
      cardRef.current.contextMenuButtonHostElement = {
        classList: { remove },
      };
      cardRef.current.onMenuUpdate();
      expect(remove).toHaveBeenCalledTimes(1);
    });

    it("should add .active and .last-item classes", async () => {
      const add = jest.fn();
      cardRef.current.contextMenuButtonHostElement = {
        classList: { add },
      };
      await cardRef.current.onMenuShow();
      expect(add).toHaveBeenCalledTimes(1);
    });
  });

  describe("DSCard standard sizes", () => {
    it("should render grid with correct image sizes", async () => {
      const { container } = wrapper;
      const image = container.querySelector(
        ".img-wrapper picture.ds-image img"
      );
      // sizes[0] === { mediaMatcher: "default", width: 296, height: 160 }
      expect(image.getAttribute("sizes")).toContain("default 296px");
      expect(image).toHaveStyle({ width: "296px", height: "160px" });
    });
  });

  describe("DSCard medium rectangle format", () => {
    it("should pass an empty sizes array to the DSImage", async () => {
      setProps({ format: "rectangle" });
      const { container } = wrapper;
      const image = container.querySelector(
        ".img-wrapper picture.ds-image img"
      );
      // An empty sizes array means the image gets no explicit size dimensions.
      expect(image.style.width).toBe("");
      expect(image.style.height).toBe("");
    });
  });

  describe("OHTTP images", () => {
    function mountWithOptions({ prefs, props } = {}) {
      const prefsState = {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "discoverystream.sections.enabled": true,
          "unifiedAds.ohttp.enabled": true,
          ohttpImagesConfig: { enabled: true, includeTopStoriesSection: false },
          "discoverystream.merino-provider.ohttp.enabled": true,
          "discoverystream.sections.contextualAds.enabled": true,
          "discoverystream.sections.personalization.inferred.user.enabled": true,
          "discoverystream.sections.personalization.inferred.enabled": true,
          "discoverystream.publisherFavicon.enabled": true,
          ...prefs,
        },
      };

      cardRef = React.createRef();
      wrapper = render(
        <WrapWithProvider>
          <DSCard
            {...{
              ...DEFAULT_PROPS,
              sectionsCardImageSizes: {
                1: "medium",
                2: "medium",
                3: "medium",
                4: "medium",
              },
            }}
            {...props}
            Prefs={prefsState}
            ref={cardRef}
          />
        </WrapWithProvider>
      );
      return wrapper;
    }

    function setWrapperIsSeen() {
      act(() => {
        cardRef.current.setState({ isSeen: true });
      });
    }

    it("should set secureImage and faviconSrc for Merino", async () => {
      wrapper = mountWithOptions();
      setWrapperIsSeen();

      const { container } = wrapper;
      const images = container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);
      images.forEach(image => {
        expect(image.getAttribute("src")).toMatch(/^moz-cached-ohttp:\/\//);
      });

      const favicon = container.querySelector(".source-wrapper img");
      expect(favicon).toHaveAttribute(
        "src",
        `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(DEFAULT_PROPS.icon_src)}`
      );
    });

    it("should set secureImage for unified ads", async () => {
      wrapper = mountWithOptions({
        props: {
          flightId: "flightId",
        },
        prefs: {
          "unifiedAds.ohttp.enabled": false,
        },
      });
      setWrapperIsSeen();

      let images = wrapper.container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);
      images.forEach(image => {
        expect(image.getAttribute("src")).not.toMatch(/^moz-cached-ohttp:\/\//);
      });

      wrapper = mountWithOptions({
        props: {
          flightId: "flightId",
        },
        prefs: {
          "unifiedAds.ohttp.enabled": true,
        },
      });
      setWrapperIsSeen();

      images = wrapper.container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);
      images.forEach(image => {
        expect(image.getAttribute("src")).toMatch(/^moz-cached-ohttp:\/\//);
      });
    });

    it("should not set secureImage or icon_src for top stories", async () => {
      wrapper = mountWithOptions({
        props: {
          section: "top_stories_section",
        },
      });
      setWrapperIsSeen();

      let images = wrapper.container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);
      images.forEach(image => {
        expect(image.getAttribute("src")).not.toMatch(/^moz-cached-ohttp:\/\//);
      });

      let favicon = wrapper.container.querySelector(".source-wrapper img");
      expect(favicon).toHaveAttribute("src", DEFAULT_PROPS.icon_src);

      wrapper = mountWithOptions({
        props: {
          section: "top_stories_section",
        },
        prefs: {
          ohttpImagesConfig: { enabled: true, includeTopStoriesSection: true },
        },
      });
      setWrapperIsSeen();

      images = wrapper.container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);
      images.forEach(image => {
        expect(image.getAttribute("src")).toMatch(/^moz-cached-ohttp:\/\//);
      });

      favicon = wrapper.container.querySelector(".source-wrapper img");
      expect(favicon).toHaveAttribute(
        "src",
        `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(DEFAULT_PROPS.icon_src)}`
      );
    });

    it("should not be seen on idle callback", async () => {
      wrapper = mountWithOptions();
      cardRef.current.onIdleCallback();
      expect(cardRef.current.state.isSeen).toBe(false);
    });
  });

  describe("DSCard section images sizes", () => {
    it("should render sections with correct image sizes", async () => {
      const cardSizes = {
        small: {
          width: 110,
          height: 117,
        },
        medium: {
          width: 300,
          height: 160,
        },
        large: {
          width: 190,
          height: 250,
        },
      };

      const mediaMatcher = {
        1: "default",
        2: "(min-width: 724px)",
        3: "(min-width: 1122px)",
        4: "(min-width: 1390px)",
      };

      setProps({
        Prefs: {
          values: {
            "discoverystream.sections.enabled": true,
          },
        },
        sectionsCardImageSizes: {
          1: "medium",
          2: "large",
          3: "small",
          4: "large",
        },
      });
      const { container } = wrapper;
      const images = container.querySelectorAll(
        ".img-wrapper picture.ds-image img"
      );
      expect(images).toHaveLength(4);

      expect(images[0].getAttribute("sizes")).toContain(mediaMatcher["1"]);
      expect(images[0]).toHaveStyle({
        height: `${cardSizes.medium.height}px`,
        width: `${cardSizes.medium.width}px`,
      });

      expect(images[1].getAttribute("sizes")).toContain(mediaMatcher["2"]);
      expect(images[1]).toHaveStyle({
        height: `${cardSizes.large.height}px`,
        width: `${cardSizes.large.width}px`,
      });

      expect(images[2].getAttribute("sizes")).toContain(mediaMatcher["3"]);
      expect(images[2]).toHaveStyle({
        height: `${cardSizes.small.height}px`,
        width: `${cardSizes.small.width}px`,
      });

      expect(images[3].getAttribute("sizes")).toContain(mediaMatcher["4"]);
      expect(images[3]).toHaveStyle({
        height: `${cardSizes.large.height}px`,
        width: `${cardSizes.large.width}px`,
      });
    });
  });
});

describe("<PlaceholderDSCard> component", () => {
  it("should have placeholder prop", () => {
    const { container } = render(
      <WrapWithProvider>
        <PlaceholderDSCard />
      </WrapWithProvider>
    );
    expect(container.querySelector(".ds-card.placeholder")).toBeInTheDocument();
  });

  it("should contain placeholder div", () => {
    const ref = React.createRef();
    const { container } = render(
      <DSCard placeholder={true} {...DEFAULT_PROPS} ref={ref} />
    );
    act(() => {
      ref.current.setState({ isSeen: true });
    });
    expect(
      container.querySelector("div.ds-card.placeholder")
    ).toBeInTheDocument();
  });

  it("should not be clickable", () => {
    const ref = React.createRef();
    const { container } = render(
      <DSCard placeholder={true} {...DEFAULT_PROPS} ref={ref} />
    );
    act(() => {
      ref.current.setState({ isSeen: true });
    });
    expect(container.querySelector("a.ds-card-link")).not.toBeInTheDocument();
  });

  it("should not have context menu", () => {
    const ref = React.createRef();
    const { container } = render(
      <DSCard placeholder={true} {...DEFAULT_PROPS} ref={ref} />
    );
    act(() => {
      ref.current.setState({ isSeen: true });
    });
    expect(
      container.querySelector(".context-menu-button")
    ).not.toBeInTheDocument();
  });
});

describe("<DSSource> component", () => {
  it("should return a default source without compact", () => {
    const { container } = render(<DSSource source="Mozilla" />);
    expect(container.querySelector(".source")).toHaveTextContent("Mozilla");
  });
  it("should return a default source with compact without a sponsor or time to read", () => {
    const { container } = render(<DSSource compact={true} source="Mozilla" />);
    expect(container.querySelector(".source")).toHaveTextContent("Mozilla");
  });
  it("should return a SponsorLabel with compact and a sponsor", () => {
    const { container } = render(
      <DSSource newSponsoredLabel={true} sponsor="Mozilla" />
    );
    expect(
      container.querySelector(".story-sponsored-label")
    ).toBeInTheDocument();
  });
  it("should return a time to read with compact and without a sponsor but with a time to read", () => {
    const { container } = render(
      <DSSource compact={true} source="Mozilla" timeToRead="2000" />
    );

    expect(container.querySelector(".time-to-read")).toBeInTheDocument();

    // Weirdly, we can test for the pressence of fluent, because time to read needs to be translated.
    // This is also because we did a shallow render, that th contents of fluent would be empty anyway.
    expect(
      container.querySelector("[data-l10n-id='newtab-label-source-read-time']")
    ).toBeInTheDocument();
  });
  it("should prioritize a SponsorLabel if for some reason it gets everything", () => {
    const { container } = render(
      <DSSource
        newSponsoredLabel={true}
        sponsor="Mozilla"
        source="Mozilla"
        timeToRead="2000"
      />
    );
    expect(
      container.querySelector(".story-sponsored-label")
    ).toBeInTheDocument();
  });
});

describe("readTimeFromWordCount function", () => {
  it("should return proper read time", () => {
    const result = readTimeFromWordCount(2000);
    expect(result).toEqual(10);
  });
  it("should return false with falsey word count", () => {
    expect(readTimeFromWordCount()).toBe(false);
    expect(readTimeFromWordCount(0)).toBe(false);
    expect(readTimeFromWordCount("")).toBe(false);
    expect(readTimeFromWordCount(null)).toBe(false);
    expect(readTimeFromWordCount(undefined)).toBe(false);
  });
  it("should return NaN with invalid word count", () => {
    expect(readTimeFromWordCount("zero")).toBeNaN();
    expect(readTimeFromWordCount({})).toBeNaN();
  });
});

describe("<DSCard> placeholder and section images", () => {
  it("should render a placeholder when not yet seen", () => {
    const { container } = render(
      <WrapWithProvider>
        <ConnectedDSCard dispatch={jest.fn()} type="DISCOVERY_STREAM" />
      </WrapWithProvider>
    );
    expect(container.querySelector(".ds-card")).toBeInTheDocument();
  });

  describe("section card images", () => {
    // DSCard is connected, so the sections pref has to come from the store.
    const SECTIONS_STATE = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "discoverystream.sections.enabled": true,
        },
      },
    };

    // isForStartupCache marks the card seen on construction, which is what makes
    // the images render rather than the placeholder.
    const SEEN_PROPS = {
      App: { isForStartupCache: { App: true } },
      mayHaveSectionsCards: true,
      image_src: "image.jpg",
      url: "https://example.com",
      title: "Card",
    };

    const renderCard = sectionsCardImageSizes =>
      render(
        <WrapWithProvider state={SECTIONS_STATE}>
          <ConnectedDSCard
            dispatch={jest.fn()}
            {...SEEN_PROPS}
            sectionsCardImageSizes={sectionsCardImageSizes}
          />
        </WrapWithProvider>
      );

    it("renders one image variant per column the layout defines", () => {
      const { container } = renderCard({
        1: "medium",
        2: "medium",
        3: "medium",
        4: "medium",
        5: "large",
      });

      expect(container.querySelectorAll(".ds-image")).toHaveLength(5);
      expect(container.querySelector(".image-5")).toBeInTheDocument();
    });

    it("renders no fifth variant for a four-column layout", () => {
      const { container } = renderCard({
        1: "medium",
        2: "medium",
        3: "medium",
        4: "medium",
      });

      expect(container.querySelectorAll(".ds-image")).toHaveLength(4);
      expect(container.querySelector(".image-5")).not.toBeInTheDocument();
    });
  });
});
