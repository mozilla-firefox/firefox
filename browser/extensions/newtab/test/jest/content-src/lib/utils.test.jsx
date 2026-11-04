/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useEffect } from "react";
import { render } from "@testing-library/react";
import { stubGlobals } from "test/jest/test-utils";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";
import {
  focusOnceRendered,
  useIntersectionObserver,
  getActiveCardSize,
  getActiveColumnLayout,
  getCardColumn,
  getNovaColumnLayout,
  useConfetti,
} from "content-src/lib/utils.jsx";

// Test component to use the useIntersectionObserver
function TestComponent({ callback, threshold }) {
  const ref = useIntersectionObserver(callback, threshold);
  return (
    <div
      ref={el => {
        ref.current.push(el);
      }}
    ></div>
  );
}

function TestConfettiComponent({ count, spread }) {
  const [canvasRef, fireConfetti] = useConfetti(count, spread);

  useEffect(() => {
    // Trigger the animation once mounted
    fireConfetti();
  }, [fireConfetti]);

  return <canvas ref={canvasRef} width={100} height={100} />;
}

describe("useIntersectionObserver", () => {
  let callback;
  let threshold;
  let observerStub;
  let restoreGlobals;

  beforeEach(() => {
    callback = jest.fn();
    threshold = 0.5;
    observerStub = jest.fn(function (cb) {
      this.observe = jest.fn();
      this.unobserve = jest.fn();
      this.disconnect = jest.fn();
      this.callback = cb;
    });
    restoreGlobals = stubGlobals({ IntersectionObserver: observerStub });
    // RTL unmounts this render for us in its own afterEach cleanup.
    render(<TestComponent callback={callback} threshold={threshold} />);
  });

  afterEach(() => {
    restoreGlobals();
  });

  it("should create an IntersectionObserver instance with the correct options", () => {
    // The equivalent of sinon's calledWithNew: a plain call would leave the
    // recorded `this` undefined.
    expect(observerStub.mock.instances.at(0)).toBeInstanceOf(observerStub);
    expect(observerStub).toHaveBeenCalledWith(expect.any(Function), {
      threshold,
    });
  });

  it("should observe elements when mounted", () => {
    const [observerInstance] = observerStub.mock.instances;
    expect(observerInstance.observe).toHaveBeenCalled();
  });

  it("should not observe until the enclosing space is active", () => {
    observerStub.mockClear();
    const tree = spaceActive => (
      <SpaceActiveContext.Provider value={spaceActive}>
        <TestComponent callback={callback} threshold={threshold} />
      </SpaceActiveContext.Provider>
    );
    const { rerender } = render(tree(false));
    expect(observerStub).not.toHaveBeenCalled();

    rerender(tree(true));
    const [observerInstance] = observerStub.mock.instances;
    expect(observerInstance.observe).toHaveBeenCalled();
  });

  it("should call callback and unobserve element when it intersects", () => {
    const secondRender = render(
      <TestComponent callback={callback} threshold={threshold} />
    );
    const [observerInstance] = observerStub.mock.instances;
    const observedElement = secondRender.container.querySelector("div");

    // Simulate an intersection
    observerInstance.callback([
      { isIntersecting: true, target: observedElement },
    ]);

    expect(callback).toHaveBeenCalledTimes(1);
    expect(callback).toHaveBeenCalledWith(observedElement);
    expect(observerInstance.unobserve).toHaveBeenCalledTimes(1);
    expect(observerInstance.unobserve).toHaveBeenCalledWith(observedElement);
  });

  it("should not call callback if element is not intersecting", () => {
    const secondRender = render(
      <TestComponent callback={callback} threshold={threshold} />
    );
    const [observerInstance] = observerStub.mock.instances;
    const observedElement = secondRender.container.querySelector("div");

    // Simulate a non-intersecting entry
    observerInstance.callback([
      { isIntersecting: false, target: observedElement },
    ]);

    expect(callback).not.toHaveBeenCalled();
    expect(observerInstance.unobserve).not.toHaveBeenCalled();
  });
});

describe("getActiveCardSize", () => {
  it("returns 'large-card' for col-4-large and screen width 1920 and sections enabled", () => {
    const result = getActiveCardSize(
      1920,
      "col-4-large col-3-medium col-2-small col-1-small",
      true
    );
    expect(result).toBe("large-card");
  });

  it("returns 'medium-card' for col-3-medium and screen width 1200 and sections enabled", () => {
    const result = getActiveCardSize(
      1200,
      "col-4-large col-3-medium col-2-small col-1-small",
      true
    );
    expect(result).toBe("medium-card");
  });

  it("returns 'small-card' for col-2-small and screen width 800 and sections enabled", () => {
    const result = getActiveCardSize(
      800,
      "col-4-large col-3-medium col-2-small col-1-medium",
      true
    );
    expect(result).toBe("small-card");
  });

  it("returns 'medium-card' for col-1-medium at 500px", () => {
    const result = getActiveCardSize(
      500,
      "col-1-medium col-1-position-0",
      true
    );
    expect(result).toBe("medium-card");
  });

  it("returns null when no matching card type is found (edge case)", () => {
    const result = getActiveCardSize(
      1200,
      "col-4-position-0 col-3-position-0",
      true
    );
    expect(result).toBeNull();
  });

  it("returns 'medium-card' when required arguments are missing and sections are disabled", () => {
    const result = getActiveCardSize(null, null, false);
    expect(result).toBe("medium-card");
  });

  it("returns null when required arguments are missing and sections are enabled", () => {
    const result = getActiveCardSize(null, null, true);
    expect(result).toBeNull();
  });

  it("returns 'spoc' when flightId has value", () => {
    const result = getActiveCardSize(null, null, false, 123);
    expect(result).toBe("spoc");
  });

  it("uses columnLayout override instead of screenWidth when provided", () => {
    const result = getActiveCardSize(
      1400,
      "col-4-large col-3-medium col-2-small col-1-small",
      true,
      null,
      "col-3"
    );
    expect(result).toBe("medium-card");
  });

  it("returns correct size with columnLayout and no screenWidth", () => {
    const result = getActiveCardSize(
      null,
      "col-3-large col-2-medium col-1-small",
      true,
      null,
      "col-3"
    );
    expect(result).toBe("large-card");
  });
});

describe("getNovaColumnLayout", () => {
  it("returns null when el is null", () => {
    expect(getNovaColumnLayout(null)).toBeNull();
  });

  it("returns null when --sections-col-count is not set", () => {
    const el = document.createElement("div");
    document.body.appendChild(el);
    expect(getNovaColumnLayout(el)).toBeNull();
    el.remove();
  });

  it("returns the correct col-N string when --sections-col-count is set", () => {
    const el = document.createElement("div");
    el.style.setProperty("--sections-col-count", "3");
    document.body.appendChild(el);
    expect(getNovaColumnLayout(el)).toBe("col-3");
    el.remove();
  });
});

describe("getCardColumn", () => {
  const COL_WIDTH = 100;
  const GAP = 10;
  const STRIDE = COL_WIDTH + GAP;
  let grid;
  let gridWidth;
  let nextColumn;

  // jsdom does not lay out CSS grid, so the geometry getCardColumn measures is
  // stubbed instead: equal-width columns separated by GAP, with cards placed
  // in order from the inline start.
  function buildGrid(columnCount) {
    gridWidth = columnCount * COL_WIDTH + (columnCount - 1) * GAP;
    nextColumn = 1;
    grid = document.createElement("div");
    grid.className = "ds-section-grid";
    grid.style.columnGap = `${GAP}px`;
    grid.style.setProperty("--sections-col-count", `${columnCount}`);
    grid.getBoundingClientRect = () => ({
      left: 0,
      right: gridWidth,
      width: gridWidth,
    });
    document.body.appendChild(grid);
    return grid;
  }

  function addCard(span = 1) {
    const card = document.createElement("article");
    const width = span * COL_WIDTH + (span - 1) * GAP;
    const inlineStart = (nextColumn - 1) * STRIDE;
    nextColumn += span;
    card.getBoundingClientRect = () =>
      document.dir === "rtl"
        ? {
            left: gridWidth - inlineStart - width,
            right: gridWidth - inlineStart,
            width,
          }
        : { left: inlineStart, right: inlineStart + width, width };
    grid.appendChild(card);
    return card;
  }

  afterEach(() => {
    grid?.remove();
    grid = null;
    document.dir = "ltr";
  });

  it("returns null for a card with no layout", () => {
    buildGrid(4);
    const card = addCard();
    // An unlaid-out card measures zero-width.
    card.getBoundingClientRect = () => ({ left: 0, right: 0, width: 0 });
    expect(getCardColumn(card)).toBeNull();
  });

  it("reports each column across a full row", () => {
    buildGrid(4);
    const cards = [addCard(), addCard(), addCard(), addCard()];
    expect(cards.map(card => getCardColumn(card))).toEqual([1, 2, 3, 4]);
  });

  it("reports the leftmost column for a card spanning several", () => {
    buildGrid(4);
    const wide = addCard(2);
    const next = addCard();
    const last = addCard();
    expect(getCardColumn(wide)).toBe(1);
    expect(getCardColumn(next)).toBe(3);
    expect(getCardColumn(last)).toBe(4);
  });

  it("counts columns from the inline start under RTL", () => {
    document.dir = "rtl";
    buildGrid(3);
    const cards = [addCard(), addCard(), addCard()];
    expect(cards.map(card => getCardColumn(card))).toEqual([1, 2, 3]);
  });

  it("resolves the grid item from an element inside the card", () => {
    buildGrid(2);
    addCard();
    const card = addCard();
    const inner = document.createElement("span");
    card.appendChild(inner);
    expect(getCardColumn(inner)).toBe(2);
  });
});

describe("getActiveColumnLayout", () => {
  it("returns 'col-4' for screen width 1920", () => {
    const result = getActiveColumnLayout(1920);
    expect(result).toBe("col-4");
  });

  it("returns 'col-3' for screen width 1200", () => {
    const result = getActiveColumnLayout(1200);
    expect(result).toBe("col-3");
  });

  it("returns 'col-2' for screen width 800", () => {
    const result = getActiveColumnLayout(800);
    expect(result).toBe("col-2");
  });

  it("returns 'col-1' for screen width 500", () => {
    const result = getActiveColumnLayout(500);
    expect(result).toBe("col-1");
  });

  it("returns 'col-1' when screen width is missing", () => {
    const result = getActiveColumnLayout(undefined);
    expect(result).toBe("col-1");
  });
});

describe("useConfetti hook", () => {
  let rafStub;
  let getContextStub;
  let fakeContext;
  let prefersReducedMotion;

  beforeEach(() => {
    prefersReducedMotion = false;

    // Create a fake 2D context
    fakeContext = {
      clearRect: jest.fn(),
      setTransform: jest.fn(),
      rotate: jest.fn(),
      scale: jest.fn(),
      fillRect: jest.fn(),
      globalAlpha: 1,
    };

    // Stub getContext on all canvas elements
    getContextStub = jest
      .spyOn(HTMLCanvasElement.prototype, "getContext")
      .mockImplementation(type => (type === "2d" ? fakeContext : null));

    jest
      .spyOn(window, "matchMedia")
      .mockImplementation(query =>
        query === "(prefers-reduced-motion: reduce)"
          ? { matches: prefersReducedMotion }
          : { matches: false }
      );

    // stub so that it only runs for one frame
    rafStub = jest.spyOn(window, "requestAnimationFrame").mockReturnValue(24);
    jest.spyOn(window, "cancelAnimationFrame").mockImplementation(() => {});
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("should initialize and animate confetti when fireConfetti is called", () => {
    // Mount the component, which calls fireConfetti in useEffect
    render(<TestConfettiComponent count={5} />);
    expect(getContextStub).toHaveBeenCalledWith("2d");
    expect(fakeContext.clearRect).toHaveBeenCalledTimes(1);
    expect(fakeContext.fillRect).toHaveBeenCalledTimes(5);
    expect(rafStub).toHaveBeenCalledTimes(1);
  });
  it("does nothing when prefers-reduced-motion is enabled", () => {
    // simulate prefers reduced motion
    prefersReducedMotion = true;

    render(<TestConfettiComponent count={5} />);

    // Confrim the confetti hasnt been drawn
    expect(fakeContext.clearRect).not.toHaveBeenCalled();
    expect(fakeContext.fillRect).not.toHaveBeenCalled();
    expect(rafStub).not.toHaveBeenCalled();
  });
});

describe("focusOnceRendered", () => {
  it("waits for a moz-button's first render before focusing it", async () => {
    const focus = jest.fn();
    focusOnceRendered({ updateComplete: Promise.resolve(), focus });
    expect(focus).not.toHaveBeenCalled();
    await Promise.resolve();
    expect(focus).toHaveBeenCalledTimes(1);
  });

  it("focuses an element with no pending render straight away and ignores null", () => {
    const focus = jest.fn();
    focusOnceRendered({ focus });
    expect(focus).toHaveBeenCalledTimes(1);
    expect(() => focusOnceRendered(null)).not.toThrow();
  });
});
