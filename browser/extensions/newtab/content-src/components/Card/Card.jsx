/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { cardContextTypes } from "./types";
import { connect } from "react-redux";
import { ContextMenuButton } from "content-src/components/ContextMenu/ContextMenuButton";
import { formatRelativeDate } from "content-src/lib/utils";
import { getLinkMenuOptions } from "content-src/lib/link-menu-options";
import { LinkMenu } from "content-src/components/LinkMenu/LinkMenu";
import { PanelListItems } from "content-src/components/LinkMenu/PanelListItems";
import { subscribePanelListToggle } from "content-src/lib/panel-list-utils";
import React from "react";
import { ScreenshotUtils } from "content-src/lib/screenshot-utils";

// Keep track of pending image loads to only request once
const gImageLoading = new Map();

/**
 * Card component.
 * Cards are found within a Section component and contain information about a link such
 * as preview image, page title, page description, and some context about if the page
 * was visited, bookmarked, trending etc...
 * Each Section can make an unordered list of Cards which will create one instane of
 * this class. Each card will then get a context menu which reflects the actions that
 * can be done on this Card.
 */
export class _Card extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      activeCard: null,
      imageLoaded: false,
      cardImage: null,
    };
    this.onMenuButtonUpdate = this.onMenuButtonUpdate.bind(this);
    this.onLinkClick = this.onLinkClick.bind(this);
    this.panelListRef = React.createRef();
  }

  /**
   * Helper to conditionally load an image and update state when it loads.
   */
  async maybeLoadImage() {
    // No need to load if it's already loaded or no image
    const { cardImage } = this.state;
    if (!cardImage) {
      return;
    }

    const imageUrl = cardImage.url;
    if (!this.state.imageLoaded) {
      // Initialize a promise to share a load across multiple card updates
      if (!gImageLoading.has(imageUrl)) {
        const loaderPromise = new Promise((resolve, reject) => {
          const loader = new Image();
          loader.addEventListener("load", resolve);
          loader.addEventListener("error", reject);
          loader.src = imageUrl;
        });

        // Save and remove the promise only while it's pending
        gImageLoading.set(imageUrl, loaderPromise);
        loaderPromise
          .catch(ex => ex)
          .then(() => gImageLoading.delete(imageUrl));
      }

      // Wait for the image whether just started loading or reused promise
      try {
        await gImageLoading.get(imageUrl);
      } catch (ex) {
        // Ignore the failed image without changing state
        return;
      }

      // Only update state if we're still waiting to load the original image
      if (
        ScreenshotUtils.isRemoteImageLocal(
          this.state.cardImage,
          this.props.link.image
        ) &&
        !this.state.imageLoaded
      ) {
        this.setState({ imageLoaded: true });
      }
    }
  }

  static getDerivedStateFromProps(nextProps, prevState) {
    const { image } = nextProps.link;
    const imageInState = ScreenshotUtils.isRemoteImageLocal(
      prevState.cardImage,
      image
    );
    let nextState = null;

    // Image is updating.
    if (!imageInState && nextProps.link) {
      nextState = { imageLoaded: false };
    }

    if (imageInState) {
      return nextState;
    }

    // Since image was updated, attempt to revoke old image blob URL, if it exists.
    ScreenshotUtils.maybeRevokeBlobObjectURL(prevState.cardImage);

    nextState = nextState || {};
    nextState.cardImage = ScreenshotUtils.createLocalImageObject(image);

    return nextState;
  }

  onMenuButtonUpdate(isOpen) {
    if (isOpen) {
      this.setState({ activeCard: this.props.index });
    } else {
      this.setState({ activeCard: null });
    }
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    // Filter out "history" type for being the default
    if (this.props.link.type !== "history") {
      return { value: { card_type: this.props.link.type } };
    }

    return null;
  }

  onLinkClick(event) {
    event.preventDefault();
    const { altKey, button, ctrlKey, metaKey, shiftKey } = event;
    if (this.props.link.type === "download") {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.OPEN_DOWNLOAD_FILE,
          data: Object.assign(this.props.link, {
            event: { button, ctrlKey, metaKey, shiftKey },
          }),
        })
      );
    } else {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.OPEN_LINK,
          data: Object.assign(this.props.link, {
            event: { altKey, button, ctrlKey, metaKey, shiftKey },
          }),
        })
      );
    }
    if (this.props.isWebExtension) {
      this.props.dispatch(
        ac.WebExtEvent(at.WEBEXT_CLICK, {
          source: this.props.eventSource,
          url: this.props.link.url,
          action_position: this.props.index,
        })
      );
    } else {
      this.props.dispatch(
        ac.UserEvent(
          Object.assign(
            {
              event: "CLICK",
              source: this.props.eventSource,
              action_position: this.props.index,
            },
            this._getTelemetryInfo()
          )
        )
      );

      if (this.props.shouldSendImpressionStats) {
        this.props.dispatch(
          ac.ImpressionStats({
            source: this.props.eventSource,
            click: 0,
            tiles: [{ id: this.props.link.guid, pos: this.props.index }],
          })
        );
      }
    }
  }

  componentDidMount() {
    this.maybeLoadImage();
    // Nova only: mirror the panel-list's open state into the card's active state
    this.teardownMenuEvents = subscribePanelListToggle(
      this.panelListRef.current,
      {
        onShown: () => this.onMenuButtonUpdate(true),
        onHidden: () => this.onMenuButtonUpdate(false),
      }
    );
  }

  componentDidUpdate() {
    this.maybeLoadImage();
  }

  componentWillUnmount() {
    ScreenshotUtils.maybeRevokeBlobObjectURL(this.state.cardImage);
    this.teardownMenuEvents?.();
  }

  render() {
    const {
      index,
      className,
      link,
      dispatch,
      contextMenuOptions,
      eventSource,
      shouldSendImpressionStats,
    } = this.props;
    const { props } = this;
    const title = link.title || link.hostname;
    const isContextMenuOpen = this.state.activeCard === index;
    // Display "now" as "trending" until we have new strings #3402
    const { icon, fluentID } =
      cardContextTypes[link.type === "now" ? "trending" : link.type] || {};
    const hasImage = this.state.cardImage || link.hasImage;
    const imageStyle = {
      backgroundImage: this.state.cardImage
        ? `url(${this.state.cardImage.url})`
        : "none",
    };
    const outerClassName = [
      "card-outer",
      className,
      isContextMenuOpen && "active",
      props.placeholder && "placeholder",
    ]
      .filter(v => v)
      .join(" ");

    // @nova-cleanup(remove-conditional): Keep only the Nova markup
    if (props.novaEnabled) {
      const visitDate = link.lastVisitDate ?? link.date_added;
      const menuId = `highlight-context-menu-${index}`;
      return (
        <li className={outerClassName}>
          <a
            href={link.url}
            onClick={!props.placeholder ? this.onLinkClick : undefined}
          >
            <div className="card">
              <div className="card-preview-image-outer">
                {hasImage && (
                  <div
                    className={`card-preview-image${
                      this.state.imageLoaded ? " loaded" : ""
                    }`}
                    style={imageStyle}
                  />
                )}
              </div>
              <div className="card-details">
                <div className="card-host-name">
                  {link.hostname?.slice(0, 100)}
                </div>
                <h4 className="card-title clamp" dir="auto">
                  {title}
                </h4>
                {!props.placeholder && (
                  <div className="card-context">
                    {icon && (
                      <span className={`card-context-icon icon icon-${icon}`} />
                    )}
                    {fluentID && (
                      <span
                        className="card-context-type"
                        data-l10n-id={fluentID}
                      />
                    )}
                    {visitDate && (
                      <span className="card-context-label">
                        {formatRelativeDate(visitDate, props.locale)}
                      </span>
                    )}
                  </div>
                )}
              </div>
            </div>
          </a>
          {!props.placeholder && (
            <>
              <moz-button
                class="context-menu-button"
                type="icon ghost"
                size="small"
                iconsrc="chrome://global/skin/icons/more.svg"
                data-l10n-id="newtab-menu-content-tooltip"
                data-l10n-args={JSON.stringify({ title })}
                menuId={menuId}
              />
              <panel-list
                className="panel-list-no-icons"
                id={menuId}
                ref={this.panelListRef}
              >
                <PanelListItems
                  options={getLinkMenuOptions({
                    dispatch,
                    index,
                    source: eventSource,
                    isPrivateBrowsingEnabled: props.isPrivateBrowsingEnabled,
                    platform: props.platform,
                    privacyInfoUrl: props.privacyInfoUrl,
                    options: link.contextMenuOptions || contextMenuOptions,
                    site: link,
                    siteInfo: this._getTelemetryInfo(),
                    shouldSendImpressionStats,
                  })}
                />
              </panel-list>
            </>
          )}
        </li>
      );
    }

    return (
      <li className={outerClassName}>
        <a
          href={link.type === "pocket" ? link.open_url : link.url}
          onClick={!props.placeholder ? this.onLinkClick : undefined}
        >
          <div className="card">
            <div className="card-preview-image-outer">
              {hasImage && (
                <div
                  className={`card-preview-image${
                    this.state.imageLoaded ? " loaded" : ""
                  }`}
                  style={imageStyle}
                />
              )}
            </div>
            <div className="card-details">
              {link.type === "download" && (
                <div
                  className="card-host-name alternate"
                  data-l10n-id="newtab-menu-open-file"
                />
              )}
              {link.hostname && (
                <div className="card-host-name">
                  {link.hostname.slice(0, 100)}
                  {link.type === "download" && `  \u2014 ${link.description}`}
                </div>
              )}
              <div
                className={[
                  "card-text",
                  icon ? "" : "no-context",
                  link.description ? "" : "no-description",
                  link.hostname ? "" : "no-host-name",
                ].join(" ")}
              >
                <h4 className="card-title" dir="auto">
                  {link.title}
                </h4>
                <p className="card-description" dir="auto">
                  {link.description}
                </p>
              </div>
              <div className="card-context">
                {icon && !link.context && (
                  <span
                    aria-haspopup="true"
                    className={`card-context-icon icon icon-${icon}`}
                  />
                )}
                {link.icon && link.context && (
                  <span
                    aria-haspopup="true"
                    className="card-context-icon icon"
                    style={{ backgroundImage: `url('${link.icon}')` }}
                  />
                )}
                {fluentID && !link.context && (
                  <div className="card-context-label" data-l10n-id={fluentID} />
                )}
                {link.context && (
                  <div className="card-context-label">{link.context}</div>
                )}
              </div>
            </div>
          </div>
        </a>
        {!props.placeholder && (
          <ContextMenuButton
            tooltip="newtab-menu-content-tooltip"
            tooltipArgs={{ title }}
            onUpdate={this.onMenuButtonUpdate}
          >
            <LinkMenu
              dispatch={dispatch}
              index={index}
              source={eventSource}
              options={link.contextMenuOptions || contextMenuOptions}
              site={link}
              siteInfo={this._getTelemetryInfo()}
              shouldSendImpressionStats={shouldSendImpressionStats}
            />
          </ContextMenuButton>
        )}
      </li>
    );
  }
}
_Card.defaultProps = { link: {} };
export const Card = connect(state => ({
  platform: state.Prefs.values.platform,
  // @nova-cleanup(remove-pref): Remove novaEnabled
  novaEnabled: state.Prefs.values["nova.enabled"],
  locale: state.App.locale,
  isPrivateBrowsingEnabled: state.Prefs.values.isPrivateBrowsingEnabled,
  privacyInfoUrl: state.Prefs.values["privacyInfo.url"],
}))(_Card);
export const PlaceholderCard = props => (
  <Card placeholder={true} className={props.className} />
);
