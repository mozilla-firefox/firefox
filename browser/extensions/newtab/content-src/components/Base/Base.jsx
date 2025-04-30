/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { DiscoveryStreamAdmin } from "content-src/components/DiscoveryStreamAdmin/DiscoveryStreamAdmin";
import { ConfirmDialog } from "content-src/components/ConfirmDialog/ConfirmDialog";
import { connect } from "react-redux";
import { DiscoveryStreamBase } from "content-src/components/DiscoveryStreamBase/DiscoveryStreamBase";
import { ErrorBoundary } from "content-src/components/ErrorBoundary/ErrorBoundary";
import { CustomizeMenu } from "content-src/components/CustomizeMenu/CustomizeMenu";
import React from "react";
import { Search } from "content-src/components/Search/Search";
import { Sections } from "content-src/components/Sections/Sections";
import { Logo } from "content-src/components/Logo/Logo";
import { Weather } from "content-src/components/Weather/Weather";
import { DownloadModalToggle } from "content-src/components/DownloadModalToggle/DownloadModalToggle";
import { Notifications } from "content-src/components/Notifications/Notifications";
import { TopicSelection } from "content-src/components/DiscoveryStreamComponents/TopicSelection/TopicSelection";
import { DownloadMobilePromoHighlight } from "../DiscoveryStreamComponents/FeatureHighlight/DownloadMobilePromoHighlight";
import { WallpaperFeatureHighlight } from "../DiscoveryStreamComponents/FeatureHighlight/WallpaperFeatureHighlight";
import { MessageWrapper } from "content-src/components/MessageWrapper/MessageWrapper";

const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";
const PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_THUMBS_UP_DOWN_LAYOUT_ENABLED =
  "discoverystream.thumbsUpDown.searchTopsitesCompact";
const PREF_INFERRED_PERSONALIZATION_SYSTEM =
  "discoverystream.sections.personalization.inferred.enabled";
const PREF_INFERRED_PERSONALIZATION_USER =
  "discoverystream.sections.personalization.inferred.user.enabled";

export const PrefsButton = ({ onClick, icon }) => (
  <div className="prefs-button">
    <button
      className={`icon ${icon || "icon-settings"}`}
      onClick={onClick}
      data-l10n-id="newtab-settings-button"
    />
  </div>
);

// Returns a function will not be continuously triggered when called. The
// function will be triggered if called again after `wait` milliseconds.
function debounce(func, wait) {
  let timer;
  return (...args) => {
    if (timer) {
      return;
    }

    let wakeUp = () => {
      timer = null;
    };

    timer = setTimeout(wakeUp, wait);
    func.apply(this, args);
  };
}

export class _Base extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      message: {},
    };
    this.notifyContent = this.notifyContent.bind(this);
  }

  notifyContent(state) {
    this.setState(state);
  }

  componentWillUnmount() {
    this.updateTheme();
  }

  componentWillUpdate() {
    this.updateTheme();
  }

  updateTheme() {
    const bodyClassName = [
      "activity-stream",
      // If we skipped the about:welcome overlay and removed the CSS classes
      // we don't want to add them back to the Activity Stream view
      document.body.classList.contains("inline-onboarding")
        ? "inline-onboarding"
        : "",
    ]
      .filter(v => v)
      .join(" ");
    globalThis.document.body.className = bodyClassName;
  }

  render() {
    const { props } = this;
    const { App } = props;
    const isDevtoolsEnabled = props.Prefs.values["asrouter.devtoolsEnabled"];

    if (!App.initialized) {
      return null;
    }

    return (
      <ErrorBoundary className="base-content-fallback">
        <React.Fragment>
          <BaseContent {...this.props} adminContent={this.state} />
          {isDevtoolsEnabled ? (
            <DiscoveryStreamAdmin notifyContent={this.notifyContent} />
          ) : null}
        </React.Fragment>
      </ErrorBoundary>
    );
  }
}

export class BaseContent extends React.PureComponent {
  constructor(props) {
    super(props);
    this.openPreferences = this.openPreferences.bind(this);
    this.openCustomizationMenu = this.openCustomizationMenu.bind(this);
    this.closeCustomizationMenu = this.closeCustomizationMenu.bind(this);
    this.handleOnKeyDown = this.handleOnKeyDown.bind(this);
    this.onWindowScroll = debounce(this.onWindowScroll.bind(this), 5);
    this.setPref = this.setPref.bind(this);
    this.shouldShowOMCHighlight = this.shouldShowOMCHighlight.bind(this);
    this.updateWallpaper = this.updateWallpaper.bind(this);
    this.prefersDarkQuery = null;
    this.handleColorModeChange = this.handleColorModeChange.bind(this);
    this.shouldDisplayTopicSelectionModal =
      this.shouldDisplayTopicSelectionModal.bind(this);
    this.toggleDownloadHighlight = this.toggleDownloadHighlight.bind(this);
    this.handleDismissDownloadHighlight =
      this.handleDismissDownloadHighlight.bind(this);
    this.state = {
      fixedSearch: false,
      firstVisibleTimestamp: null,
      colorMode: "",
      fixedNavStyle: {},
      wallpaperTheme: "",
      showDownloadHighlight: this.shouldShowOMCHighlight(
        "DownloadMobilePromoHighlight"
      ),
    };
  }

  setFirstVisibleTimestamp() {
    if (!this.state.firstVisibleTimestamp) {
      this.setState({
        firstVisibleTimestamp: Date.now(),
      });
    }
  }

  componentDidMount() {
    global.addEventListener("scroll", this.onWindowScroll);
    global.addEventListener("keydown", this.handleOnKeyDown);
    const prefs = this.props.Prefs.values;
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    if (this.props.document.visibilityState === VISIBLE) {
      this.setFirstVisibleTimestamp();
      this.shouldDisplayTopicSelectionModal();
    } else {
      this._onVisibilityChange = () => {
        if (this.props.document.visibilityState === VISIBLE) {
          this.setFirstVisibleTimestamp();
          this.shouldDisplayTopicSelectionModal();
          this.props.document.removeEventListener(
            VISIBILITY_CHANGE_EVENT,
            this._onVisibilityChange
          );
          this._onVisibilityChange = null;
        }
      };
      this.props.document.addEventListener(
        VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
    // track change event to dark/light mode
    this.prefersDarkQuery = globalThis.matchMedia(
      "(prefers-color-scheme: dark)"
    );

    this.prefersDarkQuery.addEventListener(
      "change",
      this.handleColorModeChange
    );
    this.handleColorModeChange();
    if (wallpapersV2Enabled) {
      this.updateWallpaper();
    }
  }

  componentDidUpdate(prevProps) {
    const prefs = this.props.Prefs.values;
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    if (wallpapersV2Enabled) {
      // destructure current and previous props with fallbacks
      // (preventing undefined errors)
      const {
        Wallpapers: { uploadedWallpaper = null, wallpaperList = null } = {},
      } = this.props;

      const {
        Wallpapers: {
          uploadedWallpaper: prevUploadedWallpaper = null,
          wallpaperList: prevWallpaperList = null,
        } = {},
        Prefs: { values: prevPrefs = {} } = {},
      } = prevProps;

      const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];
      const prevSelectedWallpaper = prevPrefs["newtabWallpapers.wallpaper"];

      // don't update wallpaper unless the wallpaper is being changed.
      if (
        selectedWallpaper !== prevSelectedWallpaper || // selecting a new wallpaper
        uploadedWallpaper !== prevUploadedWallpaper || // uploading a new wallpaper
        wallpaperList !== prevWallpaperList // remote settings wallpaper list updates
      ) {
        this.updateWallpaper();
      }
    }
  }

  handleColorModeChange() {
    const colorMode = this.prefersDarkQuery?.matches ? "dark" : "light";
    this.setState({ colorMode });
  }

  componentWillUnmount() {
    this.prefersDarkQuery?.removeEventListener(
      "change",
      this.handleColorModeChange
    );
    global.removeEventListener("scroll", this.onWindowScroll);
    global.removeEventListener("keydown", this.handleOnKeyDown);
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(
        VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
  }

  onWindowScroll() {
    if (window.innerHeight <= 700) {
      // Bug 1937296: Only apply fixed-search logic
      // if the page is tall enough to support it.
      return;
    }

    const prefs = this.props.Prefs.values;
    const { showSearch } = prefs;

    if (!showSearch) {
      // Bug 1944718: Only apply fixed-search logic
      // if search is visible.
      return;
    }

    const logoAlwaysVisible = prefs["logowordmark.alwaysVisible"];
    const layoutsVariantAEnabled = prefs["newtabLayouts.variant-a"];
    const layoutsVariantBEnabled = prefs["newtabLayouts.variant-b"];
    const layoutsVariantAorB = layoutsVariantAEnabled || layoutsVariantBEnabled;
    const thumbsUpDownEnabled = prefs[PREF_THUMBS_UP_DOWN_ENABLED];
    // For the compact layout to be active,
    // thumbs also has to be enabled until Bug 1932242 is fixed
    const thumbsUpDownLayoutEnabled =
      prefs[PREF_THUMBS_UP_DOWN_LAYOUT_ENABLED] && thumbsUpDownEnabled;

    /* Bug 1917937: The logic presented below is fragile but accurate to the pixel. As new tab experiments with layouts, we have a tech debt of competing styles and classes the slightly modify where the search bar sits on the page. The larger solution for this is to replace everything with an intersection observer, but would require a larger refactor of this file. In the interim, we can programmatically calculate when to fire the fixed-scroll event and account for the moved elements so that topsites/etc stays in the same place. The CSS this references has been flagged to reference this logic so (hopefully) keep them in sync. */

    let SCROLL_THRESHOLD = 0; // When the fixed-scroll event fires
    let MAIN_OFFSET_PADDING = 0; // The padding to compensate for the moved elements

    let layout = {
      outerWrapperPaddingTop: 30,
      searchWrapperPaddingTop: 34,
      searchWrapperPaddingBottom: 38,
      searchWrapperFixedScrollPaddingTop: 27,
      searchWrapperFixedScrollPaddingBottom: 27,
      searchInnerWrapperMinHeight: 52,
      logoAndWordmarkWrapperHeight: 64,
      logoAndWordmarkWrapperMarginBottom: 48,
    };

    const CSS_VAR_SPACE_XXLARGE = 34.2; // Custom Acorn themed variable (8 * 0.267rem);

    // Experimental layouts
    // (Note these if statements are ordered to match the CSS cascade)
    if (thumbsUpDownLayoutEnabled || layoutsVariantAorB) {
      // Thumbs Compact View Layout
      if (thumbsUpDownLayoutEnabled) {
        layout.logoAndWordmarkWrapperMarginBottom = CSS_VAR_SPACE_XXLARGE;
        if (!logoAlwaysVisible) {
          layout.searchWrapperPaddingTop = CSS_VAR_SPACE_XXLARGE;
          layout.searchWrapperPaddingBottom = CSS_VAR_SPACE_XXLARGE;
        }
      }

      // Variant B Layout
      if (layoutsVariantAEnabled) {
        layout.outerWrapperPaddingTop = 24;
        if (!thumbsUpDownLayoutEnabled) {
          layout.searchWrapperPaddingTop = 0;
          layout.searchWrapperPaddingBottom = 32;
          layout.logoAndWordmarkWrapperMarginBottom = 32;
        }
      }

      // Variant B Layout
      if (layoutsVariantBEnabled) {
        layout.outerWrapperPaddingTop = 24;
        // Logo is positioned absolute, so remove it
        layout.logoAndWordmarkWrapperHeight = 0;
        layout.logoAndWordmarkWrapperMarginBottom = 0;
        layout.searchWrapperPaddingTop = 16;
        layout.searchWrapperPaddingBottom = CSS_VAR_SPACE_XXLARGE;
        if (!thumbsUpDownLayoutEnabled) {
          layout.searchWrapperPaddingBottom = 32;
        }
      }
    }

    // Logo visibility applies to all layouts
    if (!logoAlwaysVisible) {
      layout.logoAndWordmarkWrapperHeight = 0;
      layout.logoAndWordmarkWrapperMarginBottom = 0;
    }

    SCROLL_THRESHOLD =
      layout.outerWrapperPaddingTop +
      layout.searchWrapperPaddingTop +
      layout.logoAndWordmarkWrapperHeight +
      layout.logoAndWordmarkWrapperMarginBottom -
      layout.searchWrapperFixedScrollPaddingTop;

    MAIN_OFFSET_PADDING =
      layout.searchWrapperPaddingTop +
      layout.searchWrapperPaddingBottom +
      layout.searchInnerWrapperMinHeight +
      layout.logoAndWordmarkWrapperHeight +
      layout.logoAndWordmarkWrapperMarginBottom;

    // Edge case if logo and thums are turned off, but Var A is enabled
    if (SCROLL_THRESHOLD < 1) {
      SCROLL_THRESHOLD = 1;
    }

    if (global.scrollY > SCROLL_THRESHOLD && !this.state.fixedSearch) {
      this.setState({
        fixedSearch: true,
        fixedNavStyle: { paddingBlockStart: `${MAIN_OFFSET_PADDING}px` },
      });
    } else if (global.scrollY <= SCROLL_THRESHOLD && this.state.fixedSearch) {
      this.setState({ fixedSearch: false, fixedNavStyle: {} });
    }
  }

  openPreferences() {
    this.props.dispatch(ac.OnlyToMain({ type: at.SETTINGS_OPEN }));
    this.props.dispatch(ac.UserEvent({ event: "OPEN_NEWTAB_PREFS" }));
  }

  openCustomizationMenu() {
    this.props.dispatch({ type: at.SHOW_PERSONALIZE });
    this.props.dispatch(ac.UserEvent({ event: "SHOW_PERSONALIZE" }));
  }

  closeCustomizationMenu() {
    if (this.props.App.customizeMenuVisible) {
      this.props.dispatch({ type: at.HIDE_PERSONALIZE });
      this.props.dispatch(ac.UserEvent({ event: "HIDE_PERSONALIZE" }));
    }
  }

  handleOnKeyDown(e) {
    if (e.key === "Escape") {
      this.closeCustomizationMenu();
    }
  }

  setPref(pref, value) {
    this.props.dispatch(ac.SetPref(pref, value));
  }

  renderWallpaperAttribution() {
    const { wallpaperList } = this.props.Wallpapers;
    const activeWallpaper =
      this.props.Prefs.values[`newtabWallpapers.wallpaper`];
    const selected = wallpaperList.find(wp => wp.title === activeWallpaper);
    // make sure a wallpaper is selected and that the attribution also exists
    if (!selected?.attribution) {
      return null;
    }

    const { name: authorDetails, webpage } = selected.attribution;
    if (activeWallpaper && wallpaperList && authorDetails.url) {
      return (
        <p
          className={`wallpaper-attribution`}
          key={authorDetails.string}
          data-l10n-id="newtab-wallpaper-attribution"
          data-l10n-args={JSON.stringify({
            author_string: authorDetails.string,
            author_url: authorDetails.url,
            webpage_string: webpage.string,
            webpage_url: webpage.url,
          })}
        >
          <a data-l10n-name="name-link" href={authorDetails.url}>
            {authorDetails.string}
          </a>
          <a data-l10n-name="webpage-link" href={webpage.url}>
            {webpage.string}
          </a>
        </p>
      );
    }
    return null;
  }

  async updateWallpaper() {
    const prefs = this.props.Prefs.values;
    const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];
    const { wallpaperList, uploadedWallpaper } = this.props.Wallpapers;
    let lightWallpaper = {};
    let darkWallpaper = {};

    if (selectedWallpaper === "custom" && uploadedWallpaper) {
      // revoke ObjectURL to prevent memory leaks
      if (this.uploadedWallpaperUrl) {
        URL.revokeObjectURL(this.uploadedWallpaperUrl);
      }

      const uploadedWallpaperUrl = URL.createObjectURL(uploadedWallpaper);

      global.document?.body.style.setProperty(
        "--newtab-wallpaper",
        `url(${uploadedWallpaperUrl})`
      );

      global.document?.body.style.setProperty(
        "--newtab-wallpaper-color",
        "transparent"
      );

      return;
    }

    if (wallpaperList) {
      let wallpaper = wallpaperList.find(wp => wp.title === selectedWallpaper);
      if (selectedWallpaper && wallpaper) {
        // if selectedWallpaper exists - we override what light and dark prefs are to match that
        lightWallpaper = wallpaper;
        darkWallpaper = wallpaper;
      }

      // solid-color-picker-#00d100
      const regexRGB = /#([a-fA-F0-9]{6})/;

      // Override Remote Settings to set custom HEX bg color
      if (selectedWallpaper.includes("solid-color-picker")) {
        wallpaper = {
          theme: wallpaper?.theme || "light",
          title: "solid-color-picker",
          category: "solid-colors",
          solid_color: selectedWallpaper.match(regexRGB)?.[0],
        };
      }

      const wallpaperColor = wallpaper?.solid_color || "";

      global.document?.body.style.setProperty(
        "--newtab-wallpaper",
        `url(${wallpaper?.wallpaperUrl || ""})`
      );

      global.document?.body.style.setProperty(
        "--newtab-wallpaper-color",
        wallpaperColor || "transparent"
      );

      let wallpaperTheme = "";

      // If we have a solid colour set, let's see how dark it is.
      if (wallpaperColor) {
        const rgbColors = this.getRGBColors(wallpaperColor);
        const isColorDark = this.isWallpaperColorDark(rgbColors);
        wallpaperTheme = isColorDark ? "dark" : "light";
      } else {
        // Grab the contrast of the currently displayed wallpaper.
        const { theme } =
          this.state.colorMode === "light" ? lightWallpaper : darkWallpaper;

        if (theme) {
          wallpaperTheme = theme;
        }
      }

      this.setState({ wallpaperTheme });
    }
  }

  shouldShowOMCHighlight(componentId) {
    const messageData = this.props.Messages?.messageData;

    if (!messageData || Object.keys(messageData).length === 0) {
      return false;
    }

    return messageData?.content?.messageType === componentId;
  }

  toggleDownloadHighlight() {
    this.setState(prevState => ({
      showDownloadHighlight: !prevState.showDownloadHighlight,
    }));
  }

  handleDismissDownloadHighlight() {
    this.setState({ showDownloadHighlight: false });
  }

  getRGBColors(input) {
    if (input.length !== 7) {
      return [];
    }

    const r = parseInt(input.substr(1, 2), 16);
    const g = parseInt(input.substr(3, 2), 16);
    const b = parseInt(input.substr(5, 2), 16);

    return [r, g, b];
  }

  isWallpaperColorDark([r, g, b]) {
    return 0.2125 * r + 0.7154 * g + 0.0721 * b <= 110;
  }

  shouldDisplayTopicSelectionModal() {
    const prefs = this.props.Prefs.values;
    const pocketEnabled =
      prefs["feeds.section.topstories"] && prefs["feeds.system.topstories"];
    const topicSelectionOnboardingEnabled =
      prefs["discoverystream.topicSelection.onboarding.enabled"] &&
      pocketEnabled;
    const maybeShowModal =
      prefs["discoverystream.topicSelection.onboarding.maybeDisplay"];
    const displayTimeout =
      prefs["discoverystream.topicSelection.onboarding.displayTimeout"];
    const lastDisplayed =
      prefs["discoverystream.topicSelection.onboarding.lastDisplayed"];
    const displayCount =
      prefs["discoverystream.topicSelection.onboarding.displayCount"];

    if (
      !maybeShowModal ||
      !prefs["discoverystream.topicSelection.enabled"] ||
      !topicSelectionOnboardingEnabled
    ) {
      return;
    }

    const day = 24 * 60 * 60 * 1000;
    const now = new Date().getTime();

    const timeoutOccured = now - parseFloat(lastDisplayed) > displayTimeout;
    if (displayCount < 3) {
      if (displayCount === 0 || timeoutOccured) {
        this.props.dispatch(
          ac.BroadcastToContent({ type: at.TOPIC_SELECTION_SPOTLIGHT_OPEN })
        );
        this.setPref(
          "discoverystream.topicSelection.onboarding.displayTimeout",
          day
        );
      }
    }
  }

  // eslint-disable-next-line max-statements, complexity
  render() {
    const { props } = this;
    const { App, DiscoveryStream } = props;
    const { initialized, customizeMenuVisible } = App;
    const prefs = props.Prefs.values;

    const layoutsVariantAEnabled = prefs["newtabLayouts.variant-a"];
    const layoutsVariantBEnabled = prefs["newtabLayouts.variant-b"];
    const shortcutsRefresh = prefs["newtabShortcuts.refresh"];
    const layoutsVariantAorB = layoutsVariantAEnabled || layoutsVariantBEnabled;

    const activeWallpaper = prefs[`newtabWallpapers.wallpaper`];
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    const weatherEnabled = prefs.showWeather;
    const { showTopicSelection } = DiscoveryStream;
    const mayShowTopicSelection =
      showTopicSelection && prefs["discoverystream.topicSelection.enabled"];

    const { pocketConfig } = prefs;

    const isDiscoveryStream =
      props.DiscoveryStream.config && props.DiscoveryStream.config.enabled;
    let filteredSections = props.Sections.filter(
      section => section.id !== "topstories"
    );

    let spocMessageVariant = "";
    if (
      props.App.locale?.startsWith("en-") &&
      pocketConfig?.spocMessageVariant === "variant-c"
    ) {
      spocMessageVariant = pocketConfig.spocMessageVariant;
    }

    const pocketEnabled =
      prefs["feeds.section.topstories"] && prefs["feeds.system.topstories"];
    const noSectionsEnabled =
      !prefs["feeds.topsites"] &&
      !pocketEnabled &&
      filteredSections.filter(section => section.enabled).length === 0;
    const searchHandoffEnabled = prefs["improvesearch.handoffToAwesomebar"];
    const enabledSections = {
      topSitesEnabled: prefs["feeds.topsites"],
      pocketEnabled: prefs["feeds.section.topstories"],
      showInferredPersonalizationEnabled:
        prefs[PREF_INFERRED_PERSONALIZATION_USER],
      showRecentSavesEnabled: prefs.showRecentSaves,
      topSitesRowsCount: prefs.topSitesRows,
      weatherEnabled: prefs.showWeather,
    };

    const pocketRegion = prefs["feeds.system.topstories"];
    const mayHaveSponsoredStories = prefs["system.showSponsored"];
    const mayHaveInferredPersonalization =
      prefs[PREF_INFERRED_PERSONALIZATION_SYSTEM];
    const mayHaveWeather = prefs["system.showWeather"];
    const { mayHaveSponsoredTopSites } = prefs;
    const supportUrl = prefs["support.url"];

    // Mobile Download Promo Pref Checks
    const mobileDownloadPromoEnabled = prefs["mobileDownloadModal.enabled"];
    const mobileDownloadPromoVariantAEnabled =
      prefs["mobileDownloadModal.variant-a"];
    const mobileDownloadPromoVariantBEnabled =
      prefs["mobileDownloadModal.variant-b"];
    const mobileDownloadPromoVariantCEnabled =
      prefs["mobileDownloadModal.variant-c"];
    const mobileDownloadPromoVariantABorC =
      mobileDownloadPromoVariantAEnabled ||
      mobileDownloadPromoVariantBEnabled ||
      mobileDownloadPromoVariantCEnabled;
    const mobileDownloadPromoWrapperHeightModifier =
      prefs["weather.display"] === "detailed" &&
      weatherEnabled &&
      mayHaveWeather
        ? "is-tall"
        : "";

    const hasThumbsUpDownLayout =
      prefs["discoverystream.thumbsUpDown.searchTopsitesCompact"];
    const hasThumbsUpDown = prefs["discoverystream.thumbsUpDown.enabled"];
    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    const topicLabelsEnabled = prefs["discoverystream.topicLabels.enabled"];
    const sectionsCustomizeMenuPanelEnabled =
      prefs["discoverystream.sections.customizeMenuPanel.enabled"];
    const sectionsPersonalizationEnabled =
      prefs["discoverystream.sections.personalization.enabled"];
    // Logic to show follow/block topic mgmt panel in Customize panel
    const mayHavePersonalizedTopicSections =
      sectionsPersonalizationEnabled &&
      topicLabelsEnabled &&
      sectionsEnabled &&
      sectionsCustomizeMenuPanelEnabled &&
      DiscoveryStream.feeds.loaded;

    const featureClassName = [
      mobileDownloadPromoEnabled &&
        mobileDownloadPromoVariantABorC &&
        "has-mobile-download-promo", // Mobile download promo modal is enabled/visible
      weatherEnabled && mayHaveWeather && "has-weather", // Weather widget is enabled/visible
      prefs.showSearch ? "has-search" : "no-search",
      layoutsVariantAEnabled ? "layout-variant-a" : "", // Layout experiment variant A
      layoutsVariantBEnabled ? "layout-variant-b" : "", // Layout experiment variant B
      shortcutsRefresh ? "shortcuts-refresh" : "", // Shortcuts refresh experiment
      pocketEnabled ? "has-recommended-stories" : "no-recommended-stories",
      sectionsEnabled ? "has-sections-grid" : "",
    ]
      .filter(v => v)
      .join(" ");

    const outerClassName = [
      "outer-wrapper",
      isDiscoveryStream && pocketEnabled && "ds-outer-wrapper-search-alignment",
      isDiscoveryStream && "ds-outer-wrapper-breakpoint-override",
      prefs.showSearch &&
        this.state.fixedSearch &&
        !noSectionsEnabled &&
        "fixed-search",
      prefs.showSearch && noSectionsEnabled && "only-search",
      prefs["feeds.topsites"] &&
        !pocketEnabled &&
        !prefs.showSearch &&
        "only-topsites",
      noSectionsEnabled && "no-sections",
      prefs["logowordmark.alwaysVisible"] && "visible-logo",
      hasThumbsUpDownLayout && hasThumbsUpDown && "thumbs-ui-compact",
    ]
      .filter(v => v)
      .join(" ");
    if (wallpapersV2Enabled) {
      // Add helper class to body if user has a wallpaper selected
      if (this.state.wallpaperTheme === "light") {
        global.document?.body.classList.add("lightWallpaper");
        global.document?.body.classList.remove("darkWallpaper");
      }

      if (this.state.wallpaperTheme === "dark") {
        global.document?.body.classList.add("darkWallpaper");
        global.document?.body.classList.remove("lightWallpaper");
      }
    }

    return (
      <div className={featureClassName}>
        {/* Floating menu for customize menu toggle */}
        <menu className="personalizeButtonWrapper">
          <CustomizeMenu
            onClose={this.closeCustomizationMenu}
            onOpen={this.openCustomizationMenu}
            openPreferences={this.openPreferences}
            setPref={this.setPref}
            enabledSections={enabledSections}
            wallpapersV2Enabled={wallpapersV2Enabled}
            activeWallpaper={activeWallpaper}
            pocketRegion={pocketRegion}
            mayHaveTopicSections={mayHavePersonalizedTopicSections}
            mayHaveSponsoredTopSites={mayHaveSponsoredTopSites}
            mayHaveSponsoredStories={mayHaveSponsoredStories}
            mayHaveInferredPersonalization={mayHaveInferredPersonalization}
            mayHaveWeather={mayHaveWeather}
            spocMessageVariant={spocMessageVariant}
            showing={customizeMenuVisible}
          />
          {this.shouldShowOMCHighlight("CustomWallpaperHighlight") && (
            <MessageWrapper dispatch={this.props.dispatch}>
              <WallpaperFeatureHighlight
                position="inset-block-start inset-inline-start"
                dispatch={this.props.dispatch}
              />
            </MessageWrapper>
          )}
        </menu>
        <div className="weatherWrapper">
          {weatherEnabled && (
            <ErrorBoundary>
              <Weather />
            </ErrorBoundary>
          )}
        </div>
        <div
          className={`mobileDownloadPromoWrapper ${mobileDownloadPromoWrapperHeightModifier}`}
        >
          {mobileDownloadPromoEnabled && mobileDownloadPromoVariantABorC && (
            <ErrorBoundary>
              <DownloadModalToggle
                isActive={this.state.showDownloadHighlight}
                onClick={this.toggleDownloadHighlight}
              />
              {this.state.showDownloadHighlight && (
                <MessageWrapper
                  hiddenOverride={this.state.showDownloadHighlight}
                  onDismiss={this.handleDismissDownloadHighlight}
                  dispatch={this.props.dispatch}
                >
                  <DownloadMobilePromoHighlight
                    // Var B layout has the weather right-aligned
                    position={`${layoutsVariantBEnabled ? "inset-inline-start" : "inset-inline-end"} inset-block-end`}
                    dispatch={this.props.dispatch}
                  />
                </MessageWrapper>
              )}
            </ErrorBoundary>
          )}
        </div>

        {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions*/}
        <div className={outerClassName} onClick={this.closeCustomizationMenu}>
          <main className="newtab-main" style={this.state.fixedNavStyle}>
            {prefs.showSearch && (
              <div className="non-collapsible-section">
                <ErrorBoundary>
                  <Search
                    showLogo={
                      noSectionsEnabled || prefs["logowordmark.alwaysVisible"]
                    }
                    handoffEnabled={searchHandoffEnabled}
                    {...props.Search}
                  />
                </ErrorBoundary>
              </div>
            )}
            {/* Bug 1914055: Show logo regardless if search is enabled */}
            {!prefs.showSearch && layoutsVariantAorB && !noSectionsEnabled && (
              <Logo />
            )}
            <div className={`body-wrapper${initialized ? " on" : ""}`}>
              {isDiscoveryStream ? (
                <ErrorBoundary className="borderless-error">
                  <DiscoveryStreamBase
                    locale={props.App.locale}
                    mayHaveSponsoredStories={mayHaveSponsoredStories}
                    firstVisibleTimestamp={this.state.firstVisibleTimestamp}
                  />
                </ErrorBoundary>
              ) : (
                <Sections />
              )}
            </div>
            <ConfirmDialog />
            {wallpapersV2Enabled && this.renderWallpaperAttribution()}
          </main>
          <aside>
            {this.props.Notifications?.showNotifications && (
              <ErrorBoundary>
                <Notifications dispatch={this.props.dispatch} />
              </ErrorBoundary>
            )}
          </aside>
          {/* Only show the modal on currently visible pages (not preloaded) */}
          {mayShowTopicSelection && pocketEnabled && (
            <TopicSelection supportUrl={supportUrl} />
          )}
        </div>
      </div>
    );
  }
}

BaseContent.defaultProps = {
  document: global.document,
};

export const Base = connect(state => ({
  App: state.App,
  Prefs: state.Prefs,
  Sections: state.Sections,
  DiscoveryStream: state.DiscoveryStream,
  Messages: state.Messages,
  Notifications: state.Notifications,
  Search: state.Search,
  Wallpapers: state.Wallpapers,
  Weather: state.Weather,
}))(_Base);
