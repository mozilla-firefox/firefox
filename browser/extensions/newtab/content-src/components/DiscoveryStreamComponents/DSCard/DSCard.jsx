/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { DSImage } from "../DSImage/DSImage.jsx";
import { DSLinkMenu } from "../DSLinkMenu/DSLinkMenu";
import { ImpressionStats } from "../../DiscoveryStreamImpressionStats/ImpressionStats";
import {
  getActiveCardSize,
  getCardColumn,
  getNovaColumnLayout,
} from "../../../lib/utils";
import React from "react";
import { SafeAnchor } from "../SafeAnchor/SafeAnchor";
import {
  DSContextFooter,
  SponsorLabel,
  DSMessageFooter,
} from "../DSContextFooter/DSContextFooter.jsx";
import { FluentOrText } from "../../FluentOrText/FluentOrText.jsx";
import { connect } from "react-redux";
const READING_WPM = 220;
const PREF_OHTTP_MERINO = "discoverystream.merino-provider.ohttp.enabled";
const PREF_OHTTP_UNIFIED_ADS = "unifiedAds.ohttp.enabled";
const PREF_SECTIONS_ENABLED = "discoverystream.sections.enabled";

/**
 * READ TIME FROM WORD COUNT
 *
 * @param {int} wordCount number of words in an article
 * @returns {int} number of words per minute in minutes
 */
export function readTimeFromWordCount(wordCount) {
  if (!wordCount) {
    return false;
  }
  return Math.ceil(parseInt(wordCount, 10) / READING_WPM);
}

export const DSSource = ({
  source,
  timeToRead,
  newSponsoredLabel,
  context,
  sponsor,
  sponsored_by_override,
  icon_src,
}) => {
  const faviconSize = 20;

  // First try to display sponsored label or time to read here.
  if (newSponsoredLabel) {
    // If we can display something for spocs, do so.
    if (sponsored_by_override || sponsor || context) {
      return (
        <SponsorLabel
          context={context}
          sponsor={sponsor}
          sponsored_by_override={sponsored_by_override}
          newSponsoredLabel="new-sponsored-label"
        />
      );
    }
  }

  // If we are not a spoc, and can display a time to read value.
  if (source && timeToRead) {
    return (
      <p className="source clamp time-to-read">
        <FluentOrText
          message={{
            id: `newtab-label-source-read-time`,
            values: { source, timeToRead },
          }}
        />
      </p>
    );
  }

  // Otherwise display a default source.
  return (
    <div className="source-wrapper">
      {icon_src && (
        <img src={icon_src} height={faviconSize} width={faviconSize} alt="" />
      )}
      <p className="source clamp">{source}</p>
    </div>
  );
};

export const DefaultMeta = ({
  source,
  title,
  excerpt,
  timeToRead,
  newSponsoredLabel,
  context,
  context_type,
  sponsor,
  sponsored_by_override,
  ctaButtonVariant,
  dispatch,
  mayHaveSectionsCards,
  format,
  icon_src,
  novaEnabled,
}) => {
  const shouldShowFooter = format !== "rectangle" && format !== "spoc";

  return (
    <div className="meta">
      <div className="info-wrap">
        <h3 className="title clamp">
          {format === "rectangle" ? "Sponsored" : title}
        </h3>
        {format === "rectangle" ? (
          <p className="excerpt clamp">
            Sponsored content supports our mission to build a better web.
          </p>
        ) : (
          excerpt && <p className="excerpt clamp">{excerpt}</p>
        )}
      </div>
      {shouldShowFooter && (
        <div className="sections-card-footer">
          {format !== "rectangle" && format !== "spoc" && (
            <DSSource
              source={source}
              timeToRead={timeToRead}
              newSponsoredLabel={newSponsoredLabel}
              context={context}
              sponsor={sponsor}
              sponsored_by_override={sponsored_by_override}
              icon_src={icon_src}
            />
          )}
        </div>
      )}
      {!newSponsoredLabel && (
        <DSContextFooter
          context_type={context_type}
          context={context}
          sponsor={sponsor}
          sponsored_by_override={sponsored_by_override}
          cta_button_variant={ctaButtonVariant}
          source={source}
          dispatch={dispatch}
          mayHaveSectionsCards={mayHaveSectionsCards}
          novaEnabled={novaEnabled}
        />
      )}
      {/* Sponsored label is normally in the way of any message.
          newSponsoredLabel cards sponsored label is moved to just under the thumbnail,
          so we can display both, so we specifically don't pass in context. */}
      {newSponsoredLabel && (
        <DSMessageFooter context_type={context_type} context={null} />
      )}
    </div>
  );
};

export class _DSCard extends React.PureComponent {
  constructor(props) {
    super(props);

    this.onLinkClick = this.onLinkClick.bind(this);
    this.onLinkOpenIntent = this.onLinkOpenIntent.bind(this);
    this.doesLinkTopicMatchSelectedTopic =
      this.doesLinkTopicMatchSelectedTopic.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.onMenuShow = this.onMenuShow.bind(this);

    this.setContextMenuButtonHostRef = element => {
      this.contextMenuButtonHostElement = element;
    };
    this.setPlaceholderRef = element => {
      this.placeholderElement = element;
    };

    this.state = {
      isSeen: false,
    };

    // If this is for the about:home startup cache, then we always want
    // to render the DSCard, regardless of whether or not its been seen.
    if (props.App.isForStartupCache.App) {
      this.state.isSeen = true;
    }

    // We want to choose the optimal thumbnail for the underlying DSImage, but
    // want to do it in a performant way. The breakpoints used in the
    // CSS of the page are, unfortuntely, not easy to retrieve without
    // causing a style flush. To avoid that, we hardcode them here.
    //
    // The values chosen here were the dimensions of the card thumbnails as
    // computed by getBoundingClientRect() for each type of viewport width
    // across both high-density and normal-density displays.
    this.standardCardImageSizes = [
      {
        mediaMatcher: "default",
        width: 296,
        height: 160,
      },
    ];

    this.listCardImageSizes = [
      {
        mediaMatcher: "(min-width: 1122px)",
        width: 75,
        height: 75,
      },
      {
        mediaMatcher: "default",
        width: 50,
        height: 50,
      },
    ];

    this.sectionsCardImagesSizes = {
      small: {
        width: 110,
        height: 117,
      },
      medium: {
        width: 300,
        height: 160,
      },
      large: {
        width: 190,
        height: 250,
      },
    };

    this.novaSectionsCardImagesSizes = {
      small: {
        width: 132,
        height: 108,
      },
      medium: {
        width: 300,
        height: 160,
      },
      large: {
        width: 240,
        height: 200,
      },
    };

    this.sectionsColumnMediaMatcher = {
      1: "default",
      2: "(min-width: 724px)",
      3: "(min-width: 1122px)",
      4: "(min-width: 1390px)",
    };

    this.novaSectionsColumnMediaMatcher = {
      1: "default",
      2: "(min-width: 684px)",
      3: "(min-width: 1032px)",
      4: "(min-width: 1380px)",
    };
  }

  getSectionImageSize(column, size) {
    // @nova-cleanup(remove-pref): Remove conditional, use nova sizes as default
    const novaEnabled = this.props.Prefs.values["nova.enabled"];
    const imageSizes = novaEnabled
      ? this.novaSectionsCardImagesSizes
      : this.sectionsCardImagesSizes;
    const mediaMatchers = novaEnabled
      ? this.novaSectionsColumnMediaMatcher
      : this.sectionsColumnMediaMatcher;
    const cardImageSize = {
      mediaMatcher: mediaMatchers[column],
      width: imageSizes[size].width,
      height: imageSizes[size].height,
    };
    return cardImageSize;
  }

  doesLinkTopicMatchSelectedTopic() {
    // Edge case for clicking on a card when topic selections have not be set
    if (!this.props.selectedTopics) {
      return "not-set";
    }

    // Edge case the topic of the card is not one of the available topics
    if (!this.props.availableTopics.includes(this.props.topic)) {
      return "topic-not-selectable";
    }

    if (this.props.selectedTopics.includes(this.props.topic)) {
      return "true";
    }

    return "false";
  }

  getClickEventData(dwellId) {
    const matchesSelectedTopic = this.doesLinkTopicMatchSelectedTopic();
    const cardColumn = getCardColumn(this.contextMenuButtonHostElement);
    return {
      event: "CLICK",
      source: this.props.type.toUpperCase(),
      action_position: this.props.pos,
      value: {
        event_source: "card",
        card_type: this.props.flightId ? "spoc" : "organic",
        tile_id: this.props.id,
        // Matches the dwell_id SafeAnchor put on the OPEN_LINK it dispatched a
        // moment ago, so TelemetryFeed can attach this story's identity to the
        // dwell measurement already in flight.
        ...(dwellId ? { dwell_id: dwellId } : {}),
        ...(this.props.shim && this.props.shim.click
          ? { shim: this.props.shim.click }
          : {}),
        corpus_item_id: this.props.corpus_item_id,
        scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
        recommended_at: this.props.recommended_at,
        received_rank: this.props.received_rank,
        variant_id: this.props.variant_id,
        source_section_id: this.props.source_section_id,
        topic: this.props.topic,
        features: this.props.features,
        matches_selected_topic: matchesSelectedTopic,
        selected_topics: this.props.selectedTopics,
        attribution: this.props.attribution,
        ...(cardColumn ? { card_column: cardColumn } : {}),
        ...(this.props.format
          ? { format: this.props.format }
          : {
              format: getActiveCardSize(
                window.innerWidth,
                this.props.sectionsClassNames,
                this.props.section,
                this.props.flightId,
                getNovaColumnLayout(this.contextMenuButtonHostElement)
              ),
            }),
        ...(this.props.section
          ? {
              section: this.props.section,
              section_position: this.props.sectionPosition,
              is_section_followed: this.props.sectionFollowed,
              layout_name: this.props.sectionLayoutName,
            }
          : {}),
      },
    };
  }

  onLinkClick(event, dwellId) {
    if (this.props.dispatch) {
      this.props.dispatch(
        ac.DiscoveryStreamUserEvent(this.getClickEventData(dwellId))
      );
    }
  }

  onLinkOpenIntent(event) {
    this.props.dispatch?.(
      ac.OnlyToMain({
        type: at.BROWSER_LINK_OPEN_INTENT,
        data: {
          url: event.currentTarget.href,
          userEvent: this.getClickEventData(),
        },
      })
    );
  }

  onMenuUpdate(showContextMenu) {
    if (!showContextMenu) {
      const dsLinkMenuHostDiv = this.contextMenuButtonHostElement;
      if (dsLinkMenuHostDiv) {
        dsLinkMenuHostDiv.classList.remove("active", "last-item");
      }
    }
  }

  async onMenuShow() {
    const dsLinkMenuHostDiv = this.contextMenuButtonHostElement;
    if (dsLinkMenuHostDiv) {
      // Force translation so we can be sure it's ready before measuring.
      await this.props.windowObj.document.l10n.translateFragment(
        dsLinkMenuHostDiv
      );
      if (this.props.windowObj.scrollMaxX > 0) {
        dsLinkMenuHostDiv.classList.add("last-item");
      }
      dsLinkMenuHostDiv.classList.add("active");
    }
  }

  onSeen(entries) {
    if (this.state) {
      const entry = entries.find(e => e.isIntersecting);

      if (entry) {
        if (this.placeholderElement) {
          this.observer.unobserve(this.placeholderElement);
        }

        // Stop observing since element has been seen
        this.setState({
          isSeen: true,
        });
      }
    }
  }

  onIdleCallback() {
    if (!this.state.isSeen) {
      // To improve responsiveness without impacting performance,
      // we start rendering stories on idle.
      // To reduce the number of requests for secure OHTTP images,
      // we skip idle-time loading.
      if (!this.secureImage) {
        if (this.observer && this.placeholderElement) {
          this.observer.unobserve(this.placeholderElement);
        }

        this.setState({
          isSeen: true,
        });
      }
    }
  }

  componentDidMount() {
    this.idleCallbackId = this.props.windowObj.requestIdleCallback(
      this.onIdleCallback.bind(this)
    );
    if (this.placeholderElement) {
      this.observer = new IntersectionObserver(this.onSeen.bind(this));
      this.observer.observe(this.placeholderElement);
    }
  }

  componentWillUnmount() {
    // Remove observer on unmount
    if (this.observer && this.placeholderElement) {
      this.observer.unobserve(this.placeholderElement);
    }
    if (this.idleCallbackId) {
      this.props.windowObj.cancelIdleCallback(this.idleCallbackId);
    }
  }

  // Wraps the image URL with the moz-cached-ohttp:// protocol.
  // This enables Firefox to load resources over Oblivious HTTP (OHTTP),
  // providing privacy-preserving resource loading.
  // Applied only when inferred personalization is enabled.
  // See: https://firefox-source-docs.mozilla.org/browser/components/mozcachedohttp/docs/index.html
  secureImageURL(url) {
    return `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(url)}`;
  }

  getRawImageSrc() {
    let rawImageSrc = "";
    // There is no point in fetching images for startup cache.
    if (!this.props.App.isForStartupCache.App) {
      rawImageSrc = this.props.raw_image_src;
    }
    return rawImageSrc;
  }

  getFaviconSrc() {
    let faviconSrc = "";
    // There is no point in fetching favicons for startup cache.
    if (!this.props.App.isForStartupCache.App && this.props.icon_src) {
      faviconSrc = this.props.icon_src;
      if (this.secureImage) {
        faviconSrc = this.secureImageURL(this.props.icon_src);
      }
    }
    return faviconSrc;
  }

  get secureImage() {
    const { Prefs, flightId } = this.props;

    let ohttpEnabled = false;
    if (flightId) {
      ohttpEnabled = Prefs.values[PREF_OHTTP_UNIFIED_ADS];
    } else {
      ohttpEnabled = Prefs.values[PREF_OHTTP_MERINO];
    }

    const ohttpImagesEnabled = Prefs.values.ohttpImagesConfig?.enabled;
    const includeTopStoriesSection =
      Prefs.values.ohttpImagesConfig?.includeTopStoriesSection;

    const nonPersonalizedSections = ["top_stories_section"];
    const sectionPersonalized =
      !nonPersonalizedSections.includes(this.props.section) ||
      includeTopStoriesSection;

    const secureImage =
      ohttpImagesEnabled && ohttpEnabled && sectionPersonalized;

    return secureImage;
  }

  renderImage({ sizes = [], classNames = "" } = {}) {
    const { Prefs } = this.props;

    const rawImageSrc = this.getRawImageSrc();
    const smartCrop = Prefs.values["images.smart"];
    return (
      <DSImage
        extraClassNames={`img ${classNames}`}
        source={this.props.image_src}
        rawSource={rawImageSrc}
        sizes={sizes}
        url={this.props.url}
        title={this.props.title}
        isRecentSave={this.props.isRecentSave}
        alt_text={this.props.alt_text}
        smartCrop={smartCrop}
        secureImage={this.secureImage}
      />
    );
  }

  renderSectionCardImages() {
    const { sectionsCardImageSizes } = this.props;

    // Derived from the layout's breakpoints rather than a fixed list, so a
    // 5-column layout renders a 5th variant.
    const columns = Object.keys(sectionsCardImageSizes);

    return (
      <>
        {columns.map(column => {
          const size = sectionsCardImageSizes[column];
          const sizes = [this.getSectionImageSize(column, size)];
          const image = this.renderImage({
            sizes,
            classNames: `image-${column}`,
          });
          return React.cloneElement(image, { key: column });
        })}
      </>
    );
  }

  render() {
    const {
      isRecentSave,
      DiscoveryStream,
      Prefs,
      mayHaveSectionsCards,
      format,
    } = this.props;

    if (this.props.placeholder || !this.state.isSeen) {
      // placeholder-seen is used to ensure the loading animation is only used if the card is visible.
      const placeholderClassName = this.state.isSeen ? `placeholder-seen` : ``;
      let placeholderElements = (
        <>
          <div className="placeholder-image placeholder-fill" />
          <div className="placeholder-label placeholder-fill" />
          <div className="placeholder-header placeholder-fill" />
          <div className="placeholder-description placeholder-fill" />
        </>
      );

      placeholderElements = (
        <>
          <div className="placeholder-image placeholder-fill" />
          <div className="placeholder-description placeholder-fill" />
          <div className="placeholder-header placeholder-fill" />
        </>
      );
      return (
        <div
          className={`ds-card placeholder ${placeholderClassName}`}
          ref={this.setPlaceholderRef}
        >
          {placeholderElements}
        </div>
      );
    }

    let source = this.props.source || this.props.publisher;
    if (!source) {
      try {
        source = new URL(this.props.url).hostname;
      } catch (e) {}
    }

    const {
      hideDescriptions,
      compactImages,
      imageGradient,
      newSponsoredLabel,
      titleLines = 3,
      descLines = 3,
      readTime: displayReadTime,
    } = DiscoveryStream;

    const sectionsEnabled = Prefs.values[PREF_SECTIONS_ENABLED];
    // @nova-cleanup(remove-pref): Delete this read and the two novaEnabled props
    // passed to DSContextFooter below; that component drops the prop entirely.
    const novaEnabled = Prefs.values["nova.enabled"];
    // We can ignore hideDescriptions if we are in sections.
    const excerpt =
      !hideDescriptions || sectionsEnabled ? this.props.excerpt : "";

    let timeToRead;
    if (displayReadTime) {
      timeToRead =
        this.props.time_to_read || readTimeFromWordCount(this.props.word_count);
    }

    const ctaButtonEnabled = this.props.ctaButtonSponsors?.includes(
      this.props.sponsor?.toLowerCase()
    );
    let ctaButtonVariant = "";
    if (ctaButtonEnabled) {
      ctaButtonVariant = this.props.ctaButtonVariant;
    }
    let ctaButtonVariantClassName = ctaButtonVariant;

    const ctaButtonClassName = ctaButtonEnabled ? `ds-card-cta-button` : ``;
    const compactImagesClassName = compactImages ? `ds-card-compact-image` : ``;
    const imageGradientClassName = imageGradient
      ? `ds-card-image-gradient`
      : ``;
    const sectionsCardsClassName = [
      mayHaveSectionsCards ? `sections-card-ui` : ``,
      this.props.sectionsClassNames,
    ]
      .filter(Boolean)
      .join(" ");
    const titleLinesName = `ds-card-title-lines-${titleLines}`;
    const descLinesClassName = `ds-card-desc-lines-${descLines}`;
    const isMediumRectangle = format === "rectangle";
    let spocFormatClassName = ``;
    if (isMediumRectangle) {
      spocFormatClassName = `ds-spoc-rectangle`;
    } else if (format === "spoc") {
      spocFormatClassName = `ds-spoc`;
    }
    const faviconSrc = this.getFaviconSrc();

    let images = this.renderImage({ sizes: this.standardCardImageSizes });
    if (isMediumRectangle) {
      images = this.renderImage();
    } else if (sectionsEnabled) {
      images = this.renderSectionCardImages();
    }

    return (
      <article
        className={`ds-card ${sectionsCardsClassName} ${compactImagesClassName} ${imageGradientClassName} ${titleLinesName} ${descLinesClassName} ${spocFormatClassName} ${ctaButtonClassName} ${ctaButtonVariantClassName}`}
        ref={this.setContextMenuButtonHostRef}
        data-position-one={this.props["data-position-one"]}
        data-position-two={this.props["data-position-one"]}
        data-position-three={this.props["data-position-one"]}
        data-position-four={this.props["data-position-one"]}
      >
        <SafeAnchor
          className="ds-card-link"
          dispatch={this.props.dispatch}
          onLinkClick={!this.props.placeholder ? this.onLinkClick : undefined}
          onLinkOpenIntent={
            !this.props.placeholder ? this.onLinkOpenIntent : undefined
          }
          url={this.props.url}
          title={this.props.title}
          isSponsored={!!this.props.flightId}
          dwellLabel={this.props.flightId ? "story_sponsored" : "story_organic"}
          tabIndex={this.props.tabIndex}
          onFocus={this.props.onFocus}
        >
          <div className="img-wrapper">
            {images}
            {this.props.showEditorsPickBadge && (
              <moz-badge
                className="ds-card-editors-pick-badge"
                iconSrc="chrome://newtab/content/data/content/assets/glyph-heart-12.svg"
                data-l10n-id="newtab-editors-pick-badge"
              ></moz-badge>
            )}
          </div>
          {/* Only the carousel passes isActive, and only its visible slide gets
              true. This stays mounted while the slide is hidden, so a card that
              is shown more than once still reports a single impression. */}
          <ImpressionStats
            isActive={this.props.isActive !== false}
            flightId={this.props.flightId}
            rows={[
              {
                id: this.props.id,
                pos: this.props.pos,
                ...(this.props.shim && this.props.shim.impression
                  ? { shim: this.props.shim.impression }
                  : {}),
                corpus_item_id: this.props.corpus_item_id,
                scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
                recommended_at: this.props.recommended_at,
                received_rank: this.props.received_rank,
                variant_id: this.props.variant_id,
                source_section_id: this.props.source_section_id,
                topic: this.props.topic,
                features: this.props.features,
                ...(format ? { format } : {}),
                is_ad_eligible_position: this.props.is_ad_eligible_position,
                category: this.props.category,
                attribution: this.props.attribution,
                ...(this.props.section
                  ? {
                      section: this.props.section,
                      section_position: this.props.sectionPosition,
                      is_section_followed: this.props.sectionFollowed,
                      layout_name: this.props.sectionLayoutName,
                    }
                  : {}),
                ...(!format && this.props.section
                  ? // Note: sectionsCardsClassName is passed to ImpressionStats.jsx in order to calculate format
                    { class_names: sectionsCardsClassName }
                  : {}),
              },
            ]}
            dispatch={this.props.dispatch}
            source={this.props.type}
          />

          {ctaButtonVariant === "variant-b" && (
            <div className="cta-header">Shop Now</div>
          )}
          <DefaultMeta
            source={source}
            title={this.props.title}
            excerpt={excerpt}
            newSponsoredLabel={newSponsoredLabel}
            timeToRead={timeToRead}
            context={this.props.context}
            context_type={this.props.context_type}
            sponsor={this.props.sponsor}
            sponsored_by_override={this.props.sponsored_by_override}
            ctaButtonVariant={ctaButtonVariant}
            dispatch={this.props.dispatch}
            mayHaveSectionsCards={this.props.mayHaveSectionsCards}
            state={this.state}
            format={format}
            icon_src={faviconSrc}
            tabIndex={this.props.tabIndex}
            novaEnabled={novaEnabled}
          />
        </SafeAnchor>
        <div className="card-stp-button-hover-background">
          <div className="card-stp-button-position-wrapper">
            <DSLinkMenu
              id={this.props.id}
              index={this.props.pos}
              dispatch={this.props.dispatch}
              url={this.props.url}
              title={this.props.title}
              source={source}
              type={this.props.type}
              card_type={this.props.flightId ? "spoc" : "organic"}
              pocket_id={this.props.pocket_id}
              shim={this.props.shim}
              bookmarkGuid={this.props.bookmarkGuid}
              flightId={this.props.flightId}
              dwellLabel={
                this.props.flightId ? "story_sponsored" : "story_organic"
              }
              showPrivacyInfo={!!this.props.flightId}
              onMenuUpdate={this.onMenuUpdate}
              onMenuShow={this.onMenuShow}
              isRecentSave={isRecentSave}
              tile_id={this.props.id}
              block_key={this.props.id}
              corpus_item_id={this.props.corpus_item_id}
              scheduled_corpus_item_id={this.props.scheduled_corpus_item_id}
              recommended_at={this.props.recommended_at}
              received_rank={this.props.received_rank}
              section={this.props.section}
              section_position={this.props.sectionPosition}
              is_section_followed={this.props.sectionFollowed}
              format={
                format
                  ? format
                  : getActiveCardSize(
                      window.innerWidth,
                      this.props.sectionsClassNames,
                      this.props.section,
                      this.props.flightId,
                      getNovaColumnLayout(this.contextMenuButtonHostElement)
                    )
              }
              isSectionsCard={this.props.mayHaveSectionsCards}
              topic={this.props.topic}
              selected_topics={this.props.selected_topics}
              tabIndex={this.props.tabIndex}
            />
          </div>
        </div>
      </article>
    );
  }
}

_DSCard.defaultProps = {
  windowObj: window, // Added to support unit tests
};

export const DSCard = connect(state => ({
  App: state.App,
  DiscoveryStream: state.DiscoveryStream,
  Prefs: state.Prefs,
}))(_DSCard);

export const PlaceholderDSCard = () => <DSCard placeholder={true} />;
