/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useContext } from "react";
import { render, fireEvent, waitFor } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { Spaces } from "content-src/components/Spaces/Spaces";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";

const SPACES = [
  { id: "stories", content: <div data-testid="stories-content" /> },
  { id: "widgets", content: <div data-testid="widgets-content" /> },
  { id: "activity", content: <div data-testid="activity-content" /> },
];

function setup(spaces = SPACES) {
  const dispatch = jest.fn();
  const utils = render(<Spaces spaces={spaces} dispatch={dispatch} />);
  return { dispatch, ...utils };
}

// The switch payload, without the action envelope.
function switchData(dispatch) {
  const [action] = dispatch.mock.calls.at(-1);
  expect(action.type).toBe(at.SPACES_USER_EVENT);
  return action.data;
}

// A story card in a section grid laid out at one column, where CardSections
// sizes it with col-1-<size>.
function SectionCard({ className = "ds-card col-1-medium", ...props }) {
  return (
    <div className="ds-section-grid" style={{ "--sections-col-count": "1" }}>
      <article className={className} {...props} />
    </div>
  );
}

describe("Spaces", () => {
  it("renders a tab per space and selects the first", () => {
    const { container } = setup();
    const tabs = container.querySelectorAll('[role="tab"]');
    expect(tabs).toHaveLength(3);
    expect(tabs[0].getAttribute("aria-selected")).toBe("true");
    expect(tabs[1].getAttribute("aria-selected")).toBe("false");
    // Roving tabindex, so the tablist is a single tab stop.
    expect(tabs[0].tabIndex).toBe(0);
    expect(tabs[1].tabIndex).toBe(-1);
  });

  it("mounts every space, so returning to one does not remount it", () => {
    const { getByTestId } = setup();
    expect(getByTestId("stories-content")).toBeTruthy();
    expect(getByTestId("widgets-content")).toBeTruthy();
    expect(getByTestId("activity-content")).toBeTruthy();
  });

  it("hides the inactive spaces from assistive tech and from tabbing", () => {
    const { container } = setup();
    const panels = container.querySelectorAll('[role="tabpanel"]');
    expect(panels[0].hasAttribute("aria-hidden")).toBe(false);
    expect(panels[0].hasAttribute("inert")).toBe(false);
    expect(panels[1].getAttribute("aria-hidden")).toBe("true");
    expect(panels[1].hasAttribute("inert")).toBe(true);
  });

  it("tells only the active space's content that it is active", () => {
    function ActiveFlag({ id }) {
      return (
        <div data-testid={id} data-active={useContext(SpaceActiveContext)} />
      );
    }
    const { container, getByTestId } = setup([
      { id: "stories", content: <ActiveFlag id="stories-flag" /> },
      { id: "widgets", content: <ActiveFlag id="widgets-flag" /> },
    ]);
    expect(getByTestId("stories-flag").dataset.active).toBe("true");
    expect(getByTestId("widgets-flag").dataset.active).toBe("false");

    fireEvent.click(container.querySelector("#spaces-tab-widgets"));

    expect(getByTestId("stories-flag").dataset.active).toBe("false");
    expect(getByTestId("widgets-flag").dataset.active).toBe("true");
  });

  it("does not peek outside the peek variant", () => {
    const { container } = setup();
    expect(container.querySelector(".spaces-peek-box")).toBeNull();
    expect(container.querySelector(".space.peeking")).toBeNull();
  });

  it("offsets each space from the active one, so only one is in view", () => {
    const { container } = setup();
    const panels = container.querySelectorAll('[role="tabpanel"]');
    expect(panels[0].style.getPropertyValue("--space-offset")).toBe("0");
    expect(panels[1].style.getPropertyValue("--space-offset")).toBe("1");
    expect(panels[2].style.getPropertyValue("--space-offset")).toBe("2");

    fireEvent.click(container.querySelector("#spaces-tab-widgets"));

    const after = container.querySelectorAll('[role="tabpanel"]');
    expect(after[0].style.getPropertyValue("--space-offset")).toBe("-1");
    expect(after[1].style.getPropertyValue("--space-offset")).toBe("0");
    expect(after[2].style.getPropertyValue("--space-offset")).toBe("1");
  });

  it("records a switch from the segmented control", () => {
    const { container, dispatch } = setup();
    fireEvent.click(container.querySelector("#spaces-tab-activity"));
    expect(switchData(dispatch)).toEqual({
      space: "activity",
      previous_space: "stories",
      method: "tab",
    });
  });

  it("does not record re-selecting the active space", () => {
    const { container, dispatch } = setup();
    fireEvent.click(container.querySelector("#spaces-tab-stories"));
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("advances a space on a horizontal wheel gesture", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    fireEvent.wheel(spaces, { deltaX: 80, deltaY: 0 });

    expect(switchData(dispatch)).toEqual({
      space: "widgets",
      previous_space: "stories",
      method: "swipe",
    });
  });

  it("treats shift plus wheel as horizontal, which Gecko reports as deltaY", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    fireEvent.wheel(spaces, { deltaX: 0, deltaY: 80, shiftKey: true });

    expect(switchData(dispatch)).toEqual({
      space: "widgets",
      previous_space: "stories",
      method: "swipe",
    });
  });

  it("leaves ordinary vertical scrolling alone", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    fireEvent.wheel(spaces, { deltaX: 5, deltaY: 200 });

    expect(dispatch).not.toHaveBeenCalled();
  });

  it("spends one wheel gesture on one switch, despite momentum", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    // A flick plus its momentum tail, with no quiet period between.
    for (let i = 0; i < 6; i++) {
      fireEvent.wheel(spaces, { deltaX: 40, deltaY: 0 });
    }

    expect(dispatch).toHaveBeenCalledTimes(1);
  });

  it("advances a space on a touch swipe", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    fireEvent.touchStart(spaces, { touches: [{ clientX: 300, clientY: 100 }] });
    // Dragging left pulls the next space in.
    fireEvent.touchEnd(spaces, {
      changedTouches: [{ clientX: 200, clientY: 105 }],
    });

    expect(switchData(dispatch).space).toBe("widgets");
    expect(switchData(dispatch).method).toBe("swipe");
  });

  it("ignores a mostly-vertical touch drag", () => {
    const { container, dispatch } = setup();
    const spaces = container.querySelector(".spaces-container");

    fireEvent.touchStart(spaces, { touches: [{ clientX: 300, clientY: 100 }] });
    fireEvent.touchEnd(spaces, {
      changedTouches: [{ clientX: 260, clientY: 400 }],
    });

    expect(dispatch).not.toHaveBeenCalled();
  });

  it("traverses with the arrow keys and reports the keyboard as the method", () => {
    const { container, dispatch } = setup();
    const tablist = container.querySelector('[role="tablist"]');

    fireEvent.keyDown(tablist, { key: "ArrowRight" });
    expect(switchData(dispatch)).toEqual({
      space: "widgets",
      previous_space: "stories",
      method: "keyboard",
    });

    fireEvent.keyDown(tablist, { key: "End" });
    expect(switchData(dispatch).space).toBe("activity");

    fireEvent.keyDown(tablist, { key: "Home" });
    expect(switchData(dispatch).space).toBe("stories");
  });

  it("does not run off either end", () => {
    const { container, dispatch } = setup();
    const tablist = container.querySelector('[role="tablist"]');
    fireEvent.keyDown(tablist, { key: "ArrowLeft" });
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("stays on the same space when an earlier one is turned off", () => {
    const dispatch = jest.fn();
    const { container, rerender } = render(
      <Spaces spaces={SPACES} dispatch={dispatch} />
    );
    fireEvent.click(container.querySelector("#spaces-tab-widgets"));

    // Drop Stories, which shifts every index after it.
    rerender(<Spaces spaces={SPACES.slice(1)} dispatch={dispatch} />);

    expect(
      container
        .querySelector("#spaces-tab-widgets")
        .getAttribute("aria-selected")
    ).toBe("true");
  });

  it("falls back to the leftmost space when the active one is turned off", () => {
    const dispatch = jest.fn();
    const { container, rerender } = render(
      <Spaces spaces={SPACES} dispatch={dispatch} />
    );
    fireEvent.click(container.querySelector("#spaces-tab-activity"));

    rerender(<Spaces spaces={SPACES.slice(0, 2)} dispatch={dispatch} />);

    expect(
      container
        .querySelector("#spaces-tab-stories")
        .getAttribute("aria-selected")
    ).toBe("true");
  });
});

describe("Spaces with arrows", () => {
  function setupArrows(defaultId) {
    const dispatch = jest.fn();
    const utils = render(
      <Spaces
        spaces={SPACES}
        defaultId={defaultId}
        arrows={true}
        dispatch={dispatch}
      />
    );
    const arrow = direction =>
      utils.container.querySelector(
        `.spaces-arrow-rail.${direction} moz-button`
      );
    return { dispatch, arrow, ...utils };
  }

  it("replaces the tablist with an arrow toward each neighbouring space", () => {
    const { container, arrow } = setupArrows("widgets");
    expect(container.querySelector('[role="tablist"]')).toBeNull();
    expect(container.querySelector('[role="tabpanel"]')).toBeNull();
    expect(arrow("prev").dataset.l10nId).toBe("newtab-spaces-tab-stories");
    expect(arrow("next").dataset.l10nId).toBe("newtab-spaces-tab-activity");
  });

  it("wraps from the first space to the last and back", () => {
    const { arrow, dispatch } = setupArrows();
    expect(arrow("prev").dataset.l10nId).toBe("newtab-spaces-tab-activity");

    fireEvent.click(arrow("prev"));
    expect(switchData(dispatch).space).toBe("activity");
    expect(arrow("next").dataset.l10nId).toBe("newtab-spaces-tab-stories");

    fireEvent.click(arrow("next"));
    expect(switchData(dispatch).space).toBe("stories");
  });

  it("shows the tablist too when asked, and keeps the panels labelled", () => {
    const { container } = render(
      <Spaces
        spaces={SPACES}
        defaultId="widgets"
        arrows={true}
        tablist={true}
        dispatch={jest.fn()}
      />
    );
    expect(container.querySelectorAll(".spaces-arrow-rail")).toHaveLength(2);
    expect(container.querySelectorAll('[role="tab"]')).toHaveLength(3);
    const panel = container.querySelector("#spaces-panel-widgets");
    expect(panel.getAttribute("role")).toBe("tabpanel");
    expect(panel.getAttribute("aria-labelledby")).toBe("spaces-tab-widgets");
  });

  it("keeps the arrows and the tab selection in step", () => {
    const { container, getByRole } = render(
      <Spaces
        spaces={SPACES}
        arrows={true}
        tablist={true}
        dispatch={jest.fn()}
      />
    );
    fireEvent.click(
      container.querySelector(".spaces-arrow-rail.next moz-button")
    );
    expect(
      container
        .querySelector("#spaces-tab-widgets")
        .getAttribute("aria-selected")
    ).toBe("true");
    fireEvent.click(container.querySelector("#spaces-tab-activity"));
    expect(
      container.querySelector(".spaces-arrow-rail.prev moz-button").dataset
        .l10nId
    ).toBe("newtab-spaces-tab-widgets");
    expect(getByRole("tablist")).toBeTruthy();
  });

  it("records an arrow switch", () => {
    const { arrow, dispatch } = setupArrows();
    fireEvent.click(arrow("next"));
    expect(switchData(dispatch)).toEqual({
      space: "widgets",
      previous_space: "stories",
      method: "arrow",
    });
  });
});

describe("Spaces with preview", () => {
  const PREVIEW_SPACES = [
    {
      id: "stories",
      content: <article className="ds-card" data-testid="first-story" />,
    },
    {
      id: "widgets",
      content: (
        <div className="widgets-container">
          <div className="widget-wrapper" data-testid="first-widget" />
        </div>
      ),
    },
    { id: "activity", content: <div /> },
  ];

  function setupPreview({ spaces = PREVIEW_SPACES } = {}) {
    const dispatch = jest.fn();
    document.mozSetImageElement = jest.fn();
    const utils = render(
      <Spaces spaces={spaces} preview={true} dispatch={dispatch} />
    );
    const tab = id => utils.container.querySelector(`#spaces-tab-${id}`);
    const previewBox = () => utils.container.querySelector(".spaces-preview");
    return { dispatch, tab, previewBox, ...utils };
  }

  afterEach(() => {
    delete document.mozSetImageElement;
  });

  it("previews the next space before the tablist is used", () => {
    const { previewBox, getByTestId } = setupPreview();
    expect(previewBox().getAttribute("aria-hidden")).toBe("true");
    expect(previewBox().style.getPropertyValue("--spaces-preview-anchor")).toBe(
      "--spaces-tab-widgets"
    );
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("first-widget")
    );
  });

  it("previews a slotted widget's wrapper, not its slot", () => {
    const { getByTestId } = setupPreview({
      spaces: PREVIEW_SPACES.map(space =>
        space.id === "widgets"
          ? {
              ...space,
              content: (
                <div className="widgets-container">
                  <div data-slot-key="lists" style={{ display: "contents" }}>
                    <div
                      className="widget-wrapper"
                      data-testid="first-widget"
                    />
                  </div>
                </div>
              ),
            }
          : space
      ),
    });
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("first-widget")
    );
  });

  it("previews the hovered tab, and nothing once the pointer leaves", () => {
    const { container, tab, previewBox } = setupPreview({
      spaces: PREVIEW_SPACES.map(space =>
        space.id === "activity"
          ? { ...space, content: <li className="card-outer" /> }
          : space
      ),
    });
    fireEvent.pointerEnter(tab("activity"));
    expect(previewBox().style.getPropertyValue("--spaces-preview-anchor")).toBe(
      "--spaces-tab-activity"
    );

    fireEvent.pointerLeave(container.querySelector('[role="tablist"]'));
    expect(previewBox()).toBeNull();
  });

  it("previews nothing for a space with no first item", () => {
    const { tab, previewBox } = setupPreview();
    fireEvent.pointerEnter(tab("activity"));
    expect(previewBox()).toBeNull();
  });

  it("prefers the space's own preview image to its first item", () => {
    const src = "chrome://newtab/content/data/content/assets/widgets.png";
    const { container } = setupPreview({
      spaces: PREVIEW_SPACES.map(space =>
        space.id === "widgets" ? { ...space, previewImage: src } : space
      ),
    });
    expect(
      container.querySelector(".spaces-preview-image").getAttribute("srcset")
    ).toBe(`${src} 2x`);
    expect(document.mozSetImageElement).not.toHaveBeenCalled();
  });

  it("previews nothing for the active tab", () => {
    const { tab, previewBox } = setupPreview();
    fireEvent.pointerEnter(tab("stories"));
    expect(previewBox()).toBeNull();
  });

  it("stops the default preview after a switch", () => {
    const { container, tab, previewBox } = setupPreview();
    fireEvent.click(tab("widgets"));
    fireEvent.pointerLeave(container.querySelector('[role="tablist"]'));
    expect(previewBox()).toBeNull();
  });

  it("holds back a first-item preview until the space is ready", () => {
    document.mozSetImageElement = jest.fn();
    const widget = (
      <div className="widgets-container">
        <div className="widget-wrapper" data-testid="widget" />
      </div>
    );
    const spacesWith = (ready, homeContent) => [
      { id: "games", label: "Games", content: <div /> },
      { id: "home", label: "Home", content: homeContent, ready },
    ];
    const { container, rerender, getByTestId } = render(
      <Spaces
        spaces={spacesWith(false, widget)}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(container.querySelector(".spaces-preview")).toBeNull();
    expect(document.mozSetImageElement).not.toHaveBeenCalled();

    rerender(
      <Spaces
        spaces={spacesWith(
          true,
          <>
            <SectionCard data-testid="story" />
            {widget}
          </>
        )}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("story")
    );
  });

  it("waits for a placeholder story rather than skipping to a widget", async () => {
    document.mozSetImageElement = jest.fn();
    const content = storyClass => (
      <>
        <SectionCard className={storyClass} data-testid="story" />
        <div className="widgets-container">
          <div className="widget-wrapper" data-testid="widget" />
        </div>
      </>
    );
    const spacesWith = storyClass => [
      { id: "games", label: "Games", content: <div /> },
      { id: "living", label: "Living", content: content(storyClass) },
    ];
    const { container, rerender, getByTestId } = render(
      <Spaces
        spaces={spacesWith("ds-card placeholder")}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(container.querySelector(".spaces-preview")).toBeNull();
    expect(document.mozSetImageElement).not.toHaveBeenCalled();

    rerender(
      <Spaces
        spaces={spacesWith("ds-card col-1-medium")}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    await waitFor(() =>
      expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
        "spaces-preview-item",
        getByTestId("story")
      )
    );
    expect(document.mozSetImageElement).toHaveBeenCalledTimes(1);
  });

  it("previews the first card that is medium at the current column count", () => {
    document.mozSetImageElement = jest.fn();
    const { getByTestId } = render(
      <Spaces
        spaces={[
          { id: "games", label: "Games", content: <div /> },
          {
            id: "living",
            label: "Living",
            content: (
              <>
                <SectionCard
                  className="ds-card col-1-large col-4-medium"
                  data-testid="large"
                />
                <SectionCard
                  className="ds-card col-1-medium col-4-large"
                  data-testid="medium"
                />
              </>
            ),
          },
        ]}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("medium")
    );
  });

  it("skips a sponsored card for the next medium story", () => {
    document.mozSetImageElement = jest.fn();
    const { getByTestId } = render(
      <Spaces
        spaces={[
          { id: "games", label: "Games", content: <div /> },
          {
            id: "living",
            label: "Living",
            content: (
              <>
                <SectionCard className="ds-card col-1-medium ds-spoc" />
                <SectionCard data-testid="organic" />
              </>
            ),
          },
        ]}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("organic")
    );
  });

  it("falls back to the first widget when no story card is medium", () => {
    document.mozSetImageElement = jest.fn();
    const { getByTestId } = render(
      <Spaces
        spaces={[
          { id: "games", label: "Games", content: <div /> },
          {
            id: "living",
            label: "Living",
            content: (
              <>
                <SectionCard className="ds-card col-1-large" />
                <div className="widgets-container">
                  <div className="widget-wrapper" data-testid="widget" />
                </div>
              </>
            ),
          },
        ]}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(document.mozSetImageElement).toHaveBeenLastCalledWith(
      "spaces-preview-item",
      getByTestId("widget")
    );
  });

  it("shows a configured preview image before the space is ready", () => {
    const src = "chrome://newtab/content/data/content/assets/home.png";
    const { container } = render(
      <Spaces
        spaces={[
          { id: "games", label: "Games", content: <div /> },
          {
            id: "home",
            label: "Home",
            content: <div />,
            ready: false,
            previewImage: src,
          },
        ]}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(
      container.querySelector(".spaces-preview-image").getAttribute("srcset")
    ).toBe(`${src} 2x`);
  });

  it("loads the previewed item's lazy images", () => {
    document.mozSetImageElement = jest.fn();
    const { getByTestId } = render(
      <Spaces
        spaces={[
          { id: "stories", content: <div /> },
          {
            id: "games",
            label: "Games",
            content: (
              <SectionCard>
                <img loading="lazy" data-testid="story-image" alt="" />
              </SectionCard>
            ),
          },
        ]}
        preview={true}
        dispatch={jest.fn()}
      />
    );
    expect(getByTestId("story-image").getAttribute("loading")).toBe("eager");
  });

  it("records a preview_tab switch", () => {
    const { tab, dispatch } = setupPreview();
    fireEvent.click(tab("widgets"));
    expect(switchData(dispatch)).toEqual({
      space: "widgets",
      previous_space: "stories",
      method: "preview_tab",
    });
  });
});

describe("Spaces with peek", () => {
  // Opens on the middle space, which has a neighbour on each side.
  function setupPeek() {
    const dispatch = jest.fn();
    const utils = render(
      <Spaces
        spaces={SPACES}
        defaultId="widgets"
        peek={true}
        dispatch={dispatch}
      />
    );
    const peekBox = side =>
      utils.container.querySelector(`.spaces-peek-box.${side}`);
    const peekSides = () =>
      [...utils.container.querySelectorAll(".spaces-peek-box")].map(el =>
        el.classList.contains("prev") ? "prev" : "next"
      );
    const peeking = side =>
      [...utils.container.querySelectorAll(`.space.peeking-${side}`)].map(
        el => el.id
      );
    const tab = id => utils.container.querySelector(`#spaces-tab-${id}`);
    const hoverClass = () =>
      [...utils.container.querySelector(".spaces-container").classList].find(
        name => name.startsWith("peek-hover")
      );
    return {
      dispatch,
      peekBox,
      peekSides,
      peeking,
      tab,
      hoverClass,
      ...utils,
    };
  }

  it("peeks at each neighbour, on the side it sits", () => {
    const { peekSides, peeking, tab } = setupPeek();
    expect(peekSides()).toEqual(["prev", "next"]);
    expect(peeking("prev")).toEqual(["spaces-panel-stories"]);
    expect(peeking("next")).toEqual(["spaces-panel-activity"]);

    fireEvent.click(tab("stories"));
    expect(peekSides()).toEqual(["next"]);
    expect(peeking("next")).toEqual(["spaces-panel-widgets"]);

    fireEvent.click(tab("activity"));
    expect(peekSides()).toEqual(["prev"]);
    expect(peeking("prev")).toEqual(["spaces-panel-widgets"]);
  });

  it("records a peek switch", () => {
    const { dispatch, peekBox } = setupPeek();
    fireEvent.click(peekBox("prev"));
    expect(switchData(dispatch)).toEqual({
      space: "stories",
      previous_space: "widgets",
      method: "peek",
    });
  });

  it("slides a peek in while it or its tab is hovered", () => {
    const { peekBox, tab, hoverClass } = setupPeek();

    fireEvent.mouseEnter(peekBox("prev"));
    expect(hoverClass()).toBe("peek-hover-prev");
    fireEvent.mouseLeave(peekBox("prev"));
    expect(hoverClass()).toBeUndefined();

    fireEvent.mouseEnter(tab("activity"));
    expect(hoverClass()).toBe("peek-hover-next");
    fireEvent.mouseLeave(tab("activity"));

    // The active space's tab has no peek to slide.
    fireEvent.mouseEnter(tab("widgets"));
    expect(hoverClass()).toBeUndefined();
  });

  it("clears the hover slide effect after a switch", () => {
    const { peekBox, hoverClass } = setupPeek();
    fireEvent.mouseEnter(peekBox("next"));
    fireEvent.click(peekBox("next"));
    expect(hoverClass()).toBeUndefined();
  });
});
