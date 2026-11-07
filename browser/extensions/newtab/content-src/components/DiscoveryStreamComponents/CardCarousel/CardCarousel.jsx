/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useEffect, useMemo, useRef, useState } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";

const PREF_CAROUSEL_PAUSED = "discoverystream.carousel.paused";
const AUTOPLAY_DELAY_MS = 5000;

// Whether focus arrived by keyboard rather than by a click. Walks to the
// innermost focused element, since :focus-visible doesn't match shadow hosts.
function focusArrivedFromKeyboard() {
  let element = document.activeElement;
  while (element?.shadowRoot?.activeElement) {
    element = element.shadowRoot.activeElement;
  }
  return !!element?.matches(":focus-visible");
}

/**
 * An auto-rotating carousel of recommended stories.
 *
 * @param {object} props
 * @param {object[]} props.recs Recommendations to show.
 * @param {string} props.labelledBy ID of the element naming the carousel.
 * @param {string} props.sectionClassNames Grid placement classes from the section layout.
 * @param {string} props.section Key of the section the carousel sits in.
 * @param {number} props.sectionPosition Position of that section on the page.
 * @param {Function} props.renderCard Builds a card: (rec, { isActive }) => element.
 * @param {Function} props.dispatch Redux dispatch.
 */
export function CardCarousel({
  recs,
  labelledBy,
  sectionClassNames = "",
  section,
  sectionPosition,
  renderCard,
  dispatch,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const containerRef = useRef(null);
  const rotationControlRef = useRef(null);

  const [slideIndex, setSlideIndex] = useState(0);
  const [isHovered, setIsHovered] = useState(false);
  // Per the ARIA carousel pattern, keyboard focus on the carousel pauses
  // rotation until focus leaves again. The rotation control itself is exempt.
  const [keyboardFocusWithin, setKeyboardFocusWithin] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [documentHidden, setDocumentHidden] = useState(document.hidden);
  // Reduced motion starts the carousel with rotation paused, but the user can
  // manually press play. Not persisted: the carousel should be paused again
  // on the next new tab.
  const [userStartedRotation, setUserStartedRotation] = useState(false);
  const motionQuery = useMemo(
    () => window.matchMedia("(prefers-reduced-motion: reduce)"),
    []
  );

  const slideCount = recs.length;
  // Fewer recommendations can arrive than the current index points at.
  const activeIndex = Math.min(slideIndex, Math.max(slideCount - 1, 0));

  const pausedByPref = !!prefs[PREF_CAROUSEL_PAUSED];
  const paused = pausedByPref || (motionQuery.matches && !userStartedRotation);
  const isPlaying =
    slideCount > 1 &&
    !paused &&
    !isHovered &&
    !keyboardFocusWithin &&
    !menuOpen &&
    !documentHidden;

  // Re-armed on every slide change, so manual navigation also resets the delay.
  useEffect(() => {
    if (!isPlaying) {
      return undefined;
    }
    const timer = setTimeout(
      () => setSlideIndex((activeIndex + 1) % slideCount),
      AUTOPLAY_DELAY_MS
    );
    return () => clearTimeout(timer);
  }, [isPlaying, activeIndex, slideCount]);

  // Hold rotation while a card's context menu is open: the menu extends past
  // the carousel, so moving the pointer into it would end the hover pause.
  // DSCard signals this with a class, hence the observer.
  useEffect(() => {
    const el = containerRef.current;
    if (!el) {
      return undefined;
    }
    const observer = new MutationObserver(() =>
      setMenuOpen(!!el.querySelector(".ds-card.active"))
    );
    observer.observe(el, {
      subtree: true,
      attributes: true,
      attributeFilter: ["class"],
    });
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    const onVisibilityChange = () => setDocumentHidden(document.hidden);
    document.addEventListener("visibilitychange", onVisibilityChange);
    return () =>
      document.removeEventListener("visibilitychange", onVisibilityChange);
  }, []);

  const goToSlide = (nextIndex, direction) => {
    setSlideIndex(nextIndex);
    dispatch(
      ac.OnlyToMain({
        type: at.CAROUSEL_NAVIGATE,
        data: {
          direction,
          slide_index: nextIndex,
          section,
          section_position: sectionPosition,
        },
      })
    );
  };

  const goToPrevious = () =>
    goToSlide((activeIndex - 1 + slideCount) % slideCount, "previous");
  const goToNext = () => goToSlide((activeIndex + 1) % slideCount, "next");

  const toggleRotation = () => {
    const nextPaused = !paused;
    setUserStartedRotation(!nextPaused);
    dispatch(ac.SetPref(PREF_CAROUSEL_PAUSED, nextPaused));
    dispatch(
      ac.OnlyToMain({
        type: at.CAROUSEL_TOGGLE_AUTOPLAY,
        data: {
          paused: nextPaused,
          section,
          section_position: sectionPosition,
        },
      })
    );
  };

  if (!slideCount) {
    return null;
  }

  return (
    <div
      className={`ds-carousel ${sectionClassNames}`}
      ref={containerRef}
      role="group"
      aria-labelledby={labelledBy}
      // mouseover/mouseout rather than enter/leave: they bubble, so hovering
      // the rotation control can be told apart from hovering a card. As with
      // keyboard focus, the control is exempt from pausing.
      onMouseOver={event =>
        setIsHovered(!rotationControlRef.current?.contains(event.target))
      }
      onMouseOut={event => {
        if (!event.currentTarget.contains(event.relatedTarget)) {
          setIsHovered(false);
        }
      }}
      onFocusCapture={event =>
        setKeyboardFocusWithin(
          focusArrivedFromKeyboard() &&
            !rotationControlRef.current?.contains(event.target)
        )
      }
      onBlurCapture={event => {
        if (!event.currentTarget.contains(event.relatedTarget)) {
          setKeyboardFocusWithin(false);
        }
      }}
    >
      {slideCount > 1 && (
        <moz-button
          ref={rotationControlRef}
          size="small"
          className="ds-carousel-rotation-control"
          iconSrc={`resource://content-accessible/media/${
            paused ? "play" : "pause"
          }-fill.svg`}
          data-l10n-id={
            paused ? "newtab-carousel-play" : "newtab-carousel-pause"
          }
          onClick={toggleRotation}
        ></moz-button>
      )}
      {slideCount > 1 && (
        <>
          <moz-button
            type="primary"
            className="ds-carousel-nav ds-carousel-previous"
            iconSrc="chrome://global/skin/icons/arrow-left.svg"
            data-l10n-id="newtab-carousel-previous"
            onClick={goToPrevious}
          ></moz-button>
          <moz-button
            type="primary"
            className="ds-carousel-nav ds-carousel-next"
            iconSrc="chrome://global/skin/icons/arrow-right.svg"
            data-l10n-id="newtab-carousel-next"
            onClick={goToNext}
          ></moz-button>
        </>
      )}
      <div className="ds-carousel-slides">
        {recs.map((rec, index) => {
          const isActive = index === activeIndex;
          return (
            <div
              key={index}
              className={`ds-carousel-slide${isActive ? " is-active" : ""}`}
              role="group"
              data-l10n-id="newtab-carousel-slide"
              data-l10n-args={JSON.stringify({
                index: index + 1,
                total: slideCount,
              })}
              aria-hidden={!isActive}
              inert={!isActive}
            >
              {renderCard(rec, { isActive })}
            </div>
          );
        })}
      </div>
      {slideCount > 1 && (
        <div className="ds-carousel-stepper" aria-hidden="true">
          {recs.map((rec, index) => (
            <span
              key={index}
              className={`ds-carousel-dot${
                index === activeIndex ? " is-active" : ""
              }`}
            />
          ))}
        </div>
      )}
    </div>
  );
}
