/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useState, useRef, useCallback } from "react";
import { useDispatch, useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useIntersectionObserver } from "../../../lib/utils";
const PREF_VISIBLE_SECTIONS =
  "discoverystream.sections.interestPicker.visibleSections";

/**
 * Shows a list of recommended topics with visual indication whether
 * the user follows some of the topics (active, blue, selected topics)
 * or is yet to do so (neutrally-coloured topics with a "plus" button).
 *
 * @returns {React.Element}
 */
function InterestPicker({ title, subtitle, interests, receivedFeedRank }) {
  const dispatch = useDispatch();
  const focusedRef = useRef(null);
  const focusRef = useRef(null);
  const [focusedIndex, setFocusedIndex] = useState(0);
  const prefs = useSelector(state => state.Prefs.values);
  const { sectionPersonalization } = useSelector(
    state => state.DiscoveryStream
  );
  const visibleSections = prefs[PREF_VISIBLE_SECTIONS]?.split(",")
    .map(item => item.trim())
    .filter(item => item);

  const handleIntersection = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.INLINE_SELECTION_IMPRESSION,
        data: {
          section_position: receivedFeedRank,
        },
      })
    );
  }, [dispatch, receivedFeedRank]);

  const ref = useIntersectionObserver(handleIntersection);

  const onKeyDown = useCallback(e => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      // prevent the page from scrolling up/down while navigating.
      e.preventDefault();
    }

    if (
      focusedRef.current?.nextSibling?.querySelector("input") &&
      e.key === "ArrowDown"
    ) {
      focusedRef.current.nextSibling.querySelector("input").tabIndex = 0;
      focusedRef.current.nextSibling.querySelector("input").focus();
    }
    if (
      focusedRef.current?.previousSibling?.querySelector("input") &&
      e.key === "ArrowUp"
    ) {
      focusedRef.current.previousSibling.querySelector("input").tabIndex = 0;
      focusedRef.current.previousSibling.querySelector("input").focus();
    }
  }, []);

  function onWrapperFocus() {
    focusRef.current?.addEventListener("keydown", onKeyDown);
  }
  function onWrapperBlur() {
    focusRef.current?.removeEventListener("keydown", onKeyDown);
  }
  function onItemFocus(index) {
    setFocusedIndex(index);
  }

  // Updates user preferences as they follow or unfollow topics
  // by selecting them from the list
  function handleChange(e, index) {
    const { name: topic, checked } = e.target;
    let updatedSections = { ...sectionPersonalization };
    if (checked) {
      updatedSections[topic] = {
        isFollowed: true,
        isBlocked: false,
        followedAt: new Date().toISOString(),
      };
      if (!visibleSections.includes(topic)) {
        // add section to visible sections and place after the inline picker
        // subtract 1 from the rank so that it is normalized with array index
        visibleSections.splice(receivedFeedRank - 1, 0, topic);
        dispatch(ac.SetPref(PREF_VISIBLE_SECTIONS, visibleSections.join(", ")));
      }
    } else {
      delete updatedSections[topic];
    }
    dispatch(
      ac.OnlyToMain({
        type: at.INLINE_SELECTION_CLICK,
        data: {
          topic,
          is_followed: checked,
          topic_position: index,
          section_position: receivedFeedRank,
        },
      })
    );
    dispatch(
      ac.AlsoToMain({
        type: at.SECTION_PERSONALIZATION_UPDATE,
        data: updatedSections,
      })
    );
  }
  return (
    <section
      className="inline-selection-wrapper ds-section"
      ref={el => {
        ref.current = [el];
      }}
    >
      <div className="section-heading">
        <div className="section-title-wrapper">
          <h2 className="section-title">{title}</h2>
          <p className="section-subtitle">{subtitle}</p>
        </div>
      </div>
      <ul
        className="topic-list"
        onFocus={onWrapperFocus}
        onBlur={onWrapperBlur}
        ref={focusRef}
      >
        {interests.map((interest, index) => {
          const checked =
            sectionPersonalization[interest.sectionId]?.isFollowed;
          return (
            <li
              key={interest.sectionId}
              ref={index === focusedIndex ? focusedRef : null}
            >
              <label>
                <input
                  type="checkbox"
                  id={interest.sectionId}
                  name={interest.sectionId}
                  checked={checked}
                  aria-checked={checked}
                  onChange={e => handleChange(e, index)}
                  tabIndex={index === focusedIndex ? 0 : -1}
                  onFocus={() => {
                    onItemFocus(index);
                  }}
                />
                <span className="topic-item-label">{interest.title || ""}</span>
                <div
                  className={`topic-item-icon icon ${checked ? "icon-check-filled" : "icon-add-circle-fill"}`}
                ></div>
              </label>
            </li>
          );
        })}
      </ul>
      <p className="learn-more-copy">
        <a
          href={prefs["support.url"]}
          data-l10n-id="newtab-topic-selection-privacy-link"
        />
      </p>
    </section>
  );
}

export { InterestPicker };
