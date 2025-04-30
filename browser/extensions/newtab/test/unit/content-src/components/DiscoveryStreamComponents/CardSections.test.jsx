import React from "react";
import { mount } from "enzyme";
import { Provider } from "react-redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { CardSections } from "content-src/components/DiscoveryStreamComponents/CardSections/CardSections";
import { combineReducers, createStore } from "redux";
import { DSCard } from "../../../../../content-src/components/DiscoveryStreamComponents/DSCard/DSCard";
const PREF_SECTIONS_PERSONALIZATION_ENABLED =
  "discoverystream.sections.personalization.enabled";

const DEFAULT_PROPS = {
  type: "CardGrid",
  firstVisibleTimeStamp: null,
  is_collection: true,
  spocMessageVariant: "",
  ctaButtonSponsors: [""],
  data: {
    sections: [
      {
        data: [
          {
            title: "Card 1",
            image_src: "image1.jpg",
            url: "http://example.com",
          },
          {},
          {},
          {},
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

// Wrap this around any component that uses useSelector,
// or any mount that uses a child that uses redux.
function WrapWithProvider({ children, state = INITIAL_STATE }) {
  let store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

describe("<CardSections />", () => {
  let wrapper;
  let sandbox;
  let dispatch;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    dispatch = sandbox.stub();
    wrapper = mount(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
  });

  afterEach(() => {
    sandbox.restore();
  });

  it("should render null if no data is provided", () => {
    // Verify the section exists normally, so the next assertion is unlikely to be a false positive.
    assert(wrapper.find(".ds-section-wrapper").exists());

    wrapper = mount(
      <WrapWithProvider>
        <CardSections dispatch={dispatch} {...DEFAULT_PROPS} data={null} />
      </WrapWithProvider>
    );
    assert(!wrapper.find(".ds-section-wrapper").exists());
  });

  it("should render DSEmptyState if sections are falsey", () => {
    wrapper = mount(
      <WrapWithProvider>
        <CardSections
          {...DEFAULT_PROPS}
          data={{ ...DEFAULT_PROPS.data, sections: [] }}
        />
      </WrapWithProvider>
    );
    assert(wrapper.find(".ds-card-grid.empty").exists());
  });

  it("should render sections and DSCard components for valid data", () => {
    const { sections } = DEFAULT_PROPS.data;
    const sectionLength = sections.length;
    assert.lengthOf(wrapper.find("section"), sectionLength);
    assert.lengthOf(wrapper.find(DSCard), 4);
    assert.equal(wrapper.find(".section-title").text(), "title");
  });

  it("should skip a section with no items available for that section", () => {
    // Verify the section exists normally, so the next assertion is unlikely to be a false positive.
    assert(wrapper.find(".ds-section").exists());

    wrapper = mount(
      <WrapWithProvider>
        <CardSections
          {...DEFAULT_PROPS}
          data={{
            ...DEFAULT_PROPS.data,
            sections: [{ ...DEFAULT_PROPS.data.sections[0], data: [] }],
          }}
        />
      </WrapWithProvider>
    );
    assert(!wrapper.find(".ds-section").exists());
  });

  it("should render a placeholder", () => {
    wrapper = mount(
      <WrapWithProvider>
        <CardSections
          {...DEFAULT_PROPS}
          data={{
            ...DEFAULT_PROPS.data,
            sections: [
              {
                ...DEFAULT_PROPS.data.sections[0],
                data: [{ placeholder: true }],
              },
            ],
          }}
        />
      </WrapWithProvider>
    );
    assert(wrapper.find(".ds-card.placeholder").exists());
  });

  it("should pass correct props to DSCard", () => {
    const cardProps = wrapper.find(DSCard).at(0).props();
    assert.equal(cardProps.title, "Card 1");
    assert.equal(cardProps.image_src, "image1.jpg");
    assert.equal(cardProps.url, "http://example.com");
  });

  it("should apply correct classNames and position from layout data", () => {
    const props = wrapper.find(DSCard).at(0).props();
    const thirdProps = wrapper.find(DSCard).at(2).props();
    assert.equal(
      props.sectionsClassNames,
      "col-1-large col-1-position-0 col-1-show-excerpt"
    );
    assert.equal(
      thirdProps.sectionsClassNames,
      "col-1-small col-1-position-1 col-1-hide-excerpt"
    );
  });

  it("should apply correct class names for cards with and without excerpts", () => {
    wrapper.find(DSCard).forEach(card => {
      const props = card.props();
      // Small cards don't show excerpts according to the data in DEFAULT_PROPS for this test suite
      if (props.sectionsClassNames.includes("small")) {
        assert.include(props.sectionsClassNames, "hide-excerpt");
        assert.notInclude(props.sectionsClassNames, "show-excerpt");
      }
      // The other cards should show excerpts though!
      else {
        assert.include(props.sectionsClassNames, "show-excerpt");
        assert.notInclude(props.sectionsClassNames, "hide-excerpt");
      }
    });
  });

  it("should dispatch SECTION_PERSONALIZATION_UPDATE updates with follow and unfollow", () => {
    const fakeDate = "2020-01-01T00:00:00.000Z";
    sandbox.useFakeTimers(new Date(fakeDate));
    const layout = {
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
    };
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

    wrapper = mount(
      <WrapWithProvider state={state}>
        <CardSections
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          data={{
            ...DEFAULT_PROPS.data,
            sections: [
              {
                data: [
                  {
                    title: "Card 1",
                    image_src: "image1.jpg",
                    url: "http://example.com",
                  },
                ],
                receivedRank: 0,
                sectionKey: "section_key_1",
                title: "title",
                layout,
              },
              {
                data: [
                  {
                    title: "Card 2",
                    image_src: "image2.jpg",
                    url: "http://example.com",
                  },
                ],
                receivedRank: 0,
                sectionKey: "section_key_2",
                title: "title",
                layout,
              },
            ],
          }}
        />
      </WrapWithProvider>
    );

    let button = wrapper.find(".section-follow moz-button").first();
    button.simulate("click", {});

    assert.deepEqual(dispatch.getCall(0).firstArg, {
      type: "SECTION_PERSONALIZATION_UPDATE",
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

    assert.calledWith(dispatch.getCall(1), {
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

    button = wrapper.find(".section-follow.following moz-button");
    button.simulate("click", {});

    assert.calledWith(dispatch.getCall(2), {
      type: "SECTION_PERSONALIZATION_UPDATE",
      data: {},
      meta: {
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
      },
    });

    assert.calledWith(dispatch.getCall(3), {
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
  });
});
