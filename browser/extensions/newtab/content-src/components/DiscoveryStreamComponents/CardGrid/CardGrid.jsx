/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DSCard, PlaceholderDSCard } from "../DSCard/DSCard.jsx";
import { DSEmptyState } from "../DSEmptyState/DSEmptyState.jsx";
import { DSDismiss } from "content-src/components/DiscoveryStreamComponents/DSDismiss/DSDismiss";
import { TopicsWidget } from "../TopicsWidget/TopicsWidget.jsx";
import { ListFeed } from "../ListFeed/ListFeed.jsx";
import { SafeAnchor } from "../SafeAnchor/SafeAnchor";
import { AdBanner } from "../AdBanner/AdBanner.jsx";
import { FluentOrText } from "../../FluentOrText/FluentOrText.jsx";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import React, { useEffect, useState, useRef, useCallback } from "react";
import { connect, useSelector } from "react-redux";
const PREF_ONBOARDING_EXPERIENCE_DISMISSED =
  "discoverystream.onboardingExperience.dismissed";
const PREF_SECTIONS_CARDS_ENABLED = "discoverystream.sections.cards.enabled";
const PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_TOPICS_ENABLED = "discoverystream.topicLabels.enabled";
const PREF_TOPICS_SELECTED = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPICS_AVAILABLE = "discoverystream.topicSelection.topics";
const PREF_SPOCS_STARTUPCACHE_ENABLED =
  "discoverystream.spocs.startupCache.enabled";
const PREF_LIST_FEED_ENABLED = "discoverystream.contextualContent.enabled";
const PREF_LIST_FEED_SELECTED_FEED =
  "discoverystream.contextualContent.selectedFeed";
const PREF_FAKESPOT_ENABLED =
  "discoverystream.contextualContent.fakespot.enabled";
const PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const INTERSECTION_RATIO = 0.5;
const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";
const WIDGET_IDS = {
  TOPICS: 1,
};

export function DSSubHeader({ children }) {
  return (
    <div className="section-top-bar ds-sub-header">
      <h3 className="section-title-container">{children}</h3>
    </div>
  );
}

export function OnboardingExperience({ dispatch, windowObj = globalThis }) {
  const [dismissed, setDismissed] = useState(false);
  const [maxHeight, setMaxHeight] = useState(null);
  const heightElement = useRef(null);

  const onDismissClick = useCallback(() => {
    // We update this as state and redux.
    // The state update is for this newtab,
    // and the redux update is for other tabs, offscreen tabs, and future tabs.
    // We need the state update for this tab to support the transition.
    setDismissed(true);
    dispatch(ac.SetPref(PREF_ONBOARDING_EXPERIENCE_DISMISSED, true));
    dispatch(
      ac.DiscoveryStreamUserEvent({
        event: "BLOCK",
        source: "POCKET_ONBOARDING",
      })
    );
  }, [dispatch]);

  useEffect(() => {
    const resizeObserver = new windowObj.ResizeObserver(() => {
      if (heightElement.current) {
        setMaxHeight(heightElement.current.offsetHeight);
      }
    });

    const options = { threshold: INTERSECTION_RATIO };
    const intersectionObserver = new windowObj.IntersectionObserver(entries => {
      if (
        entries.some(
          entry =>
            entry.isIntersecting &&
            entry.intersectionRatio >= INTERSECTION_RATIO
        )
      ) {
        dispatch(
          ac.DiscoveryStreamUserEvent({
            event: "IMPRESSION",
            source: "POCKET_ONBOARDING",
          })
        );
        // Once we have observed an impression, we can stop for this instance of newtab.
        intersectionObserver.unobserve(heightElement.current);
      }
    }, options);

    const onVisibilityChange = () => {
      intersectionObserver.observe(heightElement.current);
      windowObj.document.removeEventListener(
        VISIBILITY_CHANGE_EVENT,
        onVisibilityChange
      );
    };

    if (heightElement.current) {
      resizeObserver.observe(heightElement.current);
      // Check visibility or setup a visibility event to make
      // sure we don't fire this for off screen pre loaded tabs.
      if (windowObj.document.visibilityState === VISIBLE) {
        intersectionObserver.observe(heightElement.current);
      } else {
        windowObj.document.addEventListener(
          VISIBILITY_CHANGE_EVENT,
          onVisibilityChange
        );
      }
      setMaxHeight(heightElement.current.offsetHeight);
    }

    // Return unmount callback to clean up observers.
    return () => {
      resizeObserver?.disconnect();
      intersectionObserver?.disconnect();
      windowObj.document.removeEventListener(
        VISIBILITY_CHANGE_EVENT,
        onVisibilityChange
      );
    };
  }, [dispatch, windowObj]);

  const style = {};
  if (dismissed) {
    style.maxHeight = "0";
    style.opacity = "0";
    style.transition = "max-height 0.26s ease, opacity 0.26s ease";
  } else if (maxHeight) {
    style.maxHeight = `${maxHeight}px`;
  }

  return (
    <div style={style}>
      <div className="ds-onboarding-ref" ref={heightElement}>
        <div className="ds-onboarding-container">
          <DSDismiss
            onDismissClick={onDismissClick}
            extraClasses={`ds-onboarding`}
          >
            <div>
              <header>
                <span className="icon icon-pocket" />
                <span data-l10n-id="newtab-pocket-onboarding-discover" />
              </header>
              <p data-l10n-id="newtab-pocket-onboarding-cta" />
            </div>
            <div className="ds-onboarding-graphic" />
          </DSDismiss>
        </div>
      </div>
    </div>
  );
}

// eslint-disable-next-line no-shadow
export function IntersectionObserver({
  children,
  windowObj = window,
  onIntersecting,
}) {
  const intersectionElement = useRef(null);

  useEffect(() => {
    let observer;
    if (!observer && onIntersecting && intersectionElement.current) {
      observer = new windowObj.IntersectionObserver(entries => {
        const entry = entries.find(e => e.isIntersecting);

        if (entry) {
          // Stop observing since element has been seen
          if (observer && intersectionElement.current) {
            observer.unobserve(intersectionElement.current);
          }

          onIntersecting();
        }
      });
      observer.observe(intersectionElement.current);
    }
    // Cleanup
    return () => observer?.disconnect();
  }, [windowObj, onIntersecting]);

  return <div ref={intersectionElement}>{children}</div>;
}

export function RecentSavesContainer({
  gridClassName = "",
  dispatch,
  windowObj = window,
  items = 3,
  source = "CARDGRID_RECENT_SAVES",
}) {
  const {
    recentSavesData,
    isUserLoggedIn,
    experimentData: { utmCampaign, utmContent, utmSource },
  } = useSelector(state => state.DiscoveryStream);

  const [visible, setVisible] = useState(false);
  const onIntersecting = useCallback(() => setVisible(true), []);

  useEffect(() => {
    if (visible) {
      dispatch(
        ac.AlsoToMain({
          type: at.DISCOVERY_STREAM_POCKET_STATE_INIT,
        })
      );
    }
  }, [visible, dispatch]);

  // The user has not yet scrolled to this section,
  // so wait before potentially requesting Pocket data.
  if (!visible) {
    return (
      <IntersectionObserver
        windowObj={windowObj}
        onIntersecting={onIntersecting}
      />
    );
  }

  // Intersection observer has finished, but we're not yet logged in.
  if (visible && !isUserLoggedIn) {
    return null;
  }

  let queryParams = `?utm_source=${utmSource}`;
  // We really only need to add these params to urls we own.
  if (utmCampaign && utmContent) {
    queryParams += `&utm_content=${utmContent}&utm_campaign=${utmCampaign}`;
  }

  function renderCard(rec, index) {
    const url = new URL(rec.url);
    const urlSearchParams = new URLSearchParams(queryParams);
    if (rec?.id && !url.href.match(/getpocket\.com\/read/)) {
      url.href = `https://getpocket.com/read/${rec.id}`;
    }

    for (let [key, val] of urlSearchParams.entries()) {
      url.searchParams.set(key, val);
    }

    return (
      <DSCard
        key={`dscard-${rec?.id || index}`}
        id={rec.id}
        pos={index}
        type={source}
        image_src={rec.image_src}
        raw_image_src={rec.raw_image_src}
        icon_src={rec.icon_src}
        word_count={rec.word_count}
        time_to_read={rec.time_to_read}
        title={rec.title}
        excerpt={rec.excerpt}
        url={url.href}
        source={rec.domain}
        isRecentSave={true}
        dispatch={dispatch}
      />
    );
  }

  function onMyListClicked() {
    dispatch(
      ac.DiscoveryStreamUserEvent({
        event: "CLICK",
        source: `${source}_VIEW_LIST`,
      })
    );
  }

  const recentSavesCards = [];
  // We fill the cards with a for loop over an inline map because
  // we want empty placeholders if there are not enough cards.
  for (let index = 0; index < items; index++) {
    const recentSave = recentSavesData[index];
    if (!recentSave) {
      recentSavesCards.push(<PlaceholderDSCard key={`dscard-${index}`} />);
    } else {
      recentSavesCards.push(
        renderCard(
          {
            id: recentSave.id,
            image_src: recentSave.top_image_url,
            raw_image_src: recentSave.top_image_url,
            word_count: recentSave.word_count,
            time_to_read: recentSave.time_to_read,
            title: recentSave.resolved_title || recentSave.given_title,
            url: recentSave.resolved_url || recentSave.given_url,
            domain: recentSave.domain_metadata?.name,
            excerpt: recentSave.excerpt,
          },
          index
        )
      );
    }
  }

  // We are visible and logged in.
  return (
    <>
      <DSSubHeader>
        <span className="section-title">
          <FluentOrText message="Recently Saved to your List" />
        </span>
        <SafeAnchor
          onLinkClick={onMyListClicked}
          className="section-sub-link"
          url={`https://getpocket.com/a${queryParams}`}
        >
          <FluentOrText message="View My List" />
        </SafeAnchor>
      </DSSubHeader>
      <div className={`ds-card-grid-recent-saves ${gridClassName}`}>
        {recentSavesCards}
      </div>
    </>
  );
}

export class _CardGrid extends React.PureComponent {
  // eslint-disable-next-line max-statements
  renderCards() {
    const prefs = this.props.Prefs.values;
    const {
      items,
      fourCardLayout,
      essentialReadsHeader,
      editorsPicksHeader,
      onboardingExperience,
      ctaButtonSponsors,
      ctaButtonVariant,
      spocMessageVariant,
      widgets,
      recentSavesEnabled,
      DiscoveryStream,
    } = this.props;

    const { saveToPocketCard, topicsLoading } = DiscoveryStream;
    const showRecentSaves = prefs.showRecentSaves && recentSavesEnabled;
    const isOnboardingExperienceDismissed =
      prefs[PREF_ONBOARDING_EXPERIENCE_DISMISSED];
    const mayHaveSectionsCards = prefs[PREF_SECTIONS_CARDS_ENABLED];
    const mayHaveThumbsUpDown = prefs[PREF_THUMBS_UP_DOWN_ENABLED];
    const showTopics = prefs[PREF_TOPICS_ENABLED];
    const selectedTopics = prefs[PREF_TOPICS_SELECTED];
    const availableTopics = prefs[PREF_TOPICS_AVAILABLE];
    const spocsStartupCacheEnabled = prefs[PREF_SPOCS_STARTUPCACHE_ENABLED];
    const listFeedEnabled = prefs[PREF_LIST_FEED_ENABLED];
    const listFeedSelectedFeed = prefs[PREF_LIST_FEED_SELECTED_FEED];
    const billboardEnabled = prefs[PREF_BILLBOARD_ENABLED];
    const leaderboardEnabled = prefs[PREF_LEADERBOARD_ENABLED];
    // filter out recs that should be in ListFeed
    const recs = this.props.data.recommendations
      .filter(item => !item.feedName)
      .slice(0, items);
    const cards = [];

    let essentialReadsCards = [];
    let editorsPicksCards = [];

    for (let index = 0; index < items; index++) {
      const rec = recs[index];
      cards.push(
        topicsLoading ||
          !rec ||
          rec.placeholder ||
          (rec.flight_id &&
            !spocsStartupCacheEnabled &&
            this.props.App.isForStartupCache) ? (
          <PlaceholderDSCard key={`dscard-${index}`} />
        ) : (
          <DSCard
            key={`dscard-${rec.id}`}
            pos={rec.pos}
            flightId={rec.flight_id}
            image_src={rec.image_src}
            raw_image_src={rec.raw_image_src}
            icon_src={rec.icon_src}
            word_count={rec.word_count}
            time_to_read={rec.time_to_read}
            title={rec.title}
            topic={rec.topic}
            showTopics={showTopics}
            selectedTopics={selectedTopics}
            availableTopics={availableTopics}
            excerpt={rec.excerpt}
            url={rec.url}
            id={rec.id}
            shim={rec.shim}
            fetchTimestamp={rec.fetchTimestamp}
            type={this.props.type}
            context={rec.context}
            sponsor={rec.sponsor}
            sponsored_by_override={rec.sponsored_by_override}
            dispatch={this.props.dispatch}
            source={rec.domain}
            publisher={rec.publisher}
            pocket_id={rec.pocket_id}
            context_type={rec.context_type}
            bookmarkGuid={rec.bookmarkGuid}
            is_collection={this.props.is_collection}
            saveToPocketCard={saveToPocketCard}
            ctaButtonSponsors={ctaButtonSponsors}
            ctaButtonVariant={ctaButtonVariant}
            spocMessageVariant={spocMessageVariant}
            recommendation_id={rec.recommendation_id}
            firstVisibleTimestamp={this.props.firstVisibleTimestamp}
            mayHaveThumbsUpDown={mayHaveThumbsUpDown}
            mayHaveSectionsCards={mayHaveSectionsCards}
            corpus_item_id={rec.corpus_item_id}
            scheduled_corpus_item_id={rec.scheduled_corpus_item_id}
            recommended_at={rec.recommended_at}
            received_rank={rec.received_rank}
            format={rec.format}
            alt_text={rec.alt_text}
            isTimeSensitive={rec.isTimeSensitive}
          />
        )
      );
    }

    if (widgets?.positions?.length && widgets?.data?.length) {
      let positionIndex = 0;
      const source = "CARDGRID_WIDGET";

      for (const widget of widgets.data) {
        let widgetComponent = null;
        const position = widgets.positions[positionIndex];

        // Stop if we run out of positions to place widgets.
        if (!position) {
          break;
        }

        switch (widget?.type) {
          case "TopicsWidget":
            widgetComponent = (
              <TopicsWidget
                position={position.index}
                dispatch={this.props.dispatch}
                source={source}
                id={WIDGET_IDS.TOPICS}
              />
            );
            break;
        }

        if (widgetComponent) {
          // We found a widget, so up the position for next try.
          positionIndex++;
          // We replace an existing card with the widget.
          cards.splice(position.index, 1, widgetComponent);
        }
      }
    }
    if (listFeedEnabled) {
      const isFakespot = listFeedSelectedFeed === "fakespot";
      const fakespotEnabled = prefs[PREF_FAKESPOT_ENABLED];
      if (!isFakespot || (isFakespot && fakespotEnabled)) {
        // Place the list feed as the 3rd element in the card grid
        cards.splice(
          2,
          1,
          this.renderListFeed(
            this.props.data.recommendations,
            listFeedSelectedFeed
          )
        );
      }
    }

    // if a banner ad is enabled and we have any available, place them in the grid
    const { spocs } = this.props.DiscoveryStream;

    if (
      (billboardEnabled || leaderboardEnabled) &&
      spocs?.data?.newtab_spocs?.items
    ) {
      // Only render one AdBanner in the grid -
      // Prioritize rendering a leaderboard if it exists,
      // otherwise render a billboard
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

        function displayCardsPerRow() {
          // Determines the number of cards per row based on the window width:
          // width <= 1122px: 2 cards per row
          // width 1123px to 1697px: 3 cards per row
          // width >= 1698px: 4 cards per row
          if (window.innerWidth <= 1122) {
            return 2;
          } else if (window.innerWidth > 1122 && window.innerWidth < 1698) {
            return 3;
          }
          return 4;
        }

        const injectAdBanner = bannerIndex => {
          // .splice() inserts the AdBanner at the desired index, ensuring correct DOM order for accessibility and keyboard navigation.
          // .push() would place it at the end, which is visually incorrect even if adjusted with CSS.
          cards.splice(
            bannerIndex,
            0,
            <AdBanner
              spoc={spocToRender}
              key={`dscard-${spocToRender.id}`}
              dispatch={this.props.dispatch}
              type={this.props.type}
              firstVisibleTimestamp={this.props.firstVisibleTimestamp}
              row={row}
              prefs={prefs}
            />
          );
        };

        const getBannerIndex = () => {
          // Calculate the index for where the AdBanner should be added, depending on number of cards per row on the grid
          const cardsPerRow = displayCardsPerRow();
          let bannerIndex = (row - 1) * cardsPerRow;
          return bannerIndex;
        };

        injectAdBanner(getBannerIndex());
      }
    }

    let moreRecsHeader = "";
    // For now this is English only.
    if (showRecentSaves || (essentialReadsHeader && editorsPicksHeader)) {
      let spliceAt = 6;
      // For 4 card row layouts, second row is 8 cards, and regular it is 6 cards.
      if (fourCardLayout) {
        spliceAt = 8;
      }
      // If we have a custom header, ensure the more recs section also has a header.
      moreRecsHeader = "More Recommendations";
      // Put the first 2 rows into essentialReadsCards.
      essentialReadsCards = [...cards.splice(0, spliceAt)];
      // Put the rest into editorsPicksCards.
      if (essentialReadsHeader && editorsPicksHeader) {
        editorsPicksCards = [...cards.splice(0, cards.length)];
      }
    }

    const gridClassName = this.renderGridClassName();

    return (
      <>
        {!isOnboardingExperienceDismissed && onboardingExperience && (
          <OnboardingExperience dispatch={this.props.dispatch} />
        )}
        {essentialReadsCards?.length > 0 && (
          <div className={gridClassName}>{essentialReadsCards}</div>
        )}
        {showRecentSaves && (
          <RecentSavesContainer
            gridClassName={gridClassName}
            dispatch={this.props.dispatch}
          />
        )}
        {editorsPicksCards?.length > 0 && (
          <>
            <DSSubHeader>
              <span className="section-title">
                <FluentOrText message="Editor’s Picks" />
              </span>
            </DSSubHeader>
            <div className={gridClassName}>{editorsPicksCards}</div>
          </>
        )}
        {cards?.length > 0 && (
          <>
            {moreRecsHeader && (
              <DSSubHeader>
                <span className="section-title">
                  <FluentOrText message={moreRecsHeader} />
                </span>
              </DSSubHeader>
            )}
            <div className={gridClassName}>{cards}</div>
          </>
        )}
      </>
    );
  }

  renderListFeed(recommendations, selectedFeed) {
    const recs = recommendations.filter(item => item.feedName === selectedFeed);
    const isFakespot = selectedFeed === "fakespot";
    // remove duplicates from category list
    const categories = [...new Set(recs.map(({ category }) => category))];
    const listFeed = (
      <ListFeed
        // only display recs that match selectedFeed for ListFeed
        recs={recs}
        categories={isFakespot ? categories : []}
        firstVisibleTimestamp={this.props.firstVisibleTimestamp}
        type={this.props.type}
        dispatch={this.props.dispatch}
      />
    );
    return listFeed;
  }

  renderGridClassName() {
    const prefs = this.props.Prefs.values;
    const {
      hybridLayout,
      hideCardBackground,
      fourCardLayout,
      compactGrid,
      hideDescriptions,
    } = this.props;

    const adSizingVariantAEnabled = prefs["newtabAdSize.variant-a"];
    const adSizingVariantBEnabled = prefs["newtabAdSize.variant-b"];
    const adSizingVariantEnabled =
      adSizingVariantAEnabled || adSizingVariantBEnabled;

    let adSizingVariantClassName = "";
    if (adSizingVariantEnabled) {
      // Ad sizing experiment variant, we want to ensure only 1 of these is ever enabled.
      adSizingVariantClassName = adSizingVariantAEnabled
        ? `ad-sizing-variant-a`
        : `ad-sizing-variant-b`;
    }

    const hideCardBackgroundClass = hideCardBackground
      ? `ds-card-grid-hide-background`
      : ``;
    const fourCardLayoutClass = fourCardLayout
      ? `ds-card-grid-four-card-variant`
      : ``;
    const hideDescriptionsClassName = !hideDescriptions
      ? `ds-card-grid-include-descriptions`
      : ``;
    const compactGridClassName = compactGrid ? `ds-card-grid-compact` : ``;
    const hybridLayoutClassName = hybridLayout
      ? `ds-card-grid-hybrid-layout`
      : ``;

    const gridClassName = `ds-card-grid ${hybridLayoutClassName} ${hideCardBackgroundClass} ${fourCardLayoutClass} ${hideDescriptionsClassName} ${compactGridClassName} ${adSizingVariantClassName}`;
    return gridClassName;
  }

  render() {
    const { data } = this.props;

    // Handle a render before feed has been fetched by displaying nothing
    if (!data) {
      return null;
    }

    // Handle the case where a user has dismissed all recommendations
    const isEmpty = data.recommendations.length === 0;

    return (
      <div>
        {this.props.title && (
          <div className="ds-header">
            <div className="title">{this.props.title}</div>
            {this.props.context && (
              <FluentOrText message={this.props.context}>
                <div className="ds-context" />
              </FluentOrText>
            )}
          </div>
        )}
        {isEmpty ? (
          <div className="ds-card-grid empty">
            <DSEmptyState
              status={data.status}
              dispatch={this.props.dispatch}
              feed={this.props.feed}
            />
          </div>
        ) : (
          this.renderCards()
        )}
      </div>
    );
  }
}

_CardGrid.defaultProps = {
  items: 4, // Number of stories to display
};

export const CardGrid = connect(state => ({
  Prefs: state.Prefs,
  App: state.App,
  DiscoveryStream: state.DiscoveryStream,
}))(_CardGrid);
