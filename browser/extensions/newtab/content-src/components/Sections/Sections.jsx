/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { Card, PlaceholderCard } from "content-src/components/Card/Card";
import { CollapsibleSection } from "content-src/components/CollapsibleSection/CollapsibleSection";
import { ComponentPerfTimer } from "content-src/components/ComponentPerfTimer/ComponentPerfTimer";
import { FluentOrText } from "content-src/components/FluentOrText/FluentOrText";
import { connect } from "react-redux";
import { MoreRecommendations } from "content-src/components/MoreRecommendations/MoreRecommendations";
import React from "react";
import { TopSites } from "content-src/components/TopSites/TopSites";

const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";
const CARDS_PER_ROW_DEFAULT = 3;
const CARDS_PER_ROW_COMPACT_WIDE = 4;

export class Section extends React.PureComponent {
  get numRows() {
    const { rowsPref, maxRows, Prefs } = this.props;
    return rowsPref ? Prefs.values[rowsPref] : maxRows;
  }

  _dispatchImpressionStats() {
    const { props } = this;
    let cardsPerRow = CARDS_PER_ROW_DEFAULT;
    if (
      props.compactCards &&
      globalThis.matchMedia(`(min-width: 1072px)`).matches
    ) {
      // If the section has compact cards and the viewport is wide enough, we show
      // 4 columns instead of 3.
      // $break-point-widest = 1072px (from _variables.scss)
      cardsPerRow = CARDS_PER_ROW_COMPACT_WIDE;
    }
    const maxCards = cardsPerRow * this.numRows;
    const cards = props.rows.slice(0, maxCards);

    if (this.needsImpressionStats(cards)) {
      props.dispatch(
        ac.ImpressionStats({
          source: props.eventSource,
          tiles: cards.map(link => ({ id: link.guid })),
        })
      );
      this.impressionCardGuids = cards.map(link => link.guid);
    }
  }

  // This sends an event when a user sees a set of new content. If content
  // changes while the page is hidden (i.e. preloaded or on a hidden tab),
  // only send the event if the page becomes visible again.
  sendImpressionStatsOrAddListener() {
    const { props } = this;

    if (!props.shouldSendImpressionStats || !props.dispatch) {
      return;
    }

    if (props.document.visibilityState === VISIBLE) {
      this._dispatchImpressionStats();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(
          VISIBILITY_CHANGE_EVENT,
          this._onVisibilityChange
        );
      }

      // When the page becomes visible, send the impression stats ping if the section isn't collapsed.
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === VISIBLE) {
          if (!this.props.pref.collapsed) {
            this._dispatchImpressionStats();
          }
          props.document.removeEventListener(
            VISIBILITY_CHANGE_EVENT,
            this._onVisibilityChange
          );
        }
      };
      props.document.addEventListener(
        VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
  }

  componentDidMount() {
    this.sendNewTabRehydrated(this.props.initialized);
    if (this.props.rows.length && !this.props.pref.collapsed) {
      this.sendImpressionStatsOrAddListener();
    }
  }

  componentDidUpdate(prevProps) {
    this.sendNewTabRehydrated(this.props.initialized);
    const { props } = this;
    const isCollapsed = props.pref.collapsed;
    const wasCollapsed = prevProps.pref.collapsed;
    if (
      // Don't send impression stats for the empty state
      props.rows.length &&
      // We only want to send impression stats if the content of the cards has changed
      // and the section is not collapsed...
      ((props.rows !== prevProps.rows && !isCollapsed) ||
        // or if we are expanding a section that was collapsed.
        (wasCollapsed && !isCollapsed))
    ) {
      this.sendImpressionStatsOrAddListener();
    }
  }

  componentWillUnmount() {
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(
        VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
  }

  needsImpressionStats(cards) {
    if (
      !this.impressionCardGuids ||
      this.impressionCardGuids.length !== cards.length
    ) {
      return true;
    }

    for (let i = 0; i < cards.length; i++) {
      if (cards[i].guid !== this.impressionCardGuids[i]) {
        return true;
      }
    }

    return false;
  }

  // The NEW_TAB_REHYDRATED event is used to inform feeds that their
  // data has been consumed e.g. for counting the number of tabs that
  // have rendered that data.
  sendNewTabRehydrated(initialized) {
    if (initialized && !this.renderNotified) {
      this.props.dispatch(
        ac.AlsoToMain({ type: at.NEW_TAB_REHYDRATED, data: {} })
      );
      this.renderNotified = true;
    }
  }

  render() {
    const {
      id,
      eventSource,
      title,
      rows,
      emptyState,
      dispatch,
      compactCards,
      read_more_endpoint,
      contextMenuOptions,
      initialized,
      learnMore,
      pref,
      privacyNoticeURL,
      isFirst,
      isLast,
    } = this.props;

    const waitingForSpoc =
      id === "topstories" && this.props.Pocket.waitingForSpoc;
    const maxCardsPerRow = compactCards
      ? CARDS_PER_ROW_COMPACT_WIDE
      : CARDS_PER_ROW_DEFAULT;
    const { numRows } = this;
    const maxCards = maxCardsPerRow * numRows;
    const maxCardsOnNarrow = CARDS_PER_ROW_DEFAULT * numRows;
    const shouldShowReadMore = read_more_endpoint;

    const realRows = rows.slice(0, maxCards);

    // The empty state should only be shown after we have initialized and there is no content.
    // Otherwise, we should show placeholders.
    const shouldShowEmptyState = initialized && !rows.length;

    const cards = [];
    if (!shouldShowEmptyState) {
      for (let i = 0; i < maxCards; i++) {
        const link = realRows[i];
        // On narrow viewports, we only show 3 cards per row. We'll mark the rest as
        // .hide-for-narrow to hide in CSS via @media query.
        const className = i >= maxCardsOnNarrow ? "hide-for-narrow" : "";
        let usePlaceholder = !link;
        // If we are in the third card and waiting for spoc,
        // use the placeholder.
        if (!usePlaceholder && i === 2 && waitingForSpoc) {
          usePlaceholder = true;
        }
        cards.push(
          !usePlaceholder ? (
            <Card
              key={i}
              index={i}
              className={className}
              dispatch={dispatch}
              link={link}
              contextMenuOptions={contextMenuOptions}
              eventSource={eventSource}
              shouldSendImpressionStats={this.props.shouldSendImpressionStats}
              isWebExtension={this.props.isWebExtension}
            />
          ) : (
            <PlaceholderCard key={i} className={className} />
          )
        );
      }
    }

    const sectionClassName = [
      "section",
      compactCards ? "compact-cards" : "normal-cards",
    ].join(" ");

    // <Section> <-- React component
    // <section> <-- HTML5 element
    return (
      <ComponentPerfTimer {...this.props}>
        <CollapsibleSection
          className={sectionClassName}
          title={title}
          id={id}
          eventSource={eventSource}
          collapsed={this.props.pref.collapsed}
          showPrefName={(pref && pref.feed) || id}
          privacyNoticeURL={privacyNoticeURL}
          Prefs={this.props.Prefs}
          isFixed={this.props.isFixed}
          isFirst={isFirst}
          isLast={isLast}
          learnMore={learnMore}
          dispatch={this.props.dispatch}
          isWebExtension={this.props.isWebExtension}
        >
          {!shouldShowEmptyState && <ul className="section-list">{cards}</ul>}
          {shouldShowEmptyState && (
            <div className="section-empty-state">
              <div className="empty-state">
                <FluentOrText message={emptyState.message}>
                  <p className="empty-state-message" />
                </FluentOrText>
              </div>
            </div>
          )}
          {id === "topstories" && (
            <div className="top-stories-bottom-container">
              <div className="wrapper-more-recommendations">
                {shouldShowReadMore && (
                  <MoreRecommendations
                    read_more_endpoint={read_more_endpoint}
                  />
                )}
              </div>
            </div>
          )}
        </CollapsibleSection>
      </ComponentPerfTimer>
    );
  }
}

Section.defaultProps = {
  document: globalThis.document,
  rows: [],
  emptyState: {},
  pref: {},
  title: "",
};

export const SectionIntl = connect(state => ({
  Prefs: state.Prefs,
  Pocket: state.Pocket,
}))(Section);

export class _Sections extends React.PureComponent {
  renderSections() {
    const sections = [];
    const enabledSections = this.props.Sections.filter(
      section => section.enabled
    );
    const { sectionOrder, "feeds.topsites": showTopSites } =
      this.props.Prefs.values;
    // Enabled sections doesn't include Top Sites, so we add it if enabled.
    const expectedCount = enabledSections.length + ~~showTopSites;

    for (const sectionId of sectionOrder.split(",")) {
      const commonProps = {
        key: sectionId,
        isFirst: sections.length === 0,
        isLast: sections.length === expectedCount - 1,
      };
      if (sectionId === "topsites" && showTopSites) {
        sections.push(<TopSites {...commonProps} />);
      } else {
        const section = enabledSections.find(s => s.id === sectionId);
        if (section) {
          sections.push(<SectionIntl {...section} {...commonProps} />);
        }
      }
    }
    return sections;
  }

  render() {
    return <div className="sections-list">{this.renderSections()}</div>;
  }
}

export const Sections = connect(state => ({
  Sections: state.Sections,
  Prefs: state.Prefs,
}))(_Sections);
