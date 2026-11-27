/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { DSEmptyState } from "../DSEmptyState/DSEmptyState";
import { DSCard, PlaceholderDSCard } from "../DSCard/DSCard";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  useIntersectionObserver,
  getActiveColumnLayout,
  getNovaColumnLayout,
} from "../../../lib/utils";
import { shouldShowOMCHighlight } from "../../../lib/asrouter-message-utils.mjs";
import { SectionContextMenu } from "../SectionContextMenu/SectionContextMenu";
import { SectionFollowButton } from "../SectionFollowButton/SectionFollowButton";
import { InterestPicker } from "../InterestPicker/InterestPicker";
// @nova-cleanup(move-directory): Update import path after NovaInterestPicker moves to InterestPicker/
import { InterestPicker as NovaInterestPicker } from "content-src/components/Nova/InterestPicker/InterestPicker";
import { AdBanner } from "../AdBanner/AdBanner.jsx";
import { CardCarousel } from "../CardCarousel/CardCarousel";
import { TopicNavigation } from "../TopicNavigation/TopicNavigation";
import { PersonalizedCard } from "../PersonalizedCard/PersonalizedCard";
import { FollowSectionButtonHighlight } from "../FeatureHighlight/FollowSectionButtonHighlight";
import { MessageWrapper } from "content-src/components/MessageWrapper/MessageWrapper";
import { ErrorBoundary } from "content-src/components/ErrorBoundary/ErrorBoundary";
import { WIDGET_ROW_COMPONENTS } from "content-src/components/Widgets/WidgetsComponentRegistry.jsx";
import {
  WIDGET_LOCATIONS,
  WidgetLocationContext,
} from "content-src/components/Widgets/WidgetPlacement";

// Prefs
const PREF_SECTIONS_CARDS_ENABLED = "discoverystream.sections.cards.enabled";
const PREF_SECTIONS_PERSONALIZATION_ENABLED =
  "discoverystream.sections.personalization.enabled";
const PREF_TOPICS_SELECTED = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPICS_AVAILABLE = "discoverystream.topicSelection.topics";
const PREF_INTEREST_PICKER_ENABLED =
  "discoverystream.sections.interestPicker.enabled";
const PREF_VISIBLE_SECTIONS =
  "discoverystream.sections.interestPicker.visibleSections";
const PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const PREF_INFERRED_PERSONALIZATION_USER =
  "discoverystream.sections.personalization.inferred.user.enabled";
const PREF_SPOCS_STARTUPCACHE_ENABLED =
  "discoverystream.spocs.startupCache.enabled";
const PREF_CAROUSEL_ENABLED = "discoverystream.carousel.enabled";
const PREF_CAROUSEL_SLIDE_COUNT = "discoverystream.carousel.slideCount";
const PREF_EDITORS_PICK_SHOW_BADGE = "discoverystream.editorsPick.showBadge";
const PREF_TOPIC_NAVIGATION_ENABLED =
  "discoverystream.sections.topicNavigation.enabled";
// @nova-cleanup(remove-pref): Remove PREF_NOVA_ENABLED
const PREF_NOVA_ENABLED = "nova.enabled";

// Divides evenly by 2, 3, and 4 to avoid orphan cards in any column layout.
const DEFAULT_MAX_TILES = 12;

// Each card's footprint in grid units, sized so a medium reads as the square it
// is: a small is half a medium (2x1), a large is two mediums wide (4x2). A grid
// column is 2 units wide, so a full row spans columnCount * 2 units.
const CARD_SIZE = {
  small: { width: 2, height: 1 },
  medium: { width: 2, height: 2 },
  large: { width: 4, height: 2 },
};

const sizeOf = tile => CARD_SIZE[tile.size] ?? CARD_SIZE.medium;

// Return the tileIndexes that fall into an incomplete final row at this
// breakpoint. These are the orphan cards to hide.
function getOrphanTileIndexes(tiles, columnCount) {
  const rowWidth = columnCount * 2; // a grid column is 2 units wide
  let currentRow = []; // tile indexes in the row we're filling
  let filled = 0; // units used in this row, including tall cards from above
  let carry = 0; // units this row's tall cards reserve in the next row

  // Walks each tile, filling currentRow until the row is filled,
  // then clears it and goes to the next row.
  // At the end if currentRow is not empty, we have a remainder.
  tiles.forEach((tile, index) => {
    const { width, height } = sizeOf(tile);
    currentRow.push(index);
    filled += width;
    if (height > 1) {
      carry += width;
    }

    // This row is complete, we can reset and keep looking.
    if (filled >= rowWidth) {
      currentRow = [];
      filled = carry; // next row starts seeded by tall cards from above
      carry = 0;
      // The carry alone filled the whole next row (all-tall row), so reset it.
      if (filled >= rowWidth) {
        filled = 0;
      }
    }
  });

  return new Set(currentRow);
}

function getLayoutData(responsiveLayouts, index) {
  let layoutData = {
    classNames: [],
    imageSizes: {},
    cardPositions: {},
    isCarousel: false,
    hiddenColumnCounts: new Set(),
  };

  responsiveLayouts.forEach(layout => {
    const orphanTiles = getOrphanTileIndexes(layout.tiles, layout.columnCount);
    let hasTile = false;
    layout.tiles.forEach((tile, tileIndex) => {
      if (tile.position === index) {
        hasTile = true;
        if (tile.carousel) {
          layoutData.isCarousel = true;
        }

        if (orphanTiles.has(tileIndex)) {
          layoutData.hiddenColumnCounts.add(layout.columnCount);
        }
        layoutData.classNames.push(`col-${layout.columnCount}-${tile.size}`);
        layoutData.classNames.push(
          `col-${layout.columnCount}-position-${tileIndex}`
        );
        layoutData.imageSizes[layout.columnCount] = tile.size;
        layoutData.cardPositions[layout.columnCount] = tileIndex;

        // The API tells us whether the tile should show the excerpt or not.
        // Apply extra styles accordingly.
        if (tile.hasExcerpt) {
          if (tile.size === "medium") {
            layoutData.classNames.push(
              `col-${layout.columnCount}-hide-excerpt`
            );
          } else {
            layoutData.classNames.push(
              `col-${layout.columnCount}-show-excerpt`
            );
          }
        } else {
          layoutData.classNames.push(`col-${layout.columnCount}-hide-excerpt`);
        }
      }
    });

    // Bug 2069430: Different breakpoints can contain a different number of
    // cards. The render is sized to the breakpoint with the most tiles, so
    // a breakpoint with no tile at this position hides the extra card.
    if (!hasTile) {
      layoutData.hiddenColumnCounts.add(layout.columnCount);
    }
  });

  layoutData.hiddenColumnCounts.forEach(columnCount => {
    layoutData.classNames.push(`col-${columnCount}-hidden`);
  });

  return layoutData;
}

// function to determine amount of tiles shown per section per viewport
function getMaxTiles(responsiveLayouts) {
  return (
    responsiveLayouts
      .flatMap(responsiveLayout => responsiveLayout)
      .reduce((max, t) => Math.max(max, t.tiles.length), 0) || DEFAULT_MAX_TILES
  );
}

// How many stories the section consumes. This is more than the tile
// count when a section contains a carousel.
function getMaxRecs(responsiveLayouts, slideCount) {
  return (
    responsiveLayouts.reduce(
      (max, layout) =>
        Math.max(
          max,
          layout.tiles.reduce(
            (total, tile) => total + (tile.carousel ? slideCount : 1),
            0
          )
        ),
      0
    ) || DEFAULT_MAX_TILES
  );
}

/**
 * Transforms a comma-separated string in user preferences
 * into a cleaned-up array.
 *
 * @param {string} pref - The comma-separated pref to be converted.
 * @returns {string[]} An array of trimmed strings, excluding empty values.
 */

const prefToArray = (pref = "") => {
  return pref
    .split(",")
    .map(item => item.trim())
    .filter(item => item);
};

function CardSection({
  sectionPosition,
  section,
  dispatch,
  type,
  ctaButtonVariant,
  ctaButtonSponsors,
  anySectionsFollowed,
  spocsLoading,
  activeColumnLayout,
  syncLayoutOnFocus,
  gridRef,
}) {
  const prefs = useSelector(state => state.Prefs.values);

  const Messages = useSelector(state => state.Messages);
  const { messageData } = Messages;

  const { sectionPersonalization } = useSelector(
    state => state.DiscoveryStream
  );
  const { isForStartupCache } = useSelector(state => state.App);

  const [focusedPosition, setFocusedPosition] = useState(0);

  const onCardFocus = position => {
    setFocusedPosition(position);
  };

  const handleCardKeyDown = e => {
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      e.preventDefault();

      const currentCardEl = e.target.closest("article.ds-card");
      if (!currentCardEl) {
        return;
      }

      // Arrow direction should match visual navigation direction in RTL
      const isRTL = document.dir === "rtl";
      const navigateToPrevious = isRTL
        ? e.key === "ArrowRight"
        : e.key === "ArrowLeft";

      // Extract current position from classList
      let currentPosition = null;
      const positionPrefix = `${activeColumnLayout}-position-`;
      for (let className of currentCardEl.classList) {
        if (className.startsWith(positionPrefix)) {
          currentPosition = parseInt(
            className.substring(positionPrefix.length),
            10
          );
          break;
        }
      }

      if (currentPosition === null) {
        return;
      }

      const targetPosition = navigateToPrevious
        ? currentPosition - 1
        : currentPosition + 1;

      // Find card with target position. Searching from the grid rather than the
      // card's parent lets a card nested in a carousel reach the rest of the row.
      const positionClass = `${activeColumnLayout}-position-${targetPosition}`;
      const targetSelector = `:scope > article.ds-card.${positionClass}`;
      const carouselSelector = `:scope > .ds-carousel.${positionClass} .ds-carousel-slide.is-active article.ds-card`;
      const targetCardEl =
        e.currentTarget.querySelector(targetSelector) ||
        e.currentTarget.querySelector(carouselSelector);

      if (targetCardEl) {
        const link = targetCardEl.querySelector("a.ds-card-link");
        if (link) {
          link.focus();
        }
      }
    }
  };

  // @experiment(remove-conditional) { bug 2078816 }
  // Keep for spaces-thematic-v1.
  const handleWidgetInteraction = widgetName => {
    const prefName = `widgets.${widgetName}.interaction`;
    if (prefs[prefName] === false) {
      dispatch(ac.SetPref(prefName, true));
    }
  };

  const mayHaveSectionsCards = prefs[PREF_SECTIONS_CARDS_ENABLED];
  const selectedTopics = prefs[PREF_TOPICS_SELECTED];
  const availableTopics = prefs[PREF_TOPICS_AVAILABLE];
  const spocsStartupCacheEnabled = prefs[PREF_SPOCS_STARTUPCACHE_ENABLED];

  const mayHaveSectionsPersonalization =
    prefs[PREF_SECTIONS_PERSONALIZATION_ENABLED];
  // @nova-cleanup(remove-conditional): Remove novaEnabled, always use Nova layout
  const novaEnabled = prefs[PREF_NOVA_ENABLED];
  // @nova-cleanup(remove-conditional): Drop the novaEnabled check
  const carouselEnabled =
    (prefs.trainhopConfig?.carousel?.enabled || prefs[PREF_CAROUSEL_ENABLED]) &&
    novaEnabled;
  const carouselSlideCount =
    prefs.trainhopConfig?.carousel?.slideCount ??
    prefs[PREF_CAROUSEL_SLIDE_COUNT];
  const editorsPickShowBadge =
    prefs.trainhopConfig?.editorsPick?.showBadge ??
    prefs[PREF_EDITORS_PICK_SHOW_BADGE];

  const { sectionKey, title, subtitle, followable } = section;
  const { responsiveLayouts, name: layoutName } = section.layout;
  // A carousel takes its accessible name from the section heading.
  const sectionTitleId = `section-title-${sectionKey}`;

  const following = sectionPersonalization[sectionKey]?.isFollowed;

  const handleIntersection = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.CARD_SECTION_IMPRESSION,
        data: {
          section: sectionKey,
          section_position: sectionPosition,
          is_section_followed: following,
          layout_name: layoutName,
        },
      })
    );
  }, [dispatch, sectionKey, sectionPosition, following, layoutName]);

  // Ref to hold the section element
  const sectionRefs = useIntersectionObserver(handleIntersection);

  const onFollowClick = useCallback(() => {
    const updatedSectionData = {
      ...sectionPersonalization,
      [sectionKey]: {
        isFollowed: true,
        isBlocked: false,
        followedAt: new Date().toISOString(),
      },
    };
    dispatch(
      ac.AlsoToMain({
        type: at.SECTION_PERSONALIZATION_SET,
        data: updatedSectionData,
      })
    );
    // Telemetry Event Dispatch
    dispatch(
      ac.OnlyToMain({
        type: "FOLLOW_SECTION",
        data: {
          section: sectionKey,
          section_position: sectionPosition,
          event_source: "MOZ_BUTTON",
        },
      })
    );
    dispatch(
      ac.OnlyToOneContent(
        {
          type: at.SHOW_TOAST_MESSAGE,
          data: {
            toastId: "followSectionToast",
            showNotifications: true,
            toastData: { l10nId: "newtab-section-toast-follow", topic: title },
          },
        },
        "ActivityStream:Content"
      )
    );
  }, [dispatch, sectionPersonalization, sectionKey, sectionPosition, title]);

  const onUnfollowClick = useCallback(() => {
    const updatedSectionData = { ...sectionPersonalization };
    delete updatedSectionData[sectionKey];
    dispatch(
      ac.AlsoToMain({
        type: at.SECTION_PERSONALIZATION_SET,
        data: updatedSectionData,
      })
    );

    // Telemetry Event Dispatch
    dispatch(
      ac.OnlyToMain({
        type: "UNFOLLOW_SECTION",
        data: {
          section: sectionKey,
          section_position: sectionPosition,
          event_source: "MOZ_BUTTON",
        },
      })
    );
    dispatch(
      ac.OnlyToOneContent(
        {
          type: at.SHOW_TOAST_MESSAGE,
          data: {
            toastId: "unfollowSectionToast",
            showNotifications: true,
            toastData: {
              l10nId: "newtab-section-toast-unfollow",
              topic: title,
            },
          },
        },
        "ActivityStream:Content"
      )
    );
  }, [dispatch, sectionPersonalization, sectionKey, sectionPosition, title]);

  let maxTile = DEFAULT_MAX_TILES;
  let maxRecs = DEFAULT_MAX_TILES;
  if (!spocsLoading) {
    maxTile = getMaxTiles(responsiveLayouts);
    maxRecs = getMaxRecs(responsiveLayouts, carouselSlideCount);
  }

  const displaySections = section.data.slice(0, maxRecs);
  const isSectionEmpty = !displaySections?.length;

  if (isSectionEmpty) {
    return null;
  }

  function renderDSCard({
    rec,
    key,
    classNames,
    imageSizes,
    tabIndex,
    onFocus,
    isActive,
  }) {
    return (
      <DSCard
        key={key}
        pos={rec.pos}
        flightId={rec.flight_id}
        image_src={rec.image_src}
        raw_image_src={rec.raw_image_src}
        icon_src={rec.icon_src}
        word_count={rec.word_count}
        time_to_read={rec.time_to_read}
        title={rec.title}
        topic={rec.topic}
        features={rec.features}
        excerpt={rec.excerpt}
        url={rec.url}
        id={rec.id}
        shim={rec.shim}
        type={type}
        context={rec.context}
        sponsor={rec.sponsor}
        sponsored_by_override={rec.sponsored_by_override}
        dispatch={dispatch}
        source={rec.domain}
        publisher={rec.publisher}
        pocket_id={rec.pocket_id}
        context_type={rec.context_type}
        bookmarkGuid={rec.bookmarkGuid}
        corpus_item_id={rec.corpus_item_id}
        scheduled_corpus_item_id={rec.scheduled_corpus_item_id}
        recommended_at={rec.recommended_at}
        received_rank={rec.received_rank}
        variant_id={rec.variant_id}
        source_section_id={rec.source_section_id}
        format={rec.format}
        is_ad_eligible_position={rec.is_ad_eligible_position}
        showEditorsPickBadge={!!rec.is_editors_pick && editorsPickShowBadge}
        alt_text={rec.alt_text}
        mayHaveSectionsCards={mayHaveSectionsCards}
        selectedTopics={selectedTopics}
        availableTopics={availableTopics}
        ctaButtonSponsors={ctaButtonSponsors}
        ctaButtonVariant={ctaButtonVariant}
        sectionsClassNames={classNames.join(" ")}
        sectionsCardImageSizes={imageSizes}
        section={sectionKey}
        sectionPosition={sectionPosition}
        sectionFollowed={following}
        sectionLayoutName={layoutName}
        isTimeSensitive={rec.isTimeSensitive}
        tabIndex={tabIndex}
        onFocus={onFocus}
        attribution={rec.attribution}
        isActive={isActive}
      />
    );
  }

  // Render a placeholder card when:
  // 1. No recommendation is available.
  // 2. The item is flagged as a placeholder.
  // 3. Spocs are loading for with spocs startup cache disabled.
  // Shared so a carousel slide and a grid card agree on what counts as
  // renderable.
  function needsPlaceholder(rec) {
    return (
      !rec ||
      rec.placeholder ||
      spocsLoading ||
      (rec.flight_id &&
        !spocsStartupCacheEnabled &&
        isForStartupCache.DiscoveryStream)
    );
  }

  function buildCards() {
    const cards = [];
    let dataIndex = 0;
    const activeColumnCount = parseInt(
      activeColumnLayout.replace("col-", ""),
      10
    );
    const activeFocusPositions = [];

    for (let position = 0; position < maxTile; position++) {
      const layoutData = getLayoutData(responsiveLayouts, position);
      const { classNames, imageSizes, cardPositions } = layoutData;

      if (dataIndex >= displaySections.length) {
        break;
      }

      if (layoutData.isCarousel && carouselEnabled) {
        const slice = displaySections.slice(
          dataIndex,
          dataIndex + carouselSlideCount
        );
        // Consume the whole slice, including anything filtered out below, so
        // the tiles after the carousel don't re-show one it already took.
        dataIndex += slice.length;

        const carouselRecs = slice.filter(rec => !needsPlaceholder(rec));
        if (!carouselRecs.length) {
          cards.push(
            <PlaceholderDSCard key={`carousel-placeholder-${position}`} />
          );
          continue;
        }

        cards.push(
          <CardCarousel
            key={`carousel-${position}`}
            recs={carouselRecs}
            labelledBy={sectionTitleId}
            sectionClassNames={classNames.join(" ")}
            section={sectionKey}
            sectionPosition={sectionPosition}
            dispatch={dispatch}
            renderCard={(rec, { isActive }) =>
              renderDSCard({
                rec,
                key: `dscard-${rec.id}`,
                classNames,
                imageSizes,
                isActive,
              })
            }
          />
        );
        continue;
      }

      const rec = displaySections[dataIndex];

      // @experiment(remove-conditional) { bug 2078816 }
      // Keep for spaces-thematic-v1.
      if (rec?.type === "widget") {
        const Widget = WIDGET_ROW_COMPONENTS[rec.widgetId];
        cards.push(
          <div
            key={`section-widget-${rec.widgetId}`}
            className={`section-widget ${classNames.join(" ")}`}
          >
            <WidgetLocationContext.Provider value={WIDGET_LOCATIONS.STORY_CARD}>
              <ErrorBoundary className="widget-error-fallback">
                <Widget
                  dispatch={dispatch}
                  handleUserInteraction={handleWidgetInteraction}
                />
              </ErrorBoundary>
            </WidgetLocationContext.Provider>
          </div>
        );
        dataIndex++;
        continue;
      }

      const currentIndex = dataIndex;
      const mappedFocusPosition = cardPositions[activeColumnCount];
      // Fall back to card order when this layout does not define a mapped position.
      const activeFocusPosition = Number.isInteger(mappedFocusPosition)
        ? mappedFocusPosition
        : currentIndex;

      // If the card is an orphan or the breakpoint has no tile for it, CSS hides it.
      // A hidden card must stay out of the roving tabindex for keyboard navigation.
      const isHidden = layoutData.hiddenColumnCounts.has(activeColumnCount);
      const isPlaceholder = needsPlaceholder(rec);

      if (isPlaceholder) {
        cards.push(<PlaceholderDSCard key={`dscard-${currentIndex}`} />);
      } else {
        if (!isHidden) {
          activeFocusPositions.push(activeFocusPosition);
        }
        cards.push({
          isDSCard: true,
          key: `dscard-${rec.id}`,
          rec,
          classNames,
          imageSizes,
          activeFocusPosition,
          isHidden,
        });
      }
      dataIndex++;
    }

    const uniqueFocusPositions = [...new Set(activeFocusPositions)].sort(
      (a, b) => a - b
    );
    const activeRovingIndex = uniqueFocusPositions.includes(focusedPosition)
      ? focusedPosition
      : uniqueFocusPositions[0];

    return cards.map(card => {
      if (!card.isDSCard) {
        return card;
      }

      const { rec, classNames, imageSizes, activeFocusPosition, isHidden } =
        card;

      return renderDSCard({
        rec,
        key: card.key,
        classNames,
        imageSizes,
        tabIndex:
          !isHidden && activeFocusPosition === activeRovingIndex ? 0 : -1,
        onFocus: () => onCardFocus(activeFocusPosition),
      });
    });
  }

  const cards = buildCards();

  const sectionContextWrapper = (
    <div className="section-context-wrapper">
      <div
        className={following ? "section-follow following" : "section-follow"}
      >
        {followable !== false &&
          !anySectionsFollowed &&
          sectionPosition === 0 &&
          shouldShowOMCHighlight(Messages, "FollowSectionButtonHighlight") && (
            <MessageWrapper dispatch={dispatch}>
              <FollowSectionButtonHighlight
                verticalPosition="inset-block-center"
                position="arrow-inline-start"
                dispatch={dispatch}
                feature="FEATURE_FOLLOW_SECTION_BUTTON"
                messageData={messageData}
              />
            </MessageWrapper>
          )}
        {followable !== false &&
          !anySectionsFollowed &&
          sectionPosition === 0 &&
          shouldShowOMCHighlight(
            Messages,
            "FollowSectionButtonAltHighlight"
          ) && (
            <MessageWrapper dispatch={dispatch}>
              <FollowSectionButtonHighlight
                verticalPosition="inset-block-center"
                position="arrow-inline-start"
                dispatch={dispatch}
                feature="FEATURE_ALT_FOLLOW_SECTION_BUTTON"
              />
            </MessageWrapper>
          )}
        {followable !== false && (
          <moz-button
            onClick={following ? onUnfollowClick : onFollowClick}
            type="default"
            index={sectionPosition}
            section={sectionKey}
          >
            <span
              className="section-button-follow-text"
              data-l10n-id="newtab-section-follow-button"
            />
            <span
              className="section-button-following-text"
              data-l10n-id="newtab-section-following-button"
            />
            <span
              className="section-button-unfollow-text"
              data-l10n-id="newtab-section-unfollow-button"
            />
          </moz-button>
        )}
      </div>
      <SectionContextMenu
        dispatch={dispatch}
        index={sectionPosition}
        following={following}
        sectionPersonalization={sectionPersonalization}
        sectionKey={sectionKey}
        title={title}
        type={type}
        sectionPosition={sectionPosition}
        learnMoreUrl={prefs["sectionsLearnMore.url"]}
      />
    </div>
  );
  return (
    <section
      className="ds-section"
      ref={el => {
        sectionRefs.current[0] = el;
      }}
    >
      <div className="section-heading">
        <div className="section-title-wrapper">
          <h2 className="section-title" id={sectionTitleId}>
            {title}
          </h2>
          {mayHaveSectionsPersonalization &&
            novaEnabled &&
            followable !== false && (
              <SectionFollowButton
                following={following}
                onFollowClick={onFollowClick}
                onUnfollowClick={onUnfollowClick}
                title={title}
              />
            )}
          {subtitle && <p className="section-subtitle">{subtitle}</p>}
        </div>
        {mayHaveSectionsPersonalization &&
          (novaEnabled ? (
            <SectionContextMenu
              dispatch={dispatch}
              index={sectionPosition}
              following={following}
              sectionPersonalization={sectionPersonalization}
              sectionKey={sectionKey}
              title={title}
              type={type}
              sectionPosition={sectionPosition}
              buttonType="ghost"
              learnMoreUrl={prefs["sectionsLearnMore.url"]}
            />
          ) : (
            sectionContextWrapper
          ))}
      </div>
      <div
        ref={gridRef}
        className={`ds-section-grid ds-card-grid`}
        onFocusCapture={syncLayoutOnFocus}
        onKeyDown={handleCardKeyDown}
      >
        {cards}
      </div>
    </section>
  );
}

/**
 * Sends the rendered section order to the parent so telemetry that goes
 * through randomizeOrganicContentEvent can report a real section_position.
 *
 * @param {object} props
 * @param {string} props.sectionKeys Comma-joined sectionKeys in render order.
 */
function SectionsOrderReporter({ sectionKeys, dispatch }) {
  useEffect(() => {
    dispatch(
      ac.OnlyToMain({
        type: at.CARD_SECTIONS_ORDER,
        data: { sections: sectionKeys.split(",") },
      })
    );
  }, [dispatch, sectionKeys]);
  return null;
}

function CardSections({
  data,
  feed,
  dispatch,
  type,
  ctaButtonVariant,
  ctaButtonSponsors,
  spocsLoading,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const { spocs, sectionPersonalization } = useSelector(
    state => state.DiscoveryStream
  );
  const Messages = useSelector(state => state.Messages);
  const { messageData } = Messages;
  const personalizationEnabled = prefs[PREF_SECTIONS_PERSONALIZATION_ENABLED];
  const interestPickerEnabled = prefs[PREF_INTEREST_PICKER_ENABLED];
  // @nova-cleanup(remove-conditional): Remove novaEnabled check once classic path is gone
  const novaEnabled = prefs[PREF_NOVA_ENABLED];
  // @nova-cleanup(remove-conditional): Drop the novaEnabled check
  const topicNavigationEnabled =
    (prefs.trainhopConfig?.topicNavigation?.enabled ||
      prefs[PREF_TOPIC_NAVIGATION_ENABLED]) &&
    novaEnabled;
  const gridRef = useRef(null);
  const layoutObserverRef = useRef(null);
  const [activeColumnLayout, setActiveColumnLayout] = useState(() =>
    getActiveColumnLayout(window.innerWidth)
  );

  useLayoutEffect(() => {
    if (!novaEnabled || !gridRef.current) {
      return undefined;
    }
    const columnLayout = getNovaColumnLayout(gridRef.current);
    if (columnLayout) {
      setActiveColumnLayout(columnLayout);
    }
    return () => layoutObserverRef.current?.disconnect();
  }, [novaEnabled]);

  const syncLayoutOnFocus = useCallback(
    e => {
      const grid = gridRef.current ?? e.currentTarget;
      const syncLayout = () => {
        let nextLayout = getActiveColumnLayout(window.innerWidth);
        if (novaEnabled) {
          nextLayout = getNovaColumnLayout(grid);
        }
        if (!nextLayout) {
          return;
        }
        setActiveColumnLayout(currLayout =>
          currLayout === nextLayout ? currLayout : nextLayout
        );
      };

      syncLayout();

      // A stale activeColumnLayout can leave the roving tabindex on a card the
      // breakpoint hides. Observing the grid keeps it current. Observation
      // starts on first focus and continues once focus leaves, because a
      // resize then would otherwise go unnoticed.
      if (novaEnabled && !layoutObserverRef.current) {
        const observer = new ResizeObserver(syncLayout);
        observer.observe(grid);
        layoutObserverRef.current = observer;
      }
    },
    [novaEnabled]
  );

  // Handle a render before feed has been fetched by displaying nothing
  if (!data) {
    return null;
  }

  const visibleSections = prefToArray(prefs[PREF_VISIBLE_SECTIONS]);
  const { interestPicker } = data;

  // Used to determine if we should show FollowSectionButtonHighlight
  const anySectionsFollowed =
    sectionPersonalization &&
    Object.values(sectionPersonalization).some(section => section?.isFollowed);

  let sectionsData = data.sections;

  if (spocsLoading) {
    // To clean up the placeholder state for sections if the whole section is loading still.
    sectionsData = [
      {
        ...sectionsData[0],
        title: "",
        subtitle: "",
      },
      {
        ...sectionsData[1],
        title: "",
        subtitle: "",
      },
    ];
  }

  let filteredSections = sectionsData.filter(
    section => !sectionPersonalization[section.sectionKey]?.isBlocked
  );

  if (interestPickerEnabled && visibleSections.length) {
    filteredSections = visibleSections.reduce((acc, visibleSection) => {
      const found = filteredSections.find(
        ({ sectionKey }) => sectionKey === visibleSection
      );
      if (found) {
        acc.push(found);
      }
      return acc;
    }, []);
  }

  let sectionsToRender = filteredSections.map((section, sectionPosition) => (
    <CardSection
      key={`section-${section.sectionKey}`}
      sectionPosition={sectionPosition}
      section={section}
      dispatch={dispatch}
      type={type}
      ctaButtonVariant={ctaButtonVariant}
      ctaButtonSponsors={ctaButtonSponsors}
      anySectionsFollowed={anySectionsFollowed}
      spocsLoading={spocsLoading}
      activeColumnLayout={activeColumnLayout}
      syncLayoutOnFocus={syncLayoutOnFocus}
      gridRef={sectionPosition === 0 ? gridRef : undefined}
    />
  ));

  // Add a billboard/leaderboard IAB ad to the sectionsToRender array (if enabled/possible).
  const billboardEnabled = prefs[PREF_BILLBOARD_ENABLED];
  const leaderboardEnabled = prefs[PREF_LEADERBOARD_ENABLED];

  if (
    (billboardEnabled || leaderboardEnabled) &&
    spocs?.data?.newtab_spocs?.items
  ) {
    const spocToRender =
      spocs.data.newtab_spocs.items.find(
        ({ format }) => format === "leaderboard" && leaderboardEnabled
      ) ||
      spocs.data.newtab_spocs.items.find(
        ({ format }) => format === "billboard" && billboardEnabled
      );

    if (spocToRender && !spocs.blocked.includes(spocToRender.url)) {
      const row =
        spocToRender.format === "leaderboard"
          ? prefs[PREF_LEADERBOARD_POSITION]
          : prefs[PREF_BILLBOARD_POSITION];

      sectionsToRender.splice(
        // Math.min is used here to ensure the given row stays within the bounds of the sectionsToRender array.
        Math.min(sectionsToRender.length - 1, row),
        0,
        <AdBanner
          spoc={spocToRender}
          key={`dscard-${spocToRender.id}`}
          dispatch={dispatch}
          type={type}
          row={row}
          prefs={prefs}
        />
      );
    }
  }

  // Add the interest picker to the sectionsToRender array (if enabled/possible).
  if (
    interestPickerEnabled &&
    personalizationEnabled &&
    interestPicker?.sections
  ) {
    const index = interestPicker.receivedFeedRank - 1;

    // @nova-cleanup(remove-conditional): Remove novaEnabled check, always use NovaInterestPicker
    const InterestPickerComponent = novaEnabled
      ? NovaInterestPicker
      : InterestPicker;
    sectionsToRender.splice(
      // Math.min is used here to ensure the given row stays within the bounds of the sectionsToRender array.
      Math.min(sectionsToRender.length - 1, index),
      0,
      <InterestPickerComponent
        title={interestPicker.title}
        subtitle={interestPicker.subtitle}
        interests={interestPicker.sections || []}
        receivedFeedRank={interestPicker.receivedFeedRank}
      />
    );
  }

  function displayP13nCard() {
    if (messageData && Object.keys(messageData).length >= 1) {
      if (
        shouldShowOMCHighlight(Messages, "PersonalizedCard") &&
        prefs[PREF_INFERRED_PERSONALIZATION_USER]
      ) {
        const row = messageData.content.position;
        sectionsToRender.splice(
          row,
          0,
          <MessageWrapper dispatch={dispatch} onDismiss={() => {}}>
            <PersonalizedCard
              position={row}
              dispatch={dispatch}
              messageData={messageData}
            />
          </MessageWrapper>
        );
      }
    }
  }

  displayP13nCard();

  const isEmpty = sectionsToRender.length === 0;

  return isEmpty ? (
    <div className="ds-card-grid empty sections-empty">
      <DSEmptyState status={data.status} dispatch={dispatch} feed={feed} />
    </div>
  ) : (
    <div className="ds-section-wrapper">
      <SectionsOrderReporter
        sectionKeys={filteredSections.map(s => s.sectionKey).join(",")}
        dispatch={dispatch}
      />
      {topicNavigationEnabled && !spocsLoading && (
        <TopicNavigation sections={filteredSections} dispatch={dispatch} />
      )}
      {sectionsToRender}
    </div>
  );
}

export { CardSections, getOrphanTileIndexes };
