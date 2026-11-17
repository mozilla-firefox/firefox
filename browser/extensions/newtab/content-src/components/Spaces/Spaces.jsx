/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useCallback, useEffect, useRef, useState } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { SPACE_IDS } from "common/PageLayoutVariants.mjs";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";

// Tab label and icon per space. The section inside keeps its own heading.
const SPACE_META = {
  [SPACE_IDS.STORIES]: {
    l10nId: "newtab-spaces-tab-stories",
    iconsrc: "chrome://global/skin/icons/newsfeed.svg",
  },
  [SPACE_IDS.WIDGETS]: {
    l10nId: "newtab-spaces-tab-widgets",
    iconsrc: "chrome://browser/skin/topsites.svg",
  },
  [SPACE_IDS.ACTIVITY]: {
    l10nId: "newtab-spaces-tab-activity",
    iconsrc: "chrome://browser/skin/history.svg",
  },
};

// Trackpad momentum keeps firing after the fingers lift, so a gesture is locked
// until the wheel goes quiet. Otherwise one flick skips several spaces.
const SWIPE_THRESHOLD_PX = 50;
const SWIPE_END_MS = 200;

// What a space falls back to previewing when it has no medium story card: its
// first widget, else its first Recent activity card.
const PREVIEW_FALLBACK_SELECTOR = ".widgets-container > *, .card-outer";

/**
 * The item a space previews: its first organic story card that is medium at
 * the current column count, so every story preview has the same shape, else
 * its first widget or Recent activity card. Sponsored cards are skipped. CardSections gives each card a size
 * class per column count, `col-<count>-<size>`, and the count in force is the
 * --sections-col-count its grid sets; the grid is a subgrid of the page, so
 * its own tracks do not say.
 *
 * @param {Element} panel - the space's panel
 * @returns {Element|null|undefined} undefined while any story card is still a
 *   placeholder, as placeholders carry no size classes; null for nothing to
 *   preview
 */
function findPreviewItem(panel) {
  const cards = [...panel.querySelectorAll(".ds-card")];
  if (cards.some(card => card.classList.contains("placeholder"))) {
    return undefined;
  }
  const medium = cards.find(card => {
    if (card.matches(".ds-spoc, .ds-spoc-rectangle")) {
      return false;
    }
    const grid = card.closest(".ds-section-grid");
    const columns = grid
      ? getComputedStyle(grid).getPropertyValue("--sections-col-count").trim()
      : "";
    return card.classList.contains(`col-${columns}-medium`);
  });
  return medium ?? panel.querySelector(PREVIEW_FALLBACK_SELECTOR);
}
// The -moz-element() name the previewed item is painted through. Set inline,
// as stylelint reads the #id as a hex colour.
const PREVIEW_IMAGE_ID = "spaces-preview-item";

// Computed direction, not the dir attribute: about:newtab carries none on
// <html>, so the attribute can read empty in an RTL build. Only called from
// event handlers -- the startup cache renders without a window, so this cannot
// run during render.
function isRtl() {
  return getComputedStyle(document.documentElement).direction === "rtl";
}

/**
 * Navigable panels for the content band.
 *
 * Every space stays mounted and the track clips the inactive ones. Impression
 * observers are viewport-relative, so a clipped space never reports as seen;
 * unmounting instead would re-fire impressions on every return.
 *
 * @param {object} props
 * @param {Array<{id: string, content: React.ReactNode, label?: string,
 *   icon?: string, iconFamily?: string, previewImage?: string,
 *   ready?: boolean}>} props.spaces - populated spaces, in tablist order.
 *   label, icon and iconFamily are set for config-driven spaces and absent for
 *   the V1 ones; previewImage comes from the config or the preview variant's
 *   image set. ready is false while a space's content is still loading, which
 *   holds back its first-item preview.
 * @param {string} [props.defaultId] - space to open on, when that is not the
 *   leftmost one. Defaults to the leftmost.
 * @param {boolean} [props.arrows] - navigate with an arrow at each edge
 * @param {boolean} [props.tablist] - navigate with the tablist. Defaults to on
 *   unless arrows are
 * @param {boolean} [props.preview] - show a preview of the space a tab leads
 *   to: of the next space until the first interaction with the tablist, then
 *   of whichever tab is hovered. The space's previewImage if it has one, else
 *   its first item, and nothing for a space with neither. Needs the tablist.
 * @param {boolean} [props.peek] - preview the spaces on either side of the
 *   active one in the page margins
 * @param {Function} props.dispatch - Redux dispatch, for switch telemetry
 */
export function Spaces({
  spaces,
  defaultId,
  arrows,
  tablist = !arrows,
  preview,
  peek,
  dispatch,
}) {
  // By id, not index: turning a space off shifts the indices after it.
  const [activeId, setActiveId] = useState(defaultId ?? spaces[0]?.id);
  // Falls back to the leftmost space when the active one is turned off.
  const activeIndex = Math.max(
    spaces.findIndex(space => space.id === activeId),
    0
  );
  // Hydration is itself a change of translate, so animate from the second render.
  const [animate, setAnimate] = useState(false);
  // The side whose peek is hovered, "prev" or "next".
  const [peekHover, setPeekHover] = useState(null);
  const tablistRef = useRef(null);
  const containerRef = useRef(null);
  // Keyboard switches move focus to the new tab; pointer clicks do not.
  const focusActiveTab = useRef(false);

  const [hoveredId, setHoveredId] = useState(null);
  const [previewInteracted, setPreviewInteracted] = useState(false);
  // { id, found }: whether that space had a first item to paint.
  const [previewItem, setPreviewItem] = useState(null);
  const activeSpaceId = spaces[activeIndex]?.id;
  const nextSpaceId = spaces[(activeIndex + 1) % spaces.length]?.id;
  let previewId = null;
  if (preview) {
    previewId = hoveredId ?? (previewInteracted ? null : nextSpaceId);
  }
  // The active space is already on screen.
  if (previewId === activeSpaceId) {
    previewId = null;
  }
  const previewSpace = spaces.find(space => space.id === previewId);
  const previewImage = previewSpace?.previewImage;
  // Until then the first item may be a placeholder, or something that the
  // loaded content will push down.
  const previewReady = previewSpace?.ready !== false;

  // Painted live with -moz-element() rather than copied, so the item keeps the
  // styles its own space gives it, and no copy reports impressions.
  useEffect(() => {
    if (!previewId || previewImage || !previewReady) {
      return undefined;
    }
    const panel = containerRef.current?.querySelector(
      `#spaces-panel-${previewId}`
    );
    if (!panel) {
      return undefined;
    }
    let observer;
    let registered = false;
    // DSCard stays a placeholder until it is seen or the page goes idle, and a
    // clipped space is never seen, so this waits for the idle render.
    const settle = () => {
      const item = findPreviewItem(panel);
      if (item === undefined) {
        return false;
      }
      observer?.disconnect();
      setPreviewItem({ id: previewId, found: !!item });
      if (item) {
        // A clipped space never scrolls into view, so its lazy images would
        // never load and the preview would paint the card without them.
        for (const img of item.querySelectorAll('img[loading="lazy"]')) {
          img.setAttribute("loading", "eager");
        }
        document.mozSetImageElement?.(PREVIEW_IMAGE_ID, item);
        registered = true;
      }
      return true;
    };
    if (!settle()) {
      observer = new MutationObserver(settle);
      observer.observe(panel, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: ["class"],
      });
    }
    return () => {
      observer?.disconnect();
      if (registered) {
        document.mozSetImageElement?.(PREVIEW_IMAGE_ID, null);
      }
    };
  }, [previewId, previewImage, previewReady]);

  let previewContent = null;
  if (previewImage) {
    // Preview images are 2x, so the descriptor gives their designed size.
    previewContent = (
      <img
        className="spaces-preview-image"
        srcSet={`${previewImage} 2x`}
        alt=""
      />
    );
  } else if (!previewReady) {
    previewContent = null;
  } else if (previewItem?.id === previewId && previewItem.found) {
    previewContent = (
      <div
        className="spaces-preview-item"
        style={{
          backgroundImage: `-moz-element(#${PREVIEW_IMAGE_ID})`,
        }}
      />
    );
  }

  useEffect(() => {
    setAnimate(true);
  }, []);

  useEffect(() => {
    if (focusActiveTab.current) {
      focusActiveTab.current = false;
      tablistRef.current
        ?.querySelector('[role="tab"][aria-selected="true"]')
        ?.focus();
    }
  }, [activeIndex]);

  const switchTo = useCallback(
    (index, method) => {
      if (index === activeIndex || index < 0 || index >= spaces.length) {
        return;
      }
      setActiveId(spaces[index].id);
      setPreviewInteracted(true);
      setPeekHover(null);
      dispatch(
        ac.OnlyToMain({
          type: at.SPACES_USER_EVENT,
          data: {
            space: spaces[index].id,
            previous_space: spaces[activeIndex].id,
            method,
          },
        })
      );
    },
    [activeIndex, spaces, dispatch]
  );

  const onTabKeyDown = useCallback(
    event => {
      const offsets = { ArrowLeft: -1, ArrowRight: 1 };
      let next;
      if (event.key in offsets) {
        // The tablist is horizontal, so ArrowRight means the next tab visually.
        next = activeIndex + offsets[event.key] * (isRtl() ? -1 : 1);
      } else if (event.key === "Home") {
        next = 0;
      } else if (event.key === "End") {
        next = spaces.length - 1;
      } else {
        return;
      }
      if (next >= 0 && next < spaces.length) {
        event.preventDefault();
        focusActiveTab.current = true;
        switchTo(next, "keyboard");
      }
    },
    [activeIndex, spaces.length, switchTo]
  );

  // Trackpads report horizontal scroll as deltaX.
  const wheelTotal = useRef(0);
  const wheelSpent = useRef(false);
  const wheelTimer = useRef(null);
  useEffect(() => () => clearTimeout(wheelTimer.current), []);

  const onWheel = useCallback(
    event => {
      // With shift held Gecko reports the scroll as deltaY and ignores deltaX
      // (mousewheel.with_shift.action = 4), so the axes swap.
      const across = event.shiftKey ? event.deltaY : event.deltaX;
      const along = event.shiftKey ? event.deltaX : event.deltaY;
      if (Math.abs(across) <= Math.abs(along)) {
        return;
      }
      clearTimeout(wheelTimer.current);
      wheelTimer.current = setTimeout(() => {
        wheelTotal.current = 0;
        wheelSpent.current = false;
      }, SWIPE_END_MS);

      if (wheelSpent.current) {
        return;
      }
      wheelTotal.current += across;
      if (Math.abs(wheelTotal.current) >= SWIPE_THRESHOLD_PX) {
        wheelSpent.current = true;
        // Scrolling right moves the content left, i.e. towards the next space.
        switchTo(
          activeIndex + Math.sign(wheelTotal.current) * (isRtl() ? -1 : 1),
          "swipe"
        );
      }
    },
    [activeIndex, switchTo]
  );

  // Touch: compare where the finger lifted against where it landed.
  const touchStart = useRef(null);

  const onTouchStart = useCallback(event => {
    const [touch] = event.touches;
    touchStart.current = touch ? { x: touch.clientX, y: touch.clientY } : null;
  }, []);

  const onTouchEnd = useCallback(
    event => {
      const start = touchStart.current;
      touchStart.current = null;
      const [touch] = event.changedTouches;
      if (!start || !touch) {
        return;
      }
      const deltaX = touch.clientX - start.x;
      const deltaY = touch.clientY - start.y;
      if (
        Math.abs(deltaX) < SWIPE_THRESHOLD_PX ||
        Math.abs(deltaX) <= Math.abs(deltaY)
      ) {
        return;
      }
      // Dragging left pulls the next space into view.
      switchTo(activeIndex - Math.sign(deltaX) * (isRtl() ? -1 : 1), "swipe");
    },
    [activeIndex, switchTo]
  );

  // Labelled with the space it leads to.
  const renderArrow = (offset, direction) => {
    // Wraps, so the first space's previous arrow leads to the last.
    const targetIndex = (activeIndex + offset + spaces.length) % spaces.length;
    const target = spaces[targetIndex];
    return (
      target && (
        <div className={`spaces-arrow-rail ${direction}`}>
          <moz-button
            class="spaces-arrow"
            type="primary"
            iconsrc={`chrome://global/skin/icons/shaft-arrow-${direction === "prev" ? "left" : "right"}.svg`}
            iconposition={direction === "prev" ? "end" : undefined}
            label={target.label}
            data-l10n-id={
              target.label ? undefined : SPACE_META[target.id].l10nId
            }
            // Marks keyboard focus for the CSS name reveal. A mouse click also
            // focuses the arrow, and moz-button does not expose :focus-visible,
            // so this asks its inner button instead.
            onFocus={event =>
              event.currentTarget.toggleAttribute(
                "focusvisible",
                !!event.currentTarget.shadowRoot?.activeElement?.matches(
                  ":focus-visible"
                )
              )
            }
            onBlur={event =>
              event.currentTarget.removeAttribute("focusvisible")
            }
            onClick={() => switchTo(targetIndex, "arrow")}
          />
        </div>
      )
    );
  };

  // The spaces on either side of the active one show a peek.
  const peekSideOf = index =>
    (peek && { [-1]: "prev", 1: "next" }[index - activeIndex]) || null;
  // Hovering a peek or its tab slides that peek in.
  const peekHoverHandlers = side => ({
    onMouseEnter: () => setPeekHover(side),
    onMouseLeave: () => setPeekHover(null),
  });

  // The peeked spaces show through in the margins, inert. These boxes over
  // them handle hover and click.
  const peekBoxes = spaces.map((space, index) => {
    const side = peekSideOf(index);
    return (
      side && (
        // Keyboard users switch with the tablist; this is a pointer shortcut.
        // eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions
        <div
          key={side}
          className={`spaces-peek-box ${side}`}
          aria-hidden="true"
          onClick={() => switchTo(index, "peek")}
          {...peekHoverHandlers(side)}
        />
      )
    );
  });

  return (
    <div
      className={`spaces-container${peekHover ? ` peek-hover-${peekHover}` : ""}`}
      ref={containerRef}
      onWheel={onWheel}
      onTouchStart={onTouchStart}
      onTouchEnd={onTouchEnd}
    >
      {arrows && (
        <>
          {renderArrow(-1, "prev")}
          {renderArrow(1, "next")}
        </>
      )}
      <div className="spaces-frame">
        {peekBoxes}
        {/* A real tablist, so arrow-key traversal comes with the pattern. */}
        {tablist && (
          <div className="spaces-tablist-slot">
            <div
              className="spaces-tablist"
              role="tablist"
              ref={tablistRef}
              onKeyDown={onTabKeyDown}
              onFocus={preview ? () => setPreviewInteracted(true) : undefined}
              onPointerLeave={preview ? () => setHoveredId(null) : undefined}
            >
              {spaces.map((space, index) => {
                const isActive = index === activeIndex;
                const peekSide = peekSideOf(index);
                return (
                  <button
                    key={space.id}
                    id={`spaces-tab-${space.id}`}
                    className={`spaces-tab${isActive ? " active" : ""}`}
                    role="tab"
                    type="button"
                    aria-selected={isActive}
                    aria-controls={`spaces-panel-${space.id}`}
                    tabIndex={isActive ? 0 : -1}
                    style={
                      preview
                        ? { "--spaces-tab-anchor": `--spaces-tab-${space.id}` }
                        : undefined
                    }
                    onPointerEnter={
                      preview
                        ? () => {
                            setHoveredId(space.id);
                            setPreviewInteracted(true);
                          }
                        : undefined
                    }
                    onClick={() =>
                      switchTo(index, preview ? "preview_tab" : "tab")
                    }
                    {...(peekSide ? peekHoverHandlers(peekSide) : {})}
                  >
                    {/* A thematic space carries its own label and icon from
                  the layout config; SPACE_META covers the V1 spaces, whose
                  labels are localized. An icon the config got wrong is simply
                  absent, and the label still names the tab. */}
                    {(space.icon ?? SPACE_META[space.id]?.iconsrc) && (
                      <img
                        className="spaces-tab-icon"
                        src={space.icon ?? SPACE_META[space.id].iconsrc}
                        data-icon-family={space.iconFamily}
                        alt=""
                      />
                    )}
                    {space.label ? (
                      <span>{space.label}</span>
                    ) : (
                      <span data-l10n-id={SPACE_META[space.id].l10nId} />
                    )}
                  </button>
                );
              })}
            </div>
            {/* After the tablist, as an anchored box must follow its anchor. */}
            {previewId && previewContent && (
              <div
                className="spaces-preview"
                aria-hidden="true"
                style={{
                  "--spaces-preview-anchor": `--spaces-tab-${previewId}`,
                }}
              >
                {previewContent}
              </div>
            )}
          </div>
        )}
        <div className={`spaces-track${animate ? " animate" : ""}`}>
          {spaces.map((space, index) => {
            const isActive = index === activeIndex;
            const peekSide = peekSideOf(index);
            return (
              <div
                key={space.id}
                id={`spaces-panel-${space.id}`}
                className={`space${isActive ? " active" : ""}${peekSide ? ` peeking peeking-${peekSide}` : ""}`}
                style={{ "--space-offset": index - activeIndex }}
                // Without the tablist there are no tabs to label the panels.
                role={tablist ? "tabpanel" : undefined}
                aria-labelledby={tablist ? `spaces-tab-${space.id}` : undefined}
                aria-hidden={isActive ? undefined : "true"}
                inert={!isActive}
              >
                <SpaceActiveContext.Provider value={isActive}>
                  {space.content}
                </SpaceActiveContext.Provider>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
