/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  isSponsored,
  MIN_RICH_FAVICON_SIZE,
  MIN_SMALL_FAVICON_SIZE,
  SPOC_TYPE,
  TOP_SITES_CONTEXT_MENU_OPTIONS,
  TOP_SITES_SPOC_CONTEXT_MENU_OPTIONS,
  TOP_SITES_SPONSORED_POSITION_CONTEXT_MENU_OPTIONS,
  TOP_SITES_SEARCH_SHORTCUTS_CONTEXT_MENU_OPTIONS,
  TOP_SITES_SOURCE,
} from "./TopSitesConstants";
import { PinnedAreaOverlay } from "./PinnedAreaOverlay.jsx";
import { TopSitesHoverCard } from "content-src/components/TopSitesHoverCard/TopSitesHoverCard";
import { TopSiteWebNotification } from "content-src/components/TopSiteWebNotification/TopSiteWebNotification";
import { getLinkMenuOptions } from "content-src/lib/link-menu-options";
import { PanelListItems } from "content-src/components/LinkMenu/PanelListItems";
import { subscribePanelListToggle } from "content-src/lib/panel-list-utils";
import { ImpressionStats } from "../DiscoveryStreamImpressionStats/ImpressionStats";
import React from "react";
import { ScreenshotUtils } from "content-src/lib/screenshot-utils";
import { TOP_SITES_MAX_SITES_PER_ROW } from "common/Reducers.sys.mjs";
import { TopSiteImpressionWrapper } from "./TopSiteImpressionWrapper";
import { connect } from "react-redux";

const NEWTAB_SOURCE = "newtab";
const PREF_OHTTP_UNIFIED_ADS = "unifiedAds.ohttp.enabled";
const PREF_OHTTP_UNIFIED_ADS_TILE_IMAGES =
  "unifiedAds.tiles.ohttpImages.enabled";

// Tilt so the lifted drag ghost reads as "picked up" (counter-clockwise).
const DRAG_GHOST_ROTATION_DEG = -7.5;
// Cursor relative to the ghost's top edge; negative = cursor sits just above it.
const DRAG_GHOST_CURSOR_INSET = -12;

/**
 * Clones the dragged tile, styles it (size + rotation, pin/menu chrome hidden via
 * `.drag-ghost`), and hands it to native setDragImage so the platform draws and
 * follows it. Mounted off-screen under the tile's parent to keep the cascade.
 * Caller MUST call destroy() on dragend.
 */
function createDragGhost(e, el) {
  const restingSize = Math.round(el.getBoundingClientRect().width);
  const mountPoint = el.parentElement || document.body;

  const clone = el.cloneNode(true);
  clone.classList.add("drag-ghost", "active");
  // Drop any open context menu from the clone so only the tile is lifted.
  clone.querySelector("panel-list")?.remove();
  // Size the ghost to the hover/expanded tile (--col-width, per Figma), falling
  // back to the resting size where that token isn't defined.
  clone.style.cssText = `position:fixed;top:-9999px;left:-9999px;width:var(--col-width, ${restingSize}px);height:var(--col-width, ${restingSize}px);margin:0;pointer-events:none;transform:rotate(${DRAG_GHOST_ROTATION_DEG}deg)`;
  mountPoint.appendChild(clone);

  // offsetWidth is the layout width, unaffected by the rotate transform.
  e.dataTransfer.setDragImage(
    clone,
    clone.offsetWidth / 2,
    DRAG_GHOST_CURSOR_INSET
  );

  return {
    destroy() {
      clone.remove();
    },
  };
}

export class TopSiteLink extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = { screenshotImage: null };
    this.onDragEvent = this.onDragEvent.bind(this);
    this.onKeyPress = this.onKeyPress.bind(this);
  }

  /*
   * Helper to determine whether the drop zone should allow a drop. We only allow
   * dropping top sites for now. We don't allow dropping on sponsored top sites
   * or the add shortcut button as their position is fixed.
   */
  _allowDrop(e) {
    if (!e.dataTransfer.types.includes("text/topsite-index")) {
      return false;
    }
    // Grouped reorder is bounded to the pinned block: only pinned tiles are
    // valid drop targets, so a tile can't be dropped out into the frecent area.
    if (this.props.groupedPinsEnabled) {
      return !!this.props.link.isPinned;
    }
    return this.dragged || !isSponsored(this.props.link);
  }

  onDragEvent(event) {
    switch (event.type) {
      case "click":
        // Stop any link clicks if we started any dragging
        if (this.dragged) {
          event.preventDefault();
        }
        break;
      case "dragstart":
        event.target.blur();
        if (isSponsored(this.props.link)) {
          event.preventDefault();
          break;
        }
        this.dragged = true;
        event.dataTransfer.effectAllowed = "move";
        event.dataTransfer.setData("text/topsite-index", this.props.index);
        if (this.props.groupedPinsEnabled) {
          // Lift a styled clone (rotated, hover-sized) as the drag image.
          this._dragGhost = createDragGhost(event, event.currentTarget);
        }
        this.props.onDragEvent(
          event,
          this.props.index,
          this.props.link,
          this.props.title
        );
        break;
      case "dragend":
        this._dragGhost?.destroy();
        this._dragGhost = null;
        this.props.onDragEvent(event);
        break;
      case "dragenter":
      case "dragover":
      case "drop":
        if (this._allowDrop(event)) {
          event.preventDefault();
          this.props.onDragEvent(event, this.props.index);
        }
        break;
      case "mousedown":
        // Block the scroll wheel from appearing for middle clicks on search top sites
        if (event.button === 1 && this.props.link.searchTopSite) {
          event.preventDefault();
        }
        // Reset at the first mouse event of a potential drag
        this.dragged = false;
        break;
    }
  }

  static getDerivedStateFromProps(nextProps, prevState) {
    const { screenshot } = nextProps.link;
    const imageInState = ScreenshotUtils.isRemoteImageLocal(
      prevState.screenshotImage,
      screenshot
    );
    if (imageInState) {
      return null;
    }

    // Since image was updated, attempt to revoke old image blob URL, if it exists.
    ScreenshotUtils.maybeRevokeBlobObjectURL(prevState.screenshotImage);

    return {
      screenshotImage: ScreenshotUtils.createLocalImageObject(screenshot),
    };
  }

  componentWillUnmount() {
    ScreenshotUtils.maybeRevokeBlobObjectURL(this.state.screenshotImage);
  }

  onKeyPress(event) {
    // If we have tabbed to a search shortcut top site, and we click 'enter',
    // we should execute the onClick function. This needs to be added because
    // search top sites are anchor tags without an href. See bug 1483135
    if (event.key === "Enter" && this.props.link.searchTopSite) {
      this.props.onClick(event);
    }
  }

  /*
   * Takes the url as a string, runs it through a simple (non-secure) hash turning it into a random number
   * Apply that random number to the color array. The same url will always generate the same color.
   */
  generateColor() {
    let { title, colors } = this.props;
    if (!colors) {
      return "";
    }

    let colorArray = colors.split(",");

    const hashStr = str => {
      let hash = 0;
      for (let i = 0; i < str.length; i++) {
        let charCode = str.charCodeAt(i);
        hash += charCode;
      }
      return hash;
    };

    let hash = hashStr(title);
    let index = hash % colorArray.length;
    return colorArray[index];
  }

  /**
   * The favicon URL to render. Sponsored tiles are loaded over OHTTP
   * through the moz-cached-ohttp protocol when enabled.
   *
   * @returns {string|undefined} The favicon URL.
   */
  getFaviconURL() {
    const { link, ohttpSponsoredImagesEnabled } = this.props;
    if (
      isSponsored(link) &&
      ohttpSponsoredImagesEnabled &&
      link.favicon?.startsWith("https:")
    ) {
      return `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(link.favicon)}`;
    }
    return link.favicon;
  }

  calculateStyle() {
    const { defaultStyle, link } = this.props;

    const { tippyTopIcon, faviconSize } = link;
    let imageClassName;
    let imageStyle;
    let showSmallFavicon = false;
    let smallFaviconStyle;
    let hasScreenshotImage =
      this.state.screenshotImage && this.state.screenshotImage.url;
    let selectedColor;

    if (defaultStyle) {
      // force no styles (letter fallback) even if the link has imagery
      selectedColor = this.generateColor();
    } else if (link.searchTopSite) {
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: `url(${tippyTopIcon})`,
      };
      smallFaviconStyle = { backgroundImage: `url(${tippyTopIcon})` };
    } else if (link.customScreenshotURL) {
      // assume high quality custom screenshot and use rich icon styles and class names
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: hasScreenshotImage
          ? `url(${this.state.screenshotImage.url})`
          : "",
      };
    } else if (
      tippyTopIcon ||
      link.type === SPOC_TYPE ||
      faviconSize >= MIN_RICH_FAVICON_SIZE
    ) {
      // styles and class names for top sites with rich icons
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: `url(${tippyTopIcon || this.getFaviconURL()})`,
      };
    } else if (faviconSize >= MIN_SMALL_FAVICON_SIZE) {
      showSmallFavicon = true;
      smallFaviconStyle = { backgroundImage: `url(${this.getFaviconURL()})` };
    } else {
      selectedColor = this.generateColor();
      imageClassName = "";
    }

    return {
      showSmallFavicon,
      smallFaviconStyle,
      imageStyle,
      imageClassName,
      selectedColor,
    };
  }

  render() {
    const {
      children,
      className,
      isDraggable,
      link,
      onClick,
      title,
      visibleTopSites,
    } = this.props;

    const topSiteOuterClassName = `top-site-outer${
      className ? ` ${className}` : ""
    }${link.isDragged ? " dragged" : ""}${
      link.isCollapsed ? " collapsed" : ""
    }${link.searchTopSite ? " search-shortcut" : ""}`;
    const [letterFallback] = title;
    const {
      showSmallFavicon,
      smallFaviconStyle,
      imageStyle,
      imageClassName,
      selectedColor,
    } = this.calculateStyle();

    const addPinnedTitlel10n = {
      "data-l10n-id": "topsite-label-pinned",
      "data-l10n-args": JSON.stringify({ title }),
    };

    let draggableProps = {};
    if (isDraggable) {
      draggableProps = {
        onClick: this.onDragEvent,
        onDragEnd: this.onDragEvent,
        onDragStart: this.onDragEvent,
        onMouseDown: this.onDragEvent,
      };
    }

    let impressionStats = null;
    if (link.type === SPOC_TYPE) {
      // Record impressions for Pocket tiles.
      impressionStats = (
        <ImpressionStats
          flightId={link.flightId}
          rows={[
            {
              id: link.id,
              pos: link.pos,
              shim: link.shim && link.shim.impression,
              advertiser_name: title.toLocaleLowerCase(),
            },
          ]}
          dispatch={this.props.dispatch}
          source={TOP_SITES_SOURCE}
        />
      );
    } else if (isSponsored(link)) {
      // Record impressions for non-Pocket sponsored tiles.
      impressionStats = (
        <TopSiteImpressionWrapper
          actionType={at.TOP_SITES_SPONSORED_IMPRESSION_STATS}
          tile={{
            position: this.props.index,
            tile_id: link.sponsored_tile_id || -1,
            reporting_url: link.sponsored_impression_url,
            advertiser_name: title.toLocaleLowerCase(),
            source: NEWTAB_SOURCE,
            visible_topsites: visibleTopSites,
            frecency_boosted: link.type === "frecency-boost",
            attribution: link.attribution,
            is_ad_eligible_position: link.is_ad_eligible_position,
          }}
          // For testing.
          IntersectionObserver={this.props.IntersectionObserver}
          document={this.props.document}
          dispatch={this.props.dispatch}
        />
      );
    } else {
      // Record impressions for organic tiles.
      impressionStats = (
        <TopSiteImpressionWrapper
          actionType={at.TOP_SITES_ORGANIC_IMPRESSION_STATS}
          tile={{
            position: this.props.index,
            source: NEWTAB_SOURCE,
            isPinned: this.props.link.isPinned,
            guid: this.props.link.guid,
            visible_topsites: visibleTopSites,
            smart_scores: this.props.link.scores,
            smart_weights: this.props.link.weights,
            is_ad_eligible_position: this.props.link.is_ad_eligible_position,
          }}
          // For testing.
          IntersectionObserver={this.props.IntersectionObserver}
          document={this.props.document}
          dispatch={this.props.dispatch}
        />
      );
    }

    return (
      <li
        className={topSiteOuterClassName}
        ref={this.props.setRef}
        {...(this.props.groupedPinsEnabled && {
          // The drop-zone overlay measures the pinned block by data-index.
          "data-index": this.props.index,
        })}
        {...(!this.props.dropsOnList && {
          // Per-tile drop targets (classic + grouped reorder). Zero-pin moves
          // these to the list, since it has a single synthetic target.
          onDrop: this.onDragEvent,
          onDragOver: this.onDragEvent,
          onDragEnter: this.onDragEvent,
          onDragLeave: this.onDragEvent,
        })}
        {...draggableProps}
      >
        <div className="top-site-inner">
          {/* We don't yet support an accessible drag-and-drop implementation, see Bug 1552005 */}
          {/* eslint-disable-next-line jsx-a11y/anchor-is-valid */}
          <a
            className="top-site-button"
            href={link.searchTopSite ? undefined : link.url}
            tabIndex={this.props.tabIndex}
            onKeyPress={this.onKeyPress}
            onClick={onClick}
            draggable={true}
            data-is-sponsored-link={!!link.sponsored_tile_id}
            onFocus={this.props.onFocus}
            aria-label={link.isPinned ? undefined : title}
            title={title}
            {...(link.isPinned && { ...addPinnedTitlel10n })}
            data-l10n-args={JSON.stringify({ title })}
          >
            <div className="tile" aria-hidden={true}>
              <div className="icon-stack">
                <div
                  className={
                    selectedColor
                      ? "icon-wrapper letter-fallback"
                      : "icon-wrapper"
                  }
                  data-fallback={letterFallback}
                  style={
                    selectedColor ? { backgroundColor: selectedColor } : {}
                  }
                >
                  <div className={imageClassName} style={imageStyle} />
                  {showSmallFavicon && (
                    <div
                      className="top-site-icon default-icon"
                      data-fallback={smallFaviconStyle ? "" : letterFallback}
                      style={smallFaviconStyle}
                    />
                  )}
                </div>
                <TopSiteWebNotification link={link} />
              </div>
            </div>
            {link.isPinned && <div className="icon icon-pin-small" />}
            <div
              className={`title${link.isPinned ? " has-icon pinned" : ""}${
                link.type === SPOC_TYPE || link.show_sponsored_label
                  ? " sponsored"
                  : ""
              }`}
            >
              <span className="title-label" dir="auto">
                {link.searchTopSite && (
                  <div className="top-site-icon search-topsite" />
                )}
                {title}
              </span>
              <span
                className="sponsored-label"
                data-l10n-id="newtab-topsite-sponsored"
              />
            </div>
          </a>
          {children}
          {impressionStats}
          <TopSitesHoverCard link={link} />
        </div>
        {this.props.addButton}
      </li>
    );
  }
}
TopSiteLink.defaultProps = {
  title: "",
  link: {},
  isDraggable: true,
};

export class TopSite extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = { showContextMenu: false };
    this.onLinkClick = this.onLinkClick.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.panelListRef = React.createRef();
    this.menuButtonRef = React.createRef();
    this.onMenuShown = this.onMenuShown.bind(this);
    this.onMenuHidden = this.onMenuHidden.bind(this);
    this.onMenuButtonMouseDown = this.onMenuButtonMouseDown.bind(this);
    this.onMenuButtonClick = this.onMenuButtonClick.bind(this);
    this.onMenuButtonKeyDown = this.onMenuButtonKeyDown.bind(this);
  }

  componentDidMount() {
    // The panel-list is persistent, so mirror its open/close state into the
    // tile's active flag via panel-list's shown/hidden events (replacing
    // ContextMenuButton's onUpdate callback).
    this.teardownMenuEvents = subscribePanelListToggle(
      this.panelListRef.current,
      { onShown: this.onMenuShown, onHidden: this.onMenuHidden }
    );

    // Register the trigger as the panel-list's popover invoker. panel-list is a
    // popover="auto", so without this the platform treats a click on the
    // trigger as a click outside the popover and light-dismisses it on
    // pointerup, right after our mousedown opened it. The invoker relationship
    // is what exempts it (see nsINode::GetTopmostClickedPopover). moz-button
    // does the same thing in its MenuController. Our click handler calls
    // preventDefault, which cancels the invoker's own default toggle, so the
    // menu is not toggled twice.
    if (this.menuButtonRef.current && this.panelListRef.current) {
      this.menuButtonRef.current.popoverTargetElement =
        this.panelListRef.current;
    }
  }

  componentWillUnmount() {
    this.teardownMenuEvents?.();
  }

  onMenuShown() {
    this.onMenuUpdate(true);
  }

  onMenuHidden() {
    this.onMenuUpdate(false);
  }

  /**
   * Opens the menu on mousedown rather than click. panel-list hides itself on
   * any document mousedown landing outside the panel, so toggling on click
   * would close it on mousedown and immediately reopen it on click, leaving the
   * menu stuck open. Toggling here runs before that document listener, and
   * panel-list's hide() then records this event so the listener ignores it.
   *
   * @param {MouseEvent} event
   */
  onMenuButtonMouseDown(event) {
    if (event.button !== 0) {
      return;
    }
    this.panelListRef.current?.toggle(event, event.currentTarget);
  }

  /**
   * Activations that produce no mousedown still arrive as a click: a
   * programmatic .click() and a keyboard-generated click both carry detail 0.
   * Real mouse clicks (detail >= 1) were already handled on mousedown and must
   * not toggle a second time here. This mirrors moz-button's MenuController.
   *
   * @param {MouseEvent} event
   */
  onMenuButtonClick(event) {
    event.preventDefault();
    if (!event.detail) {
      this.panelListRef.current?.toggle(event, event.currentTarget);
    }
  }

  /**
   * Keyboard activation. The event is handed to panel-list so it can tell this
   * apart from a pointer open, which is what makes it focus the first item and
   * return focus to the trigger on close. preventDefault stops the browser
   * synthesizing a click, which would toggle the menu straight back closed.
   *
   * @param {KeyboardEvent} event
   */
  onMenuButtonKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.panelListRef.current?.toggle(event, event.currentTarget);
    }
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    const value = { icon_type: this.props.link.iconType };
    // Filter out "not_pinned" type for being the default
    if (this.props.link.isPinned) {
      value.card_type = "pinned";
    }
    if (this.props.link.searchTopSite) {
      // Set the card_type as "search" regardless of its pinning status
      value.card_type = "search";
      value.search_vendor = this.props.link.hostname;
    }
    if (isSponsored(this.props.link)) {
      value.card_type = "spoc";
    }
    return { value };
  }

  userEvent(event) {
    this.props.dispatch(
      ac.UserEvent(
        Object.assign(
          {
            event,
            source: TOP_SITES_SOURCE,
            action_position: this.props.index,
          },
          this._getTelemetryInfo()
        )
      )
    );
  }

  onLinkClick(event) {
    this.userEvent("CLICK");

    // Specially handle a top site link click for "typed" frecency bonus as
    // specified as a property on the link.
    event.preventDefault();
    const { altKey, button, ctrlKey, metaKey, shiftKey } = event;
    if (!this.props.link.searchTopSite) {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.OPEN_LINK,
          data: Object.assign(this.props.link, {
            event: { altKey, button, ctrlKey, metaKey, shiftKey },
            is_sponsored: !!this.props.link.sponsored_tile_id,
            dwell_label: isSponsored(this.props.link)
              ? "topsite_sponsored"
              : "topsite_organic",
          }),
        })
      );

      if (this.props.link.type === SPOC_TYPE) {
        // Record a Pocket-specific click.
        this.props.dispatch(
          ac.ImpressionStats({
            source: TOP_SITES_SOURCE,
            click: 0,
            tiles: [
              {
                id: this.props.link.id,
                pos: this.props.link.pos,
                shim: this.props.link.shim && this.props.link.shim.click,
              },
            ],
          })
        );

        // Record a click for a Pocket sponsored tile.
        // This first event is for the shim property
        // and is used by our ad service provider.
        this.props.dispatch(
          ac.DiscoveryStreamUserEvent({
            event: "CLICK",
            source: TOP_SITES_SOURCE,
            action_position: this.props.link.pos,
            value: {
              card_type: "spoc",
              tile_id: this.props.link.id,
              shim: this.props.link.shim && this.props.link.shim.click,
              attribution: this.props.link.attribution,
            },
          })
        );

        // A second event is recoded for internal usage.
        const title = this.props.link.label || this.props.link.hostname;
        this.props.dispatch(
          ac.OnlyToMain({
            type: at.TOP_SITES_SPONSORED_IMPRESSION_STATS,
            data: {
              type: "click",
              position: this.props.link.pos,
              tile_id: this.props.link.id,
              advertiser_name: title.toLocaleLowerCase(),
              source: NEWTAB_SOURCE,
              attribution: this.props.link.attribution,
            },
          })
        );
      } else if (isSponsored(this.props.link)) {
        // Record a click for a non-Pocket sponsored tile.
        const title = this.props.link.label || this.props.link.hostname;
        this.props.dispatch(
          ac.OnlyToMain({
            type: at.TOP_SITES_SPONSORED_IMPRESSION_STATS,
            data: {
              type: "click",
              position: this.props.index,
              tile_id: this.props.link.sponsored_tile_id || -1,
              reporting_url: this.props.link.sponsored_click_url,
              advertiser_name: title.toLocaleLowerCase(),
              source: NEWTAB_SOURCE,
              visible_topsites: this.props.visibleTopSites,
              frecency_boosted: this.props.link.type === "frecency-boost",
              attribution: this.props.link.attribution,
            },
          })
        );
      } else {
        // Record a click for an organic tile.
        this.props.dispatch(
          ac.OnlyToMain({
            type: at.TOP_SITES_ORGANIC_IMPRESSION_STATS,
            data: {
              type: "click",
              position: this.props.index,
              source: NEWTAB_SOURCE,
              isPinned: this.props.link.isPinned,
              guid: this.props.link.guid,
              visible_topsites: this.props.visibleTopSites,
              smart_scores: this.props.link.scores,
              smart_weights: this.props.link.weights,
            },
          })
        );
      }
    } else {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.FILL_SEARCH_TERM,
          data: { label: this.props.link.label },
        })
      );
    }
  }

  onMenuUpdate(isOpen) {
    if (isOpen) {
      this.props.onActivate(this.props.index);
    } else {
      this.props.onActivate();
    }
  }

  render() {
    const { props } = this;
    const { link } = props;
    const isContextMenuOpen = props.activeIndex === props.index;
    const title = link.label || link.title || link.hostname;
    let menuOptions;
    if (link.sponsored_position) {
      menuOptions = TOP_SITES_SPONSORED_POSITION_CONTEXT_MENU_OPTIONS;
    } else if (link.searchTopSite) {
      menuOptions = TOP_SITES_SEARCH_SHORTCUTS_CONTEXT_MENU_OPTIONS;
    } else if (link.type === SPOC_TYPE) {
      menuOptions = TOP_SITES_SPOC_CONTEXT_MENU_OPTIONS;
    } else {
      menuOptions = TOP_SITES_CONTEXT_MENU_OPTIONS;
    }

    const menuId = `topsite-context-menu-${props.index}`;
    const options = getLinkMenuOptions({
      dispatch: props.dispatch,
      index: props.index,
      source: TOP_SITES_SOURCE,
      isPrivateBrowsingEnabled: props.isPrivateBrowsingEnabled,
      platform: props.platform,
      privacyInfoUrl: props.privacyInfoUrl,
      options: menuOptions,
      site: link,
      shouldSendImpressionStats: link.type === SPOC_TYPE,
      siteInfo: this._getTelemetryInfo(),
    });

    return (
      <TopSiteLink
        {...props}
        onClick={this.onLinkClick}
        onDragEvent={this.props.onDragEvent}
        className={`${props.className || ""}${
          isContextMenuOpen ? " active" : ""
        }`}
        title={title}
        setPref={this.props.setPref}
        tabIndex={this.props.tabIndex}
        onFocus={this.props.onFocus}
      >
        <div>
          {/* Deliberately a plain <button> rather than moz-button, unlike the
              other menus in this bug. The tile hides this button until it is
              hovered or focused, which needs :focus-visible to tell keyboard
              focus from the pointer focus moz-button retains after its menu
              closes. moz-button delegates focus into its shadow root, and a
              shadow host never matches :focus-visible, so that is not
              expressible in CSS today. Bug 2062844 adds the state we need;
              switch this to moz-button once it has ridden to release. */}
          <button
            className="context-menu-button icon"
            aria-haspopup="menu"
            aria-expanded={isContextMenuOpen}
            data-l10n-id="newtab-menu-content-tooltip"
            data-l10n-args={JSON.stringify({ title })}
            tabIndex={this.props.tabIndex}
            ref={this.menuButtonRef}
            onFocus={this.props.onFocus}
            onMouseDown={this.onMenuButtonMouseDown}
            onClick={this.onMenuButtonClick}
            onKeyDown={this.onMenuButtonKeyDown}
          />
          <panel-list
            className="panel-list-no-icons"
            id={menuId}
            ref={this.panelListRef}
          >
            <PanelListItems options={options} />
          </panel-list>
        </div>
      </TopSiteLink>
    );
  }
}
TopSite.defaultProps = {
  link: {},
  onActivate() {},
};

export class TopSiteAddButton extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onEditButtonClick = this.onEditButtonClick.bind(this);
  }

  onEditButtonClick() {
    this.props.dispatch({
      type: at.TOP_SITES_EDIT,
      data: { index: this.props.index },
    });
  }

  render() {
    // In-grid buttons are large to match the tile icons; the full-row hover
    // overlay uses the default size. Both variants participate in the shortcuts'
    // arrow-key navigation.
    const button = (
      <moz-button
        type="primary"
        className="add-button"
        tabIndex={this.props.tabIndex}
        onFocus={this.props.onFocus}
        {...(this.props.inGrid && { size: "large" })}
        iconsrc="chrome://global/skin/icons/plus.svg"
        data-l10n-id="newtab-topsites-add-shortcut-title"
        onClick={this.onEditButtonClick}
      />
    );

    if (this.props.inGrid) {
      return (
        <li
          className={`top-site-outer add-button-tile ${
            this.props.className ? `${this.props.className}` : ""
          }`}
        >
          {button}
        </li>
      );
    }

    // For a full row, overlay the button on the last tile; reveal on hover or
    // when the shortcuts row has focus.
    return <div className="add-button-hidden">{button}</div>;
  }
}

export class TopSitePlaceholder extends React.PureComponent {
  render() {
    return (
      <TopSiteLink
        {...this.props}
        className={`placeholder ${this.props.className || ""}`}
        isDraggable={false}
      />
    );
  }
}

// The classic path renders this mode-agnostically. The grouped-pins path extends
// it through an explicit, opt-in prop contract (all inert when groupedPinsEnabled
// is false), kept here intentionally rather than composed via children/render-prop:
//   - groupedPinsEnabled: turns on the grouped-mode behavior below
//   - listProps / dropsOnList: zero-pin variant makes the whole <ul> one drop target
//   - decorations: zero-pin placeholder slot (zeroPinSlot/overZeroPin/setZeroPinRef)
//   - listRef: hands the <ul> back for the zero-pin drop geometry
//   - PinnedAreaOverlay (rendered inside the <ul>) self-measures .pinned-cell tiles
// Longer term these could be composed from the container (children/render-prop)
// so this list goes back to being fully mode-agnostic.
export class _TopSiteList extends React.PureComponent {
  static get DEFAULT_STATE() {
    return {
      activeIndex: null,
      focusedIndex: 0,
    };
  }

  constructor(props) {
    super(props);
    this.state = _TopSiteList.DEFAULT_STATE;
    this.onActivate = this.onActivate.bind(this);
    this.onWrapperFocus = this.onWrapperFocus.bind(this);
    this.onTopsiteFocus = this.onTopsiteFocus.bind(this);
    this.onWrapperBlur = this.onWrapperBlur.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onListDragLeave = this.onListDragLeave.bind(this);
    this.onListDragOver = this.onListDragOver.bind(this);
  }

  componentDidUpdate(prevProps) {
    // Drag state lives in the hook now; mirror the old reset of our own view
    // state (menu + focus) off the dragged-site signal.
    const started = !prevProps.draggedSite && this.props.draggedSite;
    const ended = prevProps.draggedSite && !this.props.draggedSite;
    if (started || ended) {
      // eslint-disable-next-line react/no-did-update-set-state
      this.setState(ended ? _TopSiteList.DEFAULT_STATE : { activeIndex: null });
    }

    // Keyboard focus: the shortcuts share one roving tab stop (focusedIndex). If
    // the row count shrinks, the focused tile may no longer be rendered, leaving
    // the row with no tab stop — reset to the first tile to keep the row focusable.
    if (prevProps.TopSitesRows !== this.props.TopSitesRows) {
      // eslint-disable-next-line react/no-did-update-set-state
      this.setState({ focusedIndex: 0 });
    }
  }

  onActivate(index) {
    this.setState({ activeIndex: index });
  }

  onKeyDown(e) {
    if (this.state.activeIndex || this.state.activeIndex === 0) {
      return;
    }

    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") {
      return;
    }
    // Arrow direction should match visual navigation direction in RTL
    const isRTL = document.dir === "rtl";
    const navigateToPrevious = isRTL
      ? e.key === "ArrowRight"
      : e.key === "ArrowLeft";

    // Walk a flat, DOM-ordered list of focus targets: each tile's link plus
    // the add-button.
    const focusTargets = [...this.focusRef.querySelectorAll("a, .add-button")];
    const currentIndex = focusTargets.indexOf(e.target);
    if (currentIndex === -1) {
      return;
    }
    // Wrap around the row: stepping forward past the last target returns to the
    // first, and vice versa. The extra `+ count` keeps the modulo positive when
    // wrapping backward from index 0.
    const count = focusTargets.length;
    const delta = navigateToPrevious ? -1 : 1;
    const target = focusTargets[(currentIndex + delta + count) % count];
    if (target) {
      target.tabIndex = 0;
      target.focus();
    }
  }

  onWrapperFocus() {
    this.focusRef?.addEventListener("keydown", this.onKeyDown);
  }
  onWrapperBlur() {
    this.focusRef?.removeEventListener("keydown", this.onKeyDown);
  }
  onTopsiteFocus(focusIndex) {
    this.setState(() => ({
      focusedIndex: focusIndex,
    }));
  }

  // dragover fires continuously on whatever's under the cursor, so it's the
  // reliable "current element" signal (dragenter/dragleave order can't be
  // trusted between adjacent tiles). The reflow should only live while the
  // cursor is within the pinned drop region (the purple outline), so hit-test
  // the live overlay boxes: each box spans a whole pinned row, so crossing the
  // gaps between pins stays inside and doesn't flicker. Classic has no pinned
  // region, so it keeps its original behavior.
  onListDragOver(event) {
    // Preserve any list-level handler (zero-pin grouped drop geometry).
    this.props.listProps?.onDragOver?.(event);
    if (!this.props.groupedPinsEnabled) {
      return;
    }
    const boxes = [...event.currentTarget.querySelectorAll(".pinned-drop-box")];
    if (!boxes.length) {
      return;
    }
    const inside = boxes.some(box => {
      const r = box.getBoundingClientRect();
      return (
        event.clientX >= r.left &&
        event.clientX <= r.right &&
        event.clientY >= r.top &&
        event.clientY <= r.bottom
      );
    });
    if (!inside) {
      this.props.onDragEvent(event);
    }
  }

  // Safety net for the grouped pinned-region clear: leaving the grid straight
  // off an edge tile, where no in-region dragover lands first. Filters out
  // tile-to-tile crossings via relatedTarget. Grouped-only, so classic keeps its
  // original "placeholder persists until drop/dragend" behavior.
  onListDragLeave(event) {
    // Preserve any list-level handler (e.g. zero-pin grouped drop).
    this.props.listProps?.onDragLeave?.(event);
    if (!this.props.groupedPinsEnabled) {
      return;
    }
    if (!event.currentTarget.contains(event.relatedTarget)) {
      this.props.onDragEvent(event);
    }
  }

  render() {
    const { props } = this;
    const topSites = this.props.sites;
    const maxSitesPerRow =
      props.topSitesMaxSitesPerRow ?? TOP_SITES_MAX_SITES_PER_ROW;
    // The array can be sparse (e.g. dismissed tiles leave holes), so count the
    // tiles that actually render rather than relying on slot indices.
    const tileCount = topSites.filter(site => site && !site.isAddButton).length;
    const rowFull = tileCount > 0 && tileCount % maxSitesPerRow === 0;
    const topSitesUI = [];
    const commonProps = {
      onDragEvent: this.props.onDragEvent,
      dispatch: props.dispatch,
      groupedPinsEnabled: this.props.groupedPinsEnabled,
      // Zero-pin drops on the list (single synthetic target, no per-tile
      // handlers). The reorder+append path keeps per-tile handlers and adds a
      // list-level append target, so it passes listProps without this flag.
      dropsOnList: !!this.props.dropsOnList,
      // Prefs needed by getLinkMenuOptions (previously supplied by LinkMenu's
      // own connect()). TopSite now builds the options itself for panel-list.
      isPrivateBrowsingEnabled:
        this.props.Prefs.values.isPrivateBrowsingEnabled,
      platform: this.props.Prefs.values.platform,
      privacyInfoUrl: this.props.Prefs.values["privacyInfo.url"],
      ohttpSponsoredImagesEnabled: !!(
        this.props.Prefs.values[PREF_OHTTP_UNIFIED_ADS] &&
        (this.props.Prefs.values[PREF_OHTTP_UNIFIED_ADS_TILE_IMAGES] ||
          this.props.Prefs.values.trainhopConfig?.ohttpSponsoredTileImages
            ?.enabled)
      ),
    };
    const { decorations } = this.props;
    // We assign a key to each placeholder slot. We need it to be independent
    // of the slot index (i below) so that the keys used stay the same during
    // drag and drop reordering and the underlying DOM nodes are reused.
    // This mostly (only?) affects linux so be sure to test on linux before changing.
    let holeIndex = 0;

    // On narrow viewports, we only show 6 sites per row. We'll mark the rest as
    // .hide-for-narrow to hide in CSS via @media query.
    const novaEnabled = this.props.Prefs.values["nova.enabled"];
    const maxNarrowVisibleIndex = props.TopSitesRows * 6;
    const maxSmallVisibleIndex = props.TopSitesRows * 8;

    for (let i = 0, l = topSites.length; i < l; i++) {
      // Zero-pin grouped drag: with no pins there are no slots to reorder, so
      // open the pin area with one placeholder at the first pinnable slot.
      if (decorations && i === decorations.zeroPinSlot) {
        topSitesUI.push(
          <TopSitePlaceholder
            key="pinned-zero-drop"
            index={i}
            {...commonProps}
            className={`zero-pin-placeholder pinned-cell${
              decorations.overZeroPin ? " over" : ""
            }`}
            setRef={decorations.setZeroPinRef}
          />
        );
      }
      const link =
        topSites[i] &&
        Object.assign({}, topSites[i], {
          iconType: this.props.topSiteIconType(topSites[i]),
        });

      const slotProps = {
        // Stable key so the button isn't remounted (and re-flashed) as its slot
        // shifts on pin/unpin — it has no url to key off of.
        key: link?.isAddButton
          ? "add-button"
          : link?.url || `hole-${holeIndex++}`,
        index: i,
      };
      // @nova-cleanup(remove-conditional): Remove classic path once Nova ships
      if (novaEnabled) {
        if (i >= maxSmallVisibleIndex) {
          slotProps.className = "nova-hide-for-s";
        } else if (i >= maxNarrowVisibleIndex) {
          slotProps.className = "nova-hide-for-xs";
        }
      } else if (i >= maxSmallVisibleIndex) {
        slotProps.className = "hide-for-small";
      } else if (i >= maxNarrowVisibleIndex) {
        slotProps.className = "hide-for-narrow";
      }
      // Marker (no styles) the drop-zone overlay measures to size the box. The
      // preview marks a joining frecent isPinned, so the box grows with it.
      if (this.props.groupedPinsEnabled && link?.isPinned) {
        slotProps.className = `${
          slotProps.className ? `${slotProps.className} ` : ""
        }pinned-cell`;
      }
      const { key: slotKey, ...restSlotProps } = slotProps;

      let topSiteLink = null;
      // Use a placeholder if the link is empty or it's rendering a sponsored
      // tile for the about:home startup cache.
      if (
        !link ||
        (props.App.isForStartupCache.TopSites && isSponsored(link))
      ) {
        if (link) {
          topSiteLink = (
            <TopSitePlaceholder
              key={slotKey}
              {...restSlotProps}
              {...commonProps}
            />
          );
        }
      } else if (topSites[i]?.isAddButton) {
        // Render the add button in-grid when the row isn't full. It carries the
        // slot's responsive hide class so it drops at the same breakpoints as a
        // tile in that column would.
        if (!rowFull) {
          topSiteLink = (
            <TopSiteAddButton
              inGrid={true}
              className={slotProps.className}
              key={slotKey}
              index={i}
              dispatch={props.dispatch}
              tabIndex={i === this.state.focusedIndex ? 0 : -1}
              onFocus={() => {
                this.onTopsiteFocus(i);
              }}
            />
          );
        }
      } else {
        // When this is the last tile of a full row, the add button has no free
        // cell of its own, so render it as a hover overlay anchored to this tile.
        let addButton = null;
        if (topSites[i + 1]?.isAddButton && rowFull) {
          addButton = (
            <TopSiteAddButton
              index={i + 1}
              dispatch={props.dispatch}
              tabIndex={i + 1 === this.state.focusedIndex ? 0 : -1}
              onFocus={() => {
                this.onTopsiteFocus(i + 1);
              }}
            />
          );
        }
        topSiteLink = (
          <TopSite
            key={slotKey}
            link={link}
            activeIndex={this.state.activeIndex}
            onActivate={this.onActivate}
            {...restSlotProps}
            {...commonProps}
            colors={props.colors}
            tabIndex={i === this.state.focusedIndex ? 0 : -1}
            onFocus={() => {
              this.onTopsiteFocus(i);
            }}
            visibleTopSites={this.props.visibleTopSites}
            addButton={addButton}
          />
        );
      }

      // Skip empty slots — topSiteLink is null when there's no link and no placeholder.
      if (topSiteLink) {
        topSitesUI.push(topSiteLink);
      }
    }
    return (
      <div className="top-sites-list-wrapper">
        <ul
          role="group"
          aria-label="Shortcuts"
          onFocus={this.onWrapperFocus}
          onBlur={this.onWrapperBlur}
          {...this.props.listProps}
          onDragOver={this.onListDragOver}
          onDragLeave={this.onListDragLeave}
          ref={el => {
            this.focusRef = el;
            this.props.listRef?.(el);
          }}
          className={`top-sites-list${
            this.props.draggedSite ? " dnd-active" : ""
          }`}
          style={{
            "--top-sites-max-per-row":
              this.props.topSitesMaxSitesPerRow ?? TOP_SITES_MAX_SITES_PER_ROW,
          }}
        >
          {topSitesUI}
          {this.props.groupedPinsEnabled && (
            <PinnedAreaOverlay
              active={!!this.props.draggedSite}
              revision={topSites}
            />
          )}
        </ul>
      </div>
    );
  }
}

export const TopSiteList = connect(state => ({
  App: state.App,
  Prefs: state.Prefs,
}))(_TopSiteList);
