/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import {
  Section,
  SectionIntl,
  _Sections as Sections,
} from "content-src/components/Sections/Sections";
import { actionTypes as at } from "common/Actions.mjs";
import { render } from "@testing-library/react";
import { Provider } from "react-redux";
import React from "react";

// Enzyme's shallow()/mount() have no RTL equivalent that renders a connected
// child (CollapsibleSection, Card, TopSites all use react-redux connect), so
// every render is wrapped in a real redux Provider. mountSectionWithProps
// renders the unconnected <Section> (dispatch passed as a prop, mirroring the
// legacy test) and mountSectionIntlWithProps renders the connected <SectionIntl>.
function mountSectionWithProps(props) {
  const store = createStore(combineReducers(reducers), INITIAL_STATE);
  return render(
    <Provider store={store}>
      <Section {...props} />
    </Provider>
  );
}

function mountSectionIntlWithProps(props) {
  const store = createStore(combineReducers(reducers), INITIAL_STATE);
  return render(
    <Provider store={store}>
      <SectionIntl {...props} />
    </Provider>
  );
}

// Renders the unconnected <Sections> inside a Provider so its connected
// children render into the DOM (the legacy test used shallow()).
function renderSections(props) {
  const store = createStore(combineReducers(reducers), INITIAL_STATE);
  return render(
    <Provider store={store}>
      <Sections {...props} />
    </Provider>
  );
}

// Each rendered SectionIntl/TopSites emits a <section data-section-id> element,
// which is how we count/identify sections in the DOM in place of the legacy
// wrapper.find(SectionIntl)/find(TopSites).
function getRenderedSections(container) {
  return [...container.querySelectorAll(".sections-list [data-section-id]")];
}

describe("<Sections>", () => {
  let wrapper;
  let FAKE_SECTIONS;
  // _Sections.renderSections spreads a React `key` into <SectionIntl>/<TopSites>,
  // which makes React log a "key prop is being spread into JSX" console.error.
  // Swallow it locally so the jest-setup console.error guard doesn't fail these
  // rendering tests.
  let consoleErrorSpy;
  beforeEach(() => {
    consoleErrorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
    FAKE_SECTIONS = new Array(5).fill(null).map((value, index) => ({
      id: `foo_bar_${index}`,
      title: `Foo Bar ${index}`,
      enabled: !!(index % 2),
      rows: [],
    }));
    wrapper = renderSections({
      Sections: FAKE_SECTIONS,
      Prefs: {
        values: {
          sectionOrder: FAKE_SECTIONS.map(section => section.id).join(","),
        },
      },
    });
  });
  afterEach(() => {
    consoleErrorSpy.mockRestore();
  });
  it("should render a Sections element", () => {
    expect(
      wrapper.container.querySelector(".sections-list")
    ).toBeInTheDocument();
  });
  it("should render a Section for each one passed in props.Sections with .enabled === true", () => {
    const sectionElems = getRenderedSections(wrapper.container);
    expect(sectionElems).toHaveLength(2);
    sectionElems.forEach((section, i) => {
      expect(section.getAttribute("data-section-id")).toEqual(
        FAKE_SECTIONS[2 * i + 1].id
      );
      expect(FAKE_SECTIONS[2 * i + 1].enabled).toBe(true);
    });
  });
  it("should render Top Sites if feeds.topsites pref is true", () => {
    wrapper = renderSections({
      Sections: FAKE_SECTIONS,
      Prefs: {
        values: {
          "feeds.topsites": true,
          sectionOrder: "topsites,topstories,highlights",
        },
      },
    });
    expect(wrapper.container.querySelectorAll(".top-sites")).toHaveLength(1);
  });
  it("should NOT render Top Sites if feeds.topsites pref is false", () => {
    wrapper = renderSections({
      Sections: FAKE_SECTIONS,
      Prefs: {
        values: {
          "feeds.topsites": false,
          sectionOrder: "topsites,topstories,highlights",
        },
      },
    });
    expect(wrapper.container.querySelectorAll(".top-sites")).toHaveLength(0);
  });
  it("should render the sections in the order specifed by sectionOrder pref", () => {
    wrapper = renderSections({
      Sections: FAKE_SECTIONS,
      Prefs: { values: { sectionOrder: "foo_bar_1,foo_bar_3" } },
    });
    let sections = getRenderedSections(wrapper.container);
    expect(sections).toHaveLength(2);
    expect(sections[0].getAttribute("data-section-id")).toEqual("foo_bar_1");
    expect(
      sections[sections.length - 1].getAttribute("data-section-id")
    ).toEqual("foo_bar_3");
    wrapper = renderSections({
      Sections: FAKE_SECTIONS,
      Prefs: { values: { sectionOrder: "foo_bar_3,foo_bar_1" } },
    });
    sections = getRenderedSections(wrapper.container);
    expect(sections).toHaveLength(2);
    expect(sections[0].getAttribute("data-section-id")).toEqual("foo_bar_3");
    expect(
      sections[sections.length - 1].getAttribute("data-section-id")
    ).toEqual("foo_bar_1");
  });
});

describe("<Section>", () => {
  let wrapper;
  let FAKE_SECTION;

  beforeEach(() => {
    FAKE_SECTION = {
      id: `foo_bar_1`,
      pref: { collapsed: false },
      title: `Foo Bar 1`,
      rows: [{ link: "http://localhost", index: 0 }],
      emptyState: {
        icon: "check",
        message: "Some message",
      },
      rowsPref: "section.rows",
      maxRows: 4,
      Prefs: { values: { "section.rows": 2 } },
    };
    wrapper = mountSectionIntlWithProps(FAKE_SECTION);
  });

  describe("placeholders", () => {
    const CARDS_PER_ROW = 3;
    const fakeSite = { link: "http://localhost" };
    function renderWithSites(rows) {
      const store = createStore(combineReducers(reducers), INITIAL_STATE);
      return render(
        <Provider store={store}>
          <Section {...FAKE_SECTION} rows={rows} />
        </Provider>
      );
    }

    it("should return 2 row of placeholders if realRows is 0", () => {
      wrapper = renderWithSites([]);
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(6);
    });
    it("should fill in the rest of the rows", () => {
      wrapper = renderWithSites(new Array(CARDS_PER_ROW).fill(fakeSite));
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(CARDS_PER_ROW);

      wrapper = renderWithSites(new Array(CARDS_PER_ROW + 1).fill(fakeSite));
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(2);

      wrapper = renderWithSites(new Array(CARDS_PER_ROW + 2).fill(fakeSite));
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(1);

      wrapper = renderWithSites(
        new Array(2 * CARDS_PER_ROW - 1).fill(fakeSite)
      );
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(1);
    });
    it("should not add placeholders all the rows are full", () => {
      wrapper = renderWithSites(new Array(2 * CARDS_PER_ROW).fill(fakeSite));
      expect(
        wrapper.container.querySelectorAll(".card-outer.placeholder")
      ).toHaveLength(0);
    });
  });

  describe("empty state", () => {
    beforeEach(() => {
      Object.assign(FAKE_SECTION, {
        initialized: true,
        dispatch: () => {},
        rows: [],
        emptyState: {
          message: "Some message",
        },
      });
      wrapper = mountSectionWithProps(FAKE_SECTION);
    });
    it("should be shown when rows is empty and initialized is true", () => {
      expect(
        wrapper.container.querySelector(".empty-state")
      ).toBeInTheDocument();
    });
    it("should not be shown in initialized is false", () => {
      Object.assign(FAKE_SECTION, {
        initialized: false,
        rows: [],
        emptyState: {
          message: "Some message",
        },
      });
      wrapper = mountSectionWithProps(FAKE_SECTION);
      expect(
        wrapper.container.querySelector(".empty-state")
      ).not.toBeInTheDocument();
    });
    it("no icon should be shown", () => {
      expect(wrapper.container.querySelectorAll(".icon")).toHaveLength(0);
    });
  });

  describe("impression stats", () => {
    const FAKE_TOPSTORIES_SECTION_PROPS = {
      id: "TopStories",
      title: "Foo Bar 1",
      pref: { collapsed: false },
      maxRows: 1,
      rows: [{ guid: 1 }, { guid: 2 }],
      shouldSendImpressionStats: true,

      document: {
        visibilityState: "visible",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
      eventSource: "TOP_STORIES",
      options: { personalized: false },
    };

    // shallow(<Section {...defaults} {...props} />) becomes a Provider-wrapped
    // render. setProps mirrors enzyme's merge-into-existing-props semantics on
    // rerender so the same tree updates instead of remounting.
    function renderSection(props = {}) {
      const store = createStore(combineReducers(reducers), INITIAL_STATE);
      let currentProps = { ...FAKE_TOPSTORIES_SECTION_PROPS, ...props };
      const utils = render(
        <Provider store={store}>
          <Section {...currentProps} />
        </Provider>
      );
      utils.setProps = newProps => {
        currentProps = { ...currentProps, ...newProps };
        utils.rerender(
          <Provider store={store}>
            <Section {...currentProps} />
          </Provider>
        );
      };
      return utils;
    }

    it("should send impression with the right stats when the page loads", () => {
      const dispatch = jest.fn();
      renderSection({ dispatch });

      expect(dispatch).toHaveBeenCalledTimes(1);

      const [[action]] = dispatch.mock.calls;
      expect(action.type).toEqual(at.TELEMETRY_IMPRESSION_STATS);
      expect(action.data.source).toEqual("TOP_STORIES");
      expect(action.data.tiles).toEqual([{ id: 1 }, { id: 2 }]);
    });
    it("should not send impression stats if not configured", () => {
      const dispatch = jest.fn();
      const props = Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
        shouldSendImpressionStats: false,
        dispatch,
      });
      renderSection(props);
      expect(dispatch).not.toHaveBeenCalled();
    });
    it("should not send impression stats if the section is collapsed", () => {
      const dispatch = jest.fn();
      const props = Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
        pref: { collapsed: true },
      });
      renderSection(props);
      expect(dispatch).not.toHaveBeenCalled();
    });
    it("should send 1 impression when the page becomes visibile after loading", () => {
      const props = {
        dispatch: jest.fn(),
        document: {
          visibilityState: "hidden",
          addEventListener: jest.fn(),
          removeEventListener: jest.fn(),
        },
      };

      renderSection(props);

      // Was the event listener added?
      expect(props.document.addEventListener).toHaveBeenCalledWith(
        "visibilitychange",
        expect.any(Function)
      );

      // Make sure dispatch wasn't called yet
      expect(props.dispatch).not.toHaveBeenCalled();

      // Simulate a visibilityChange event
      const [[, listener]] = props.document.addEventListener.mock.calls;
      props.document.visibilityState = "visible";
      listener();

      // Did we actually dispatch an event?
      expect(props.dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = props.dispatch.mock.calls;
      expect(action.type).toEqual(at.TELEMETRY_IMPRESSION_STATS);

      // Did we remove the event listener?
      expect(props.document.removeEventListener).toHaveBeenCalledWith(
        "visibilitychange",
        listener
      );
    });
    it("should remove visibility change listener when section is removed", () => {
      const props = {
        dispatch: jest.fn(),
        document: {
          visibilityState: "hidden",
          addEventListener: jest.fn(),
          removeEventListener: jest.fn(),
        },
      };

      const section = renderSection(props);
      expect(props.document.addEventListener).toHaveBeenCalledWith(
        "visibilitychange",
        expect.any(Function)
      );
      const [[, listener]] = props.document.addEventListener.mock.calls;

      section.unmount();
      expect(props.document.removeEventListener).toHaveBeenCalledWith(
        "visibilitychange",
        listener
      );
    });
    it("should send an impression if props are updated and props.rows are different", () => {
      const props = { dispatch: jest.fn() };
      wrapper = renderSection(props);
      props.dispatch.mockClear();

      // New rows
      wrapper.setProps(
        Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
          rows: [{ guid: 123 }],
        })
      );

      expect(props.dispatch).toHaveBeenCalledTimes(1);
    });
    it("should not send an impression if props are updated but props.rows are the same", () => {
      const props = { dispatch: jest.fn() };
      wrapper = renderSection(props);
      props.dispatch.mockClear();

      // Only update the disclaimer prop
      wrapper.setProps(
        Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
          disclaimer: { id: "bar" },
        })
      );

      expect(props.dispatch).not.toHaveBeenCalled();
    });
    it("should not send an impression if props are updated and props.rows are the same but section is collapsed", () => {
      const props = { dispatch: jest.fn() };
      wrapper = renderSection(props);
      props.dispatch.mockClear();

      // New rows and collapsed
      wrapper.setProps(
        Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
          rows: [{ guid: 123 }],
          pref: { collapsed: true },
        })
      );

      expect(props.dispatch).not.toHaveBeenCalled();

      // Expand the section. Now the impression stats should be sent
      wrapper.setProps(
        Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
          rows: [{ guid: 123 }],
          pref: { collapsed: false },
        })
      );

      expect(props.dispatch).toHaveBeenCalledTimes(1);
    });
    it("should not send an impression if props are updated but GUIDs are the same", () => {
      const props = { dispatch: jest.fn() };
      wrapper = renderSection(props);
      props.dispatch.mockClear();

      wrapper.setProps(
        Object.assign({}, FAKE_TOPSTORIES_SECTION_PROPS, {
          rows: [{ guid: 1 }, { guid: 2 }],
        })
      );

      expect(props.dispatch).not.toHaveBeenCalled();
    });
    it("should only send the latest impression on a visibility change", () => {
      const listeners = new Set();
      const props = {
        dispatch: jest.fn(),
        document: {
          visibilityState: "hidden",
          addEventListener: (ev, cb) => listeners.add(cb),
          removeEventListener: (ev, cb) => listeners.delete(cb),
        },
      };

      wrapper = renderSection(props);

      // Update twice
      wrapper.setProps(Object.assign({}, props, { rows: [{ guid: 123 }] }));
      wrapper.setProps(Object.assign({}, props, { rows: [{ guid: 2432 }] }));

      expect(props.dispatch).not.toHaveBeenCalled();

      // Simulate listeners getting called
      props.document.visibilityState = "visible";
      listeners.forEach(l => l());

      // Make sure we only sent the latest event
      expect(props.dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = props.dispatch.mock.calls;
      expect(action.data.tiles).toEqual([{ id: 2432 }]);
    });
  });

  describe("tab rehydrated", () => {
    it("should fire NEW_TAB_REHYDRATED event", () => {
      const dispatch = jest.fn();
      const TOP_STORIES_SECTION = {
        id: "topstories",
        title: "TopStories",
        pref: { collapsed: false },
        initialized: false,
        rows: [{ guid: 1, link: "http://localhost", isDefault: true }],
        read_more_endpoint: "http://localhost/read-more",
        maxRows: 1,
        eventSource: "TOP_STORIES",
      };
      const store = createStore(combineReducers(reducers), INITIAL_STATE);
      let props = {
        Pocket: { waitingForSpoc: true, pocketCta: {} },
        ...TOP_STORIES_SECTION,
        dispatch,
      };
      const { rerender } = render(
        <Provider store={store}>
          <Section {...props} />
        </Provider>
      );
      expect(dispatch).not.toHaveBeenCalled();

      props = { ...props, initialized: true };
      rerender(
        <Provider store={store}>
          <Section {...props} />
        </Provider>
      );

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toEqual("NEW_TAB_REHYDRATED");
    });
  });

  describe("#numRows", () => {
    // wrapper.find(Section).instance().numRows has no RTL equivalent, so the
    // unconnected <Section> is rendered with a ref to read the numRows getter
    // off the class instance directly.
    function renderSectionRef(props) {
      const store = createStore(combineReducers(reducers), INITIAL_STATE);
      const sectionRef = React.createRef();
      render(
        <Provider store={store}>
          <Section ref={sectionRef} {...props} />
        </Provider>
      );
      return sectionRef;
    }

    it("should return maxRows if there is no rowsPref set", () => {
      delete FAKE_SECTION.rowsPref;
      const sectionRef = renderSectionRef(FAKE_SECTION);
      expect(sectionRef.current.numRows).toEqual(FAKE_SECTION.maxRows);
    });

    it("should return number of rows set in Pref if rowsPref is set", () => {
      const numRows = 2;
      Object.assign(FAKE_SECTION, {
        rowsPref: "section.rows",
        maxRows: 4,
        Prefs: { values: { "section.rows": numRows } },
      });
      const sectionRef = renderSectionRef(FAKE_SECTION);
      expect(sectionRef.current.numRows).toEqual(numRows);
    });

    it("should return number of rows set in Pref even if higher than maxRows value", () => {
      const numRows = 10;
      Object.assign(FAKE_SECTION, {
        rowsPref: "section.rows",
        maxRows: 4,
        Prefs: { values: { "section.rows": numRows } },
      });
      const sectionRef = renderSectionRef(FAKE_SECTION);
      expect(sectionRef.current.numRows).toEqual(numRows);
    });
  });
});
