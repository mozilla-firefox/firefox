/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { CardGrid } from "content-src/components/DiscoveryStreamComponents/CardGrid/CardGrid";
import { CollapsibleSection } from "content-src/components/CollapsibleSection/CollapsibleSection";
import { connect } from "react-redux";
import { ReportContent } from "../DiscoveryStreamComponents/ReportContent/ReportContent";
import { Highlights } from "content-src/components/DiscoveryStreamComponents/Highlights/Highlights";
import { HorizontalRule } from "content-src/components/DiscoveryStreamComponents/HorizontalRule/HorizontalRule";
// eslint-disable-next-line no-shadow
import { Navigation } from "content-src/components/DiscoveryStreamComponents/Navigation/Navigation";
import { PrivacyLink } from "content-src/components/DiscoveryStreamComponents/PrivacyLink/PrivacyLink";
import React from "react";
import { SectionTitle } from "content-src/components/DiscoveryStreamComponents/SectionTitle/SectionTitle";
import {
  keepOnlySections,
  selectLayoutRender,
} from "content-src/lib/selectLayoutRender";
import { TopSites } from "content-src/components/TopSites/TopSites";
import { CardSections } from "../DiscoveryStreamComponents/CardSections/CardSections";
import { Widgets } from "content-src/components/Widgets/Widgets";
import { Spaces } from "content-src/components/Spaces/Spaces";
import {
  isSpacesActive,
  hasSpacesArrows,
  isSpacesArrowsAssigned,
  isSpacesPreviewEnabled,
  isSpacesPeekAssigned,
  resolveFeatureSpacePreviewImages,
  resolvePopulatedSpaces,
  resolveThematicSpaces,
  SPACE_IDS,
} from "common/PageLayoutVariants.mjs";
import {
  ASROUTER_NEWTAB_MESSAGE_POSITIONS,
  shouldShowASRouterNewTabMessage,
} from "../../lib/asrouter-message-utils.mjs";
import { ErrorBoundary } from "content-src/components/ErrorBoundary/ErrorBoundary";
import { MessageWrapper } from "content-src/components/MessageWrapper/MessageWrapper";
import { ExternalComponentWrapper } from "content-src/components/ExternalComponentWrapper/ExternalComponentWrapper";

// @nova-cleanup(remove-pref): Remove PREF_NOVA_ENABLED
const PREF_NOVA_ENABLED = "nova.enabled";

const ALLOWED_CSS_URL_PREFIXES = [
  "chrome://",
  "resource://",
  "https://img-getpocket.cdn.mozilla.net/",
];
const DUMMY_CSS_SELECTOR = "DUMMY#CSS.SELECTOR";

/**
 * Validate a CSS declaration. The values are assumed to be normalized by CSSOM.
 */
export function isAllowedCSS(property, value) {
  // Bug 1454823: INTERNAL properties, e.g., -moz-context-properties, are
  // exposed but their values aren't resulting in getting nothing. Fortunately,
  // we don't care about validating the values of the current set of properties.
  if (value === undefined) {
    return true;
  }

  // Make sure all urls are of the allowed protocols/prefixes
  const urls = value.match(/url\("[^"]+"\)/g);
  return (
    !urls ||
    urls.every(url =>
      ALLOWED_CSS_URL_PREFIXES.some(prefix => url.slice(5).startsWith(prefix))
    )
  );
}

export class _DiscoveryStreamBase extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onStyleMount = this.onStyleMount.bind(this);
  }

  onStyleMount(style) {
    // Unmounting style gets rid of old styles, so nothing else to do
    if (!style) {
      return;
    }

    const { sheet } = style;
    const styles = JSON.parse(style.dataset.styles);
    styles.forEach((row, rowIndex) => {
      row.forEach((component, componentIndex) => {
        // Nothing to do without optional styles overrides
        if (!component) {
          return;
        }

        Object.entries(component).forEach(([selectors, declarations]) => {
          // Start with a dummy rule to validate declarations and selectors
          sheet.insertRule(`${DUMMY_CSS_SELECTOR} {}`);
          const [rule] = sheet.cssRules;

          // Validate declarations and remove any offenders. CSSOM silently
          // discards invalid entries, so here we apply extra restrictions.
          rule.style = declarations;
          [...rule.style].forEach(property => {
            const value = rule.style[property];
            if (!isAllowedCSS(property, value)) {
              console.error(`Bad CSS declaration ${property}: ${value}`);
              rule.style.removeProperty(property);
            }
          });

          // Set the actual desired selectors scoped to the component
          const prefix = `.ds-layout > .ds-column:nth-child(${
            rowIndex + 1
          }) .ds-column-grid > :nth-child(${componentIndex + 1})`;
          // NB: Splitting on "," doesn't work with strings with commas, but
          // we're okay with not supporting those selectors
          rule.selectorText = selectors
            .split(",")
            .map(
              selector =>
                prefix +
                // Assume :pseudo-classes are for component instead of descendant
                (selector[0] === ":" ? "" : " ") +
                selector
            )
            .join(",");

          // CSSOM silently ignores bad selectors, so we'll be noisy instead
          if (rule.selectorText === DUMMY_CSS_SELECTOR) {
            console.error(`Bad CSS selector ${selectors}`);
          }
        });
      });
    });
  }

  renderComponent(component) {
    switch (component.type) {
      case "Highlights":
        return <Highlights />;
      case "TopSites":
        // @nova-cleanup(remove-conditional): Remove this guard when DiscoveryStreamBase
        // is no longer used in the Nova layout
        if (this.props.Prefs.values[PREF_NOVA_ENABLED]) {
          return null;
        }
        return (
          <div className="ds-top-sites">
            <TopSites isFixed={true} title={component.header?.title} />
          </div>
        );
      case "SectionTitle":
        return <SectionTitle header={component.header} />;
      case "Navigation":
        return (
          <Navigation
            dispatch={this.props.dispatch}
            links={component.properties.links}
            extraLinks={component.properties.extraLinks}
            alignment={component.properties.alignment}
            explore_topics={component.properties.explore_topics}
            header={component.header}
            locale={this.props.App.locale}
            newFooterSection={component.newFooterSection}
            privacyNoticeURL={component.properties.privacyNoticeURL}
          />
        );
      case "CardGrid": {
        const sectionsEnabled =
          this.props.Prefs.values["discoverystream.sections.enabled"];
        if (sectionsEnabled) {
          return (
            <CardSections
              feed={component.feed}
              data={component.data}
              dispatch={this.props.dispatch}
              type={component.type}
              ctaButtonSponsors={component.properties.ctaButtonSponsors}
              ctaButtonVariant={component.properties.ctaButtonVariant}
              spocsLoading={this.props.spocsLoading}
            />
          );
        }
        return (
          <CardGrid
            title={component.header && component.header.title}
            data={component.data}
            feed={component.feed}
            widgets={component.widgets}
            type={component.type}
            dispatch={this.props.dispatch}
            items={component.properties.items}
            hybridLayout={component.properties.hybridLayout}
            hideCardBackground={component.properties.hideCardBackground}
            fourCardLayout={component.properties.fourCardLayout}
            compactGrid={component.properties.compactGrid}
            ctaButtonSponsors={component.properties.ctaButtonSponsors}
            ctaButtonVariant={component.properties.ctaButtonVariant}
            hideDescriptions={this.props.DiscoveryStream.hideDescriptions}
            spocPositions={component.spocs?.positions}
            placeholder={this.props.spocsLoading}
          />
        );
      }
      case "HorizontalRule":
        return <HorizontalRule />;
      case "PrivacyLink":
        return <PrivacyLink properties={component.properties} />;
      case "Widgets":
        return (
          <Widgets
            widgetIds={component.widgetIds}
            placement={component.widgetPlacement}
          />
        );
      default:
        return <div>{component.type}</div>;
    }
  }

  renderStyles(styles) {
    // Use json string as both the key and styles to render so React knows when
    // to unmount and mount a new instance for new styles.
    const json = JSON.stringify(styles);
    return <style key={json} data-styles={json} ref={this.onStyleMount} />;
  }

  render() {
    const { locale } = this.props;
    // Bug 1980459 - Note that selectLayoutRender acts as a selector that transforms layout data based on current
    // preferences and experiment flags. It runs after Redux state is populated but before render.
    // Components removed in selectLayoutRender (e.g., Widgets or TopSites) will not appear in the
    // layoutRender result, and therefore will not be rendered here regardless of logic below.

    // Select layout renders data by adding spocs and position to recommendations
    const { layoutRender } = selectLayoutRender({
      state: this.props.DiscoveryStream,
      prefs: this.props.Prefs.values,
      locale,
    });
    // @nova-cleanup(remove-pref): Delete this line; remove all !novaEnabled guards on ASRouterNewTabMessage blocks below.
    const novaEnabled = this.props.Prefs.values[PREF_NOVA_ENABLED];
    const sectionsEnabled =
      this.props.Prefs.values["discoverystream.sections.enabled"];
    const topicSelectionEnabled =
      this.props.Prefs.values["discoverystream.topicSelection.enabled"];
    const reportAdsEnabled =
      this.props.Prefs.values["discoverystream.reportAds.enabled"];
    const spocsEnabled = this.props.Prefs.values["unifiedAds.spocs.enabled"];
    const reportTileAdsEnabled =
      this.props.Prefs.values["discoverystream.reportAds.tiles.enabled"];

    // Find the first component of a type and remove it from layout
    const extractComponent = type => {
      for (const [rowIndex, row] of Object.entries(layoutRender)) {
        for (const [index, component] of Object.entries(row.components)) {
          if (component.type === type) {
            // Remove the row if it was the only component or the single item
            if (row.components.length === 1) {
              layoutRender.splice(rowIndex, 1);
            } else {
              row.components.splice(index, 1);
            }
            return component;
          }
        }
      }
      return null;
    };

    // Get "topstories" Section state for default values
    const topStories = this.props.Sections.find(s => s.id === "topstories");

    if (!topStories) {
      return null;
    }

    // Extract TopSites to render before the rest and Message to use for header
    const topSites = extractComponent("TopSites");

    // There are two ways to enable widgets:
    // Via `widgets.system.*` prefs or Nimbus experiment
    const widgetsNimbusTrainhopEnabled =
      this.props.Prefs.values.trainhopConfig?.widgets?.enabled;
    const widgetsNimbusEnabled = this.props.Prefs.values.widgetsConfig?.enabled;
    const widgetsSystemPrefsEnabled =
      this.props.Prefs.values["widgets.system.enabled"];

    const widgets =
      widgetsNimbusTrainhopEnabled ||
      widgetsNimbusEnabled ||
      widgetsSystemPrefsEnabled;

    const message = extractComponent("Message") || {
      header: {
        link_text: topStories.learnMore.link.message,
        link_url: topStories.learnMore.link.href,
        title: topStories.title,
      },
    };

    const privacyLinkComponent = extractComponent("PrivacyLink");
    // renderLayout always returns a div, so gate on the same condition
    // Highlights.jsx uses or the side-by-side panel frames an empty box.
    const hasHighlights = Boolean(
      this.props.Sections?.find(s => s.id === "highlights")?.enabled
    );
    let learnMore = {
      link: {
        href: message.header.link_url,
        message: message.header.link_text,
      },
    };
    let sectionTitle = message.header.title;
    let subTitle = "";

    const { DiscoveryStream } = this.props;

    // Widgets, the content feed and Highlights are already three separate boxes
    // in this band, so spaces navigates between those rather than introducing a
    // new grouping. Declared here so the flat band and Spaces can each place
    // them.
    const widgetsGroup =
      widgets &&
      this.renderLayout([
        {
          width: 12,
          components: [{ type: "Widgets" }],
          sectionType: "widgets",
        },
      ]);

    // A thematic space shows only its own content sections, so the feed is built
    // per space from a filtered copy of the layout rather than shared. Passing
    // null keeps the whole feed, which is what every other layout gets.
    const contentGroupFor = (sectionKeys, isLeadSpace = true) => {
      const rows =
        sectionKeys === null
          ? layoutRender
          : keepOnlySections(layoutRender, sectionKeys);
      const hasFeed = !!rows.length;
      return (
        <div className={`layout-content-column${hasFeed ? " has-feed" : ""}`}>
          {/* Nova only: the ABOVE_CONTENT_FEED message, built in Base.jsx. This
          is the widgets/feed boundary, which only exists inside this component.
          One copy only, or a message would render once per space. */}
          {isLeadSpace && this.props.aboveContentFeed}

          {hasFeed && (
            <CollapsibleSection
              className="ds-layout"
              collapsed={topStories.pref.collapsed}
              dispatch={this.props.dispatch}
              id={topStories.id}
              isFixed={true}
              learnMore={learnMore}
              privacyNoticeURL={topStories.privacyNoticeURL}
              showPrefName={topStories.pref.feed}
              title={sectionTitle}
              subTitle={subTitle}
              mayHaveTopicsSelection={topicSelectionEnabled}
              sectionsEnabled={sectionsEnabled}
              eventSource="CARDGRID"
            >
              {this.renderLayout(rows)}
            </CollapsibleSection>
          )}
          {isLeadSpace &&
            privacyLinkComponent &&
            this.renderLayout([
              {
                width: 12,
                components: [privacyLinkComponent],
              },
            ])}
        </div>
      );
    };

    const highlightsGroup = hasHighlights && (
      <div className="layout-highlights-column">
        {this.renderLayout([
          {
            width: 12,
            components: [{ type: "Highlights" }],
          },
        ])}
      </div>
    );

    // Guarded so the ordinary layout does no spaces work at all: isSpacesActive
    // reads prefs only. resolvePopulatedSpaces then decides which spaces exist
    // and their tablist order, so the tabs follow the prefs rather than whatever
    // the feeds have delivered so far.
    let spaceEntries = [];
    let defaultSpaceId;
    // One call: null unless the thematic variant is assigned with a usable
    // config, and otherwise everything each space needs, already resolved.
    const thematicSpaces = resolveThematicSpaces(
      this.props.Prefs.values,
      DiscoveryStream
    );
    if (isSpacesActive(this.props.Prefs.values, DiscoveryStream)) {
      const populated =
        thematicSpaces?.order ??
        resolvePopulatedSpaces(this.props.Prefs.values);
      // V1 opens on stories wherever pageLayouts.spacesOrder puts it.
      defaultSpaceId = thematicSpaces?.defaultId ?? SPACE_IDS.STORIES;
      // A thematic space is its own slice of the feed plus the widgets column,
      // which is the side-by-side pair rather than one of the three boxes. A
      // space whose widgets are all off gets no column and keeps the feed width.
      const spaceContent = thematicSpaces
        ? Object.fromEntries(
            populated.map(id => [
              id,
              <React.Fragment key={id}>
                {/* A widgets-only space has no feed column. The lead space
                keeps it for the message and privacy link. */}
                {(!!thematicSpaces.spaces[id].sectionKeys.length ||
                  id === defaultSpaceId) &&
                  contentGroupFor(
                    thematicSpaces.spaces[id].sectionKeys,
                    id === defaultSpaceId
                  )}
                {widgets &&
                  !!thematicSpaces.spaces[id].widgetIds.length &&
                  this.renderLayout([
                    {
                      width: 12,
                      components: [
                        {
                          type: "Widgets",
                          widgetIds: thematicSpaces.spaces[id].widgetIds,
                          widgetPlacement:
                            thematicSpaces.spaces[id].widgetPlacement,
                        },
                      ],
                      sectionType: "widgets",
                    },
                  ])}
                {/* Recent activity keeps showing for a user who has it on, in
                the space the config marks for it or else the one the page
                opens on. */}
                {id === thematicSpaces.activityId && highlightsGroup}
              </React.Fragment>,
            ])
          )
        : {
            [SPACE_IDS.STORIES]: contentGroupFor(null),
            [SPACE_IDS.WIDGETS]: widgetsGroup,
            [SPACE_IDS.ACTIVITY]: highlightsGroup,
          };
      // Whether each space's content has settled, so the tab preview does not
      // pick its first item from placeholders and then change it. A thematic
      // space leads with its feed, as V1 stories is its feed. Sponsored stories
      // arrive separately and can take the first slot, so the feed waits for
      // them too; their update is sent even with them off. V1 widgets render
      // from prefs alone.
      const feedsLoaded = !!(
        DiscoveryStream.feeds?.loaded && DiscoveryStream.spocs?.loaded
      );
      const spaceReady = thematicSpaces
        ? {}
        : {
            [SPACE_IDS.WIDGETS]: true,
            [SPACE_IDS.ACTIVITY]: !!this.props.Sections?.find(
              s => s.id === "highlights"
            )?.initialized,
          };
      // Thematic spaces carry their own, from their config.
      const featureImages = thematicSpaces
        ? {}
        : resolveFeatureSpacePreviewImages(this.props.Prefs.values);
      spaceEntries = populated
        .map(id => ({
          id,
          content: spaceContent[id],
          ready: spaceReady[id] ?? feedsLoaded,
          previewImage: featureImages[id],
          ...(thematicSpaces?.spaces[id] ?? {}),
        }))
        .filter(space => space.content);
    }
    const spacesActive = spaceEntries.length > 1;

    return (
      <React.Fragment>
        {/* Reporting stories/articles will only be available in sections, not the default card grid  */}
        {((reportAdsEnabled && spocsEnabled) ||
          sectionsEnabled ||
          reportTileAdsEnabled) && (
          <ReportContent spocs={DiscoveryStream.spocs} />
        )}

        {/**
         * The ABOVE_TOPSITES ASRouterNewTabMessage rendering actually occurs in Base.jsx
         * for silly reasons. Essentially, it's easier for the browser_asrouter_newtab_message
         * mochitest-browser test to render a test message if it doesn't have to rely on
         * DiscoveryStreamBase being rendered. Thankfully, this can all be removed
         * after Nova ships.
         */}

        {topSites &&
          this.renderLayout([
            {
              width: 12,
              components: [topSites],
              sectionType: "topsites",
            },
          ])}

        {
          // @nova-cleanup(remove-conditional): Remove this entire block; Base.jsx handles ABOVE_WIDGETS in the Nova layout.
        }
        {!novaEnabled &&
          shouldShowASRouterNewTabMessage(
            this.props.Messages,
            "ASRouterNewTabMessage",
            ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_WIDGETS
          ) && (
            <ErrorBoundary>
              <MessageWrapper dispatch={this.props.dispatch}>
                <ExternalComponentWrapper
                  type="ASROUTER_NEWTAB_MESSAGE"
                  messageData={this.props.Messages.messageData}
                  className="asrouter-newtab-message-wrapper"
                />
              </MessageWrapper>
            </ErrorBoundary>
          )}

        {spacesActive ? (
          <Spaces
            spaces={spaceEntries}
            defaultId={defaultSpaceId}
            arrows={hasSpacesArrows(this.props.Prefs.values)}
            tablist={!isSpacesArrowsAssigned(this.props.Prefs.values)}
            preview={isSpacesPreviewEnabled(this.props.Prefs.values)}
            peek={isSpacesPeekAssigned(this.props.Prefs.values)}
            dispatch={this.props.dispatch}
          />
        ) : (
          <React.Fragment>
            {widgetsGroup}

            {
              //@nova-cleanup(remove-conditional): Remove this entire block; in the Nova layout Base.jsx passes ABOVE_CONTENT_FEED in via the aboveContentFeed prop below. */
            }
            {!novaEnabled &&
              shouldShowASRouterNewTabMessage(
                this.props.Messages,
                "ASRouterNewTabMessage",
                ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_CONTENT_FEED
              ) && (
                <ErrorBoundary>
                  <MessageWrapper dispatch={this.props.dispatch}>
                    <ExternalComponentWrapper
                      type="ASROUTER_NEWTAB_MESSAGE"
                      messageData={this.props.Messages.messageData}
                      className="asrouter-newtab-message-wrapper"
                    />
                  </MessageWrapper>
                </ErrorBoundary>
              )}

            {contentGroupFor(null)}
            {highlightsGroup}
          </React.Fragment>
        )}
      </React.Fragment>
    );
  }

  renderLayout(layoutRender) {
    const styles = [];
    let [data] = layoutRender;
    // Add helper class for topsites
    const sectionClass = data.sectionType
      ? `ds-layout-${data.sectionType}`
      : "";

    return (
      <div className={`discovery-stream ds-layout ${sectionClass}`}>
        {layoutRender.map((row, rowIndex) => (
          <div
            key={`row-${rowIndex}`}
            className={`ds-column ds-column-${row.width}`}
          >
            <div className="ds-column-grid">
              {row.components.map((component, componentIndex) => {
                if (!component) {
                  return null;
                }
                styles[rowIndex] = [
                  ...(styles[rowIndex] || []),
                  component.styles,
                ];
                return (
                  <div key={`component-${componentIndex}`}>
                    {this.renderComponent(component, row.width)}
                  </div>
                );
              })}
            </div>
          </div>
        ))}
        {this.renderStyles(styles)}
      </div>
    );
  }
}

export const DiscoveryStreamBase = connect(state => ({
  DiscoveryStream: state.DiscoveryStream,
  Messages: state.Messages,
  Prefs: state.Prefs,
  Sections: state.Sections,
  document: globalThis.document,
  App: state.App,
}))(_DiscoveryStreamBase);
