/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { TopicNavigation } from "content-src/components/DiscoveryStreamComponents/TopicNavigation/TopicNavigation";

const GAP = 10;

const SECTIONS = [
  { sectionKey: "top_stories_section", title: "Popular Today", data: [{}] },
  { sectionKey: "politics", title: "Politics", data: [{}] },
  { sectionKey: "science", title: "Science", data: [{}] },
  { sectionKey: "food", title: "Food", data: [{}] },
];

// Every topic is 100px wide, so three of them plus two gaps occupy 320px.
const ITEM_WIDTHS = { Politics: 100, Science: 100, Food: 100 };

// Mutable so a test can describe the layout it wants before rendering.
let containerWidth;
let itemWidths;
let controlWidth;
let observers;

class MockResizeObserver {
  constructor(callback) {
    this.callback = callback;
    this.targets = new Set();
    observers.push(this);
  }
  observe(element) {
    this.targets.add(element);
  }
  unobserve(element) {
    this.targets.delete(element);
  }
  disconnect() {
    this.targets.clear();
    observers = observers.filter(observer => observer !== this);
  }
}

// Hand the live observer a delivery for everything it watches, the way the
// platform would after a resize.
function deliverResize() {
  const observer = observers[observers.length - 1];
  const entries = [...observer.targets].map(target => ({ target }));
  act(() => observer.callback(entries, observer));
}

function rect(width) {
  return { width, height: 20, top: 0, left: 0, right: width, bottom: 20 };
}

const chipTitles = container =>
  [...container.querySelectorAll(".topic-navigation-list li")].map(
    li => li.textContent
  );
const menuTitles = container =>
  [...container.querySelectorAll("panel-item")].map(item => item.textContent);
const chipButton = (container, title) =>
  [...container.querySelectorAll(".topic-navigation-list li")]
    .find(li => li.textContent === title)
    ?.querySelector("moz-button");
const moreButton = container =>
  container.querySelector(".topic-navigation-overflow moz-button");

function renderNav(sections = SECTIONS) {
  const dispatch = jest.fn();
  return {
    dispatch,
    ...render(<TopicNavigation {...{ sections, dispatch }} />),
  };
}

// A section in the page for a topic to scroll to, matching what CardSections
// renders: a heading carrying the id, and cards inside .ds-section.
function addSectionToPage(sectionKey) {
  const section = document.createElement("section");
  section.className = "ds-section";
  section.innerHTML = `
    <h2 id="section-title-${sectionKey}">Heading</h2>
    <a class="ds-card-link" href="https://example.com/first">First card</a>
    <a class="ds-card-link" href="https://example.com/second">Second card</a>
  `;
  document.body.appendChild(section);
  return section;
}

describe("TopicNavigation", () => {
  beforeEach(() => {
    containerWidth = 1000;
    itemWidths = ITEM_WIDTHS;
    controlWidth = 50;
    observers = [];

    globalThis.ResizeObserver = MockResizeObserver;
    Element.prototype.scrollIntoView = jest.fn();

    jest
      .spyOn(Element.prototype, "getBoundingClientRect")
      .mockImplementation(function () {
        if (this.classList.contains("topic-navigation")) {
          return rect(containerWidth);
        }
        if (this.classList.contains("topic-navigation-overflow")) {
          return rect(controlWidth);
        }
        if (this.localName === "li") {
          return rect(itemWidths[this.textContent] ?? 0);
        }
        return rect(0);
      });

    jest
      .spyOn(globalThis, "getComputedStyle")
      .mockImplementation(() => ({ columnGap: `${GAP}px` }));
  });

  afterEach(() => {
    jest.restoreAllMocks();
    delete globalThis.ResizeObserver;
    delete Element.prototype.scrollIntoView;
    document.body.innerHTML = "";
  });

  it("lists the topics in feed order, without Popular Today", () => {
    const { container } = renderNav();
    expect(chipTitles(container)).toEqual(["Politics", "Science", "Food"]);
  });

  describe("overflow", () => {
    it("shows no More button when every topic fits", () => {
      const { container } = renderNav();
      expect(moreButton(container)).toBeNull();
    });

    it("moves the topics that don't fit into the dropdown", () => {
      // 319px leaves 259px once the More button and its gap are reserved,
      // which fits two topics and the gap between them.
      containerWidth = 319;
      const { container } = renderNav();

      expect(chipTitles(container)).toEqual(["Politics", "Science"]);
      expect(moreButton(container)).not.toBeNull();
      expect(menuTitles(container)).toEqual(["Food"]);
    });

    it("recomputes when the container resizes", () => {
      const { container } = renderNav();
      expect(chipTitles(container)).toHaveLength(3);

      containerWidth = 319;
      deliverResize();

      expect(chipTitles(container)).toEqual(["Politics", "Science"]);
      expect(menuTitles(container)).toEqual(["Food"]);
    });
  });

  describe("measuring", () => {
    it("stays hidden until the widths are real, then settles", () => {
      // Buttons that haven't rendered their contents report zero widths, which
      // must not be cached as if they were measurements.
      itemWidths = {};
      const { container } = renderNav();
      const nav = container.querySelector(".topic-navigation");

      expect(nav).toHaveClass("is-measuring");
      expect(chipTitles(container)).toHaveLength(3);

      itemWidths = ITEM_WIDTHS;
      deliverResize();

      expect(nav).not.toHaveClass("is-measuring");
      expect(chipTitles(container)).toEqual(["Politics", "Science", "Food"]);
    });

    it("ignores a width that differs by a fraction of a pixel", () => {
      containerWidth = 319;
      const { container } = renderNav();
      expect(chipTitles(container)).toHaveLength(2);

      // Layout rounding, not a real change. Treating it as one restarts
      // measuring, and the topics coming back report zero widths until they
      // render, so the pass can't finish and the strip stays hidden.
      controlWidth = 50.00001;
      itemWidths = {};
      deliverResize();

      const nav = container.querySelector(".topic-navigation");
      expect(nav).not.toHaveClass("is-measuring");
      expect(chipTitles(container)).toHaveLength(2);
    });

    it("re-measures when a width really changes", () => {
      containerWidth = 319;
      const { container } = renderNav();
      expect(chipTitles(container)).toHaveLength(2);

      // A wider More button leaves room for one topic instead of two.
      controlWidth = 200;
      deliverResize();

      expect(chipTitles(container)).toEqual(["Politics"]);
      expect(menuTitles(container)).toEqual(["Science", "Food"]);
    });
  });

  describe("activation", () => {
    it("scrolls to the section, animated", () => {
      addSectionToPage("science");
      const { container } = renderNav();

      fireEvent.click(chipButton(container, "Science"));

      const heading = document.getElementById("section-title-science");
      expect(heading.scrollIntoView).toHaveBeenCalledWith({
        behavior: "smooth",
        block: "start",
      });
    });

    it("reports a click on the strip", () => {
      const { container, dispatch } = renderNav();

      fireEvent.click(chipButton(container, "Science"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.TOPIC_NAVIGATION_CLICK,
          data: { topic: "science", event_source: "TOPIC_STRIP" },
        })
      );
    });

    it("reports a dropdown pick as coming from the menu", () => {
      containerWidth = 319;
      const { container, dispatch } = renderNav();

      fireEvent.click(container.querySelector("panel-item"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.TOPIC_NAVIGATION_CLICK,
          data: { topic: "food", event_source: "OVERFLOW_MENU" },
        })
      );
    });

    it("moves focus to the section's first card on keyboard activation", () => {
      const section = addSectionToPage("science");
      const { container } = renderNav();

      fireEvent.click(chipButton(container, "Science"), { detail: 0 });

      expect(document.activeElement).toBe(
        section.querySelector("a.ds-card-link")
      );
    });

    it("leaves focus alone on a pointer click", () => {
      addSectionToPage("science");
      const { container } = renderNav();
      const chip = chipButton(container, "Science");

      fireEvent.click(chip, { detail: 1 });

      expect(document.activeElement).not.toHaveClass("ds-card-link");
    });
  });

  describe("keyboard navigation", () => {
    it("gives the strip a single tab stop", () => {
      const { container } = renderNav();
      const tabbable = [
        ...container.querySelectorAll('moz-button[tabindex="0"]'),
      ];
      expect(tabbable).toHaveLength(1);
      expect(tabbable[0]).toBe(chipButton(container, "Politics"));
    });

    it("moves along the strip with arrow keys", () => {
      const { container } = renderNav();
      const nav = container.querySelector(".topic-navigation");

      fireEvent.keyDown(nav, { key: "ArrowRight" });

      expect(document.activeElement).toBe(chipButton(container, "Science"));
      expect(chipButton(container, "Science")).toHaveAttribute("tabindex", "0");
      expect(chipButton(container, "Politics")).toHaveAttribute(
        "tabindex",
        "-1"
      );
    });

    it("treats the More button as the last arrow stop", () => {
      containerWidth = 319;
      const { container } = renderNav();
      const nav = container.querySelector(".topic-navigation");

      fireEvent.keyDown(nav, { key: "End" });

      expect(document.activeElement).toBe(moreButton(container));
    });
  });
});
