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
import { BaseContext } from "content-src/lib/BaseContext";
import { CUSTOMIZE_SUBPANELS } from "content-src/lib/constants";
import React, { useState, useEffect } from "react";
import { Search } from "content-src/components/Search/Search";
import { TopSites } from "content-src/components/TopSites/TopSites";
import { Sections } from "content-src/components/Sections/Sections";
import { Logo } from "content-src/components/Logo/Logo";
import { Weather } from "content-src/components/Weather/Weather";
import { WidgetsSidebar } from "content-src/components/Widgets/WidgetsSidebar";
import { DownloadModalToggle } from "content-src/components/DownloadModalToggle/DownloadModalToggle";
import { Notifications } from "content-src/components/Notifications/Notifications";
import { TopicSelection } from "content-src/components/DiscoveryStreamComponents/TopicSelection/TopicSelection";
import { DownloadMobilePromoHighlight } from "../DiscoveryStreamComponents/FeatureHighlight/DownloadMobilePromoHighlight";
import { WallpaperFeatureHighlight } from "../DiscoveryStreamComponents/FeatureHighlight/WallpaperFeatureHighlight";
import { ActivationWindowMessage } from "../ActivationWindowMessage/ActivationWindowMessage";
import { MessageWrapper } from "content-src/components/MessageWrapper/MessageWrapper";
import { ExternalComponentWrapper } from "content-src/components/ExternalComponentWrapper/ExternalComponentWrapper";
import {
  ASROUTER_NEWTAB_MESSAGE_POSITIONS,
  shouldShowOMCHighlight,
  shouldShowASRouterNewTabMessage,
} from "../../lib/asrouter-message-utils.mjs";
import {
  WIDGET_REGISTRY,
  hasContentAreaWidgets,
  isWeatherAvailable,
  isWidgetEnabled,
  isWidgetToggleVisible,
  isWidgetsContainerVisible,
} from "common/WidgetsRegistry.mjs";
import {
  isSideBySideActive,
  isSpaceOverridden,
  isSpacesActive,
  isSpacesArrowsAssigned,
  SPACE_IDS,
  sideBySideBandClasses,
  spacesBandClasses,
} from "common/PageLayoutVariants.mjs";

const CLOSED_SUBPANELS = {
  activeSubpanel: null,
  wallpapersPanelCategory: null,
};

const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";
// Scroll distances in pixels, in ascending order, that each record a scroll
// telemetry event the first time they are passed in a session.
const SCROLL_TELEMETRY_THRESHOLDS = [50, 100, 250];
const PREF_INFERRED_PERSONALIZATION_SYSTEM =
  "discoverystream.sections.personalization.inferred.enabled";
const PREF_INFERRED_PERSONALIZATION_USER =
  "discoverystream.sections.personalization.inferred.user.enabled";
// @nova-cleanup(remove-pref): Remove PREF_NOVA_ENABLED
const PREF_NOVA_ENABLED = "nova.enabled";
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

export function WithDsAdmin(props) {
  const { hash = globalThis?.location?.hash || "" } = props;

  const [devtoolsCollapsed, setDevtoolsCollapsed] = useState(
    !hash.startsWith("#devtools")
  );

  useEffect(() => {
    const onHashChange = () => {
      const h = globalThis?.location?.hash || "";
      setDevtoolsCollapsed(!h.startsWith("#devtools"));
    };

    // run once in case hash changed before mount
    onHashChange();

    globalThis?.addEventListener("hashchange", onHashChange);
    return () => globalThis?.removeEventListener("hashchange", onHashChange);
  }, []);

  return (
    <>
      <DiscoveryStreamAdmin devtoolsCollapsed={devtoolsCollapsed} />
      {devtoolsCollapsed ? <BaseContent {...props} /> : null}
    </>
  );
}

export function _Base(props) {
  const isDevtoolsEnabled = props.Prefs.values["asrouter.devtoolsEnabled"];
  const { App } = props;

  if (!App.initialized) {
    return null;
  }

  return (
    <ErrorBoundary className="base-content-fallback">
      {isDevtoolsEnabled ? (
        <WithDsAdmin {...props} />
      ) : (
        <BaseContent {...props} />
      )}
    </ErrorBoundary>
  );
}

export class BaseContent extends React.PureComponent {
  constructor(props) {
    super(props);
    this.openPreferences = this.openPreferences.bind(this);
    this.openCustomizationMenu = this.openCustomizationMenu.bind(this);
    this.closeCustomizationMenu = this.closeCustomizationMenu.bind(this);
    this.onWindowScroll = debounce(this.onWindowScroll.bind(this), 5);
    this.setPref = this.setPref.bind(this);
    this.updateWallpaper = this.updateWallpaper.bind(this);
    this.prefersDarkQuery = null;
    this.handleColorModeChange = this.handleColorModeChange.bind(this);
    this.onVisible = this.onVisible.bind(this);
    this.toggleDownloadHighlight = this.toggleDownloadHighlight.bind(this);
    this.handleDismissDownloadHighlight =
      this.handleDismissDownloadHighlight.bind(this);
    this.applyBodyClasses = this.applyBodyClasses.bind(this);
    this.toggleSectionsMgmtPanel = this.toggleSectionsMgmtPanel.bind(this);
    this.toggleWidgetsManagementPanel =
      this.toggleWidgetsManagementPanel.bind(this);
    this.toggleThemesPanel = this.toggleThemesPanel.bind(this);
    this.openWallpapersPanel = this.openWallpapersPanel.bind(this);
    this.closeWallpapersPanel = this.closeWallpapersPanel.bind(this);
    this.closeSubpanels = this.closeSubpanels.bind(this);
    this.openWidgetsPanel = this.openWidgetsPanel.bind(this);
    this.attachSearchSentinel = this.attachSearchSentinel.bind(this);
    this.onSearchSentinelIntersect = this.onSearchSentinelIntersect.bind(this);
    this.searchStickyObserver = null;
    this._nextScrollThreshold = 0;
    this.state = {
      fixedSearch: false,
      colorMode: "",
      fixedNavStyle: {},
      wallpaperTheme: "",
      showDownloadHighlightOverride: null,
      visible: false,
      ...CLOSED_SUBPANELS,
    };
    this.spocPlaceholderStartTime = null;
  }

  attachSearchSentinel(el) {
    if (this.searchStickyObserver) {
      this.searchStickyObserver.disconnect();
      this.searchStickyObserver = null;
    }
    if (el) {
      this.searchStickyObserver = new IntersectionObserver(
        this.onSearchSentinelIntersect,
        { threshold: 0 }
      );
      this.searchStickyObserver.observe(el);
    } else if (this.state.fixedSearch) {
      this.setState({ fixedSearch: false });
    }
  }

  onSearchSentinelIntersect(entries) {
    const entry = entries[entries.length - 1];
    const stuck = !entry.isIntersecting && entry.boundingClientRect.top < 0;
    if (stuck !== this.state.fixedSearch) {
      this.setState({ fixedSearch: stuck });
    }
  }

  onVisible() {
    this.setState({
      visible: true,
    });
    this.shouldDisplayTopicSelectionModal();
    this.onVisibilityDispatch();

    if (this.isSpocsOnDemandExpired && !this.spocPlaceholderStartTime) {
      this.spocPlaceholderStartTime = Date.now();
    }
  }

  onVisibilityDispatch() {
    const { onDemand = {} } = this.props.DiscoveryStream.spocs;

    // We only need to dispatch this if:
    // 1. onDemand is enabled,
    // 2. onDemand spocs have not been loaded on this tab.
    // 3. Spocs are expired.
    if (onDemand.enabled && !onDemand.loaded && this.isSpocsOnDemandExpired) {
      // This dispatches that spocs are expired and we need to update them.
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.DISCOVERY_STREAM_SPOCS_ONDEMAND_UPDATE,
        })
      );
    }
  }

  get isSpocsOnDemandExpired() {
    const {
      onDemand = {},
      cacheUpdateTime,
      lastUpdated,
    } = this.props.DiscoveryStream.spocs;

    // We can bail early if:
    // 1. onDemand is off,
    // 2. onDemand spocs have been loaded on this tab.
    if (!onDemand.enabled || onDemand.loaded) {
      return false;
    }

    return Date.now() - lastUpdated >= cacheUpdateTime;
  }

  spocsOnDemandUpdated() {
    const { onDemand = {}, loaded } = this.props.DiscoveryStream.spocs;

    // We only need to fire this if:
    // 1. Spoc data is loaded.
    // 2. onDemand is enabled.
    // 3. The component is visible (not preloaded tab).
    // 4. onDemand spocs have not been loaded on this tab.
    // 5. Spocs are not expired.
    if (
      loaded &&
      onDemand.enabled &&
      this.state.visible &&
      !onDemand.loaded &&
      !this.isSpocsOnDemandExpired
    ) {
      // This dispatches that spocs have been loaded on this tab
      // and we don't need to update them again for this tab.
      this.props.dispatch(
        ac.BroadcastToContent({ type: at.DISCOVERY_STREAM_SPOCS_ONDEMAND_LOAD })
      );
    }
  }

  componentDidMount() {
    this.applyBodyClasses();
    global.addEventListener("scroll", this.onWindowScroll);
    const prefs = this.props.Prefs.values;
    const novaEnabled = prefs[PREF_NOVA_ENABLED];
    const wallpapersEnabled = prefs["newtabWallpapers.enabled"];
    const wallpapersUserEnabled = prefs["newtabWallpapers.user.enabled"];

    if (this.props.document.visibilityState === VISIBLE) {
      this.onVisible();
    } else {
      this._onVisibilityChange = () => {
        if (this.props.document.visibilityState === VISIBLE) {
          this.onVisible();
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
    const isWallpaperVisible = novaEnabled
      ? wallpapersEnabled && wallpapersUserEnabled
      : wallpapersEnabled;
    if (isWallpaperVisible) {
      this.updateWallpaper();
    }

    this._onHashChange = () => {
      const hash = globalThis.location?.hash || "";
      if (hash === "#customize" || hash === "#customize-topics") {
        this.openCustomizationMenu();

        if (hash === "#customize-topics") {
          this.toggleSectionsMgmtPanel();
        }
      } else if (hash === "#customize-widgets") {
        const currentPrefs = this.props.Prefs.values;
        // The widgets subpanel only exists with Nova and a visible widgets
        // container. Opening it in any other state leaves the main view
        // unscrollable, so open the main view, where the widget toggles are
        // without Nova.
        // @nova-cleanup(remove-conditional): Remove the novaEnabled check; keep the isWidgetsContainerVisible check and both branches
        if (
          currentPrefs[PREF_NOVA_ENABLED] &&
          isWidgetsContainerVisible(currentPrefs)
        ) {
          this.openWidgetsPanel();
        } else {
          this.openCustomizationMenu();
        }
      } else if (this.props.App.customizeMenuVisible) {
        this.closeCustomizationMenu();
      }
    };

    // Using the Performance API to detect page reload vs fresh navigation.
    // Only open customize menu on fresh navigation, not on page refresh.
    // See: https://developer.mozilla.org/en-US/docs/Web/API/Performance/getEntriesByType
    // See: https://developer.mozilla.org/en-US/docs/Web/API/PerformanceEntry/entryType#navigation
    // See: https://developer.mozilla.org/en-US/docs/Web/API/PerformanceNavigationTiming/type
    const isReload =
      globalThis.performance?.getEntriesByType("navigation")[0]?.type ===
      "reload";

    if (!isReload) {
      this._onHashChange();
    }

    globalThis.addEventListener("hashchange", this._onHashChange);
  }

  componentDidUpdate(prevProps) {
    this.applyBodyClasses();
    const prefs = this.props.Prefs.values;

    // Check if weather widget was re-enabled from customization menu
    // @nova-cleanup(remove-conditional): Remove novaEnabledInUpdate and weatherPref variables; replace wasWeatherDisabled/isWeatherEnabled with direct reads of prevProps/props.Prefs.values["widgets.weather.enabled"]
    const novaEnabledInUpdate = this.props.Prefs.values["nova.enabled"];
    const weatherPref = novaEnabledInUpdate
      ? "widgets.weather.enabled"
      : "showWeather";
    const wasWeatherDisabled = !prevProps.Prefs.values[weatherPref];
    const isWeatherEnabled = this.props.Prefs.values[weatherPref];

    if (wasWeatherDisabled && isWeatherEnabled) {
      // If weather widget was enabled from customization menu, display opt-in dialog
      this.props.dispatch(ac.SetPref("weather.optInDisplayed", true));
    }

    const novaEnabled = prefs[PREF_NOVA_ENABLED];
    const wallpapersEnabled = prefs["newtabWallpapers.enabled"];
    const wallpapersUserEnabled = prefs["newtabWallpapers.user.enabled"];
    // Previous values of the wallpaper prefs, used to compare against the
    // current values and detect what changed since the last render.
    const prevNovaEnabled = prevProps.Prefs.values[PREF_NOVA_ENABLED];
    const prevWallpapersEnabled =
      prevProps.Prefs.values["newtabWallpapers.enabled"];
    const prevWallpapersUserEnabled =
      prevProps.Prefs.values["newtabWallpapers.user.enabled"];

    const isWallpaperActive = novaEnabled
      ? wallpapersEnabled && wallpapersUserEnabled
      : wallpapersEnabled;
    // This checks if the wallpaper was active before this update so that we can
    // detect when it just turned off and clear it from the background.
    const wasWallpaperActive = prevNovaEnabled
      ? prevWallpapersEnabled && prevWallpapersUserEnabled
      : prevWallpapersEnabled;

    if (isWallpaperActive) {
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
      const initialWallpaper = prefs["newtabWallpapers.initialWallpaper"];
      const prevInitialWallpaper =
        prevPrefs["newtabWallpapers.initialWallpaper"];
      const uploadedWallpaperTheme =
        prefs["newtabWallpapers.customWallpaper.theme"];
      const prevUploadedWallpaperTheme =
        prevPrefs["newtabWallpapers.customWallpaper.theme"];
      const uploadedWallpaperPosition =
        prefs["newtabWallpapers.customWallpaper.position"];
      const prevUploadedWallpaperPosition =
        prevPrefs["newtabWallpapers.customWallpaper.position"];

      // don't update wallpaper unless the wallpaper is being changed.
      if (
        !wasWallpaperActive || // the wallpaper wasn't active last render but is now, meaning it was just enabled, force an apply even if nothing else changed
        selectedWallpaper !== prevSelectedWallpaper || // selecting a new wallpaper
        initialWallpaper !== prevInitialWallpaper || // experiment sets initial wallpaper
        uploadedWallpaper !== prevUploadedWallpaper || // uploading a new wallpaper
        wallpaperList !== prevWallpaperList || // remote settings wallpaper list updates
        this.props.App.isForStartupCache.Wallpaper !==
          prevProps.App.isForStartupCache.Wallpaper || // Startup cached page wallpaper is updating
        uploadedWallpaperTheme !== prevUploadedWallpaperTheme ||
        uploadedWallpaperPosition !== prevUploadedWallpaperPosition
      ) {
        this.updateWallpaper();
      }
    } else if (wasWallpaperActive) {
      // The wallpaper was active last render but isn't anymore, meaning it was just turned off — clear it from the background
      this.updateWallpaper();
    }

    this.spocsOnDemandUpdated();
    this.trackSpocPlaceholderDuration(prevProps);
  }

  trackSpocPlaceholderDuration(prevProps) {
    // isExpired returns true when the current props have expired spocs (showing placeholders)
    const isExpired = this.isSpocsOnDemandExpired;

    // Init tracking when placeholders become visible
    if (isExpired && this.state.visible && !this.spocPlaceholderStartTime) {
      this.spocPlaceholderStartTime = Date.now();
    }

    // wasExpired returns true when the previous props had expired spocs (showing placeholders)
    const wasExpired =
      prevProps.DiscoveryStream.spocs.onDemand?.enabled &&
      !prevProps.DiscoveryStream.spocs.onDemand?.loaded &&
      Date.now() - prevProps.DiscoveryStream.spocs.lastUpdated >=
        prevProps.DiscoveryStream.spocs.cacheUpdateTime;

    // Record duration telemetry event when placeholders are replaced with real content
    if (wasExpired && !isExpired && this.spocPlaceholderStartTime) {
      const duration = Date.now() - this.spocPlaceholderStartTime;
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
          data: { duration },
        })
      );
      this.spocPlaceholderStartTime = null;
    }
  }

  handleColorModeChange() {
    const colorMode = this.prefersDarkQuery?.matches ? "dark" : "light";
    if (colorMode !== this.state.colorMode) {
      this.setState({ colorMode });
      this.updateWallpaper();
    }
  }

  componentWillUnmount() {
    this.prefersDarkQuery?.removeEventListener(
      "change",
      this.handleColorModeChange
    );
    global.removeEventListener("scroll", this.onWindowScroll);
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(
        VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
    if (this._onHashChange) {
      globalThis.removeEventListener("hashchange", this._onHashChange);
    }
    if (this.searchStickyObserver) {
      this.searchStickyObserver.disconnect();
      this.searchStickyObserver = null;
    }
  }

  onWindowScroll() {
    // A single scroll can pass several thresholds at once, so report every
    // threshold that hasn't been reported yet.
    while (
      this._nextScrollThreshold < SCROLL_TELEMETRY_THRESHOLDS.length &&
      global.scrollY > SCROLL_TELEMETRY_THRESHOLDS[this._nextScrollThreshold]
    ) {
      const threshold =
        SCROLL_TELEMETRY_THRESHOLDS[this._nextScrollThreshold++];
      this.props.dispatch(
        ac.OnlyToMain({ type: at.NEW_TAB_SCROLL, data: { threshold } })
      );
    }

    if (this.props.Prefs.values[PREF_NOVA_ENABLED]) {
      // Nova restores sticky search via IntersectionObserver
      // (attachSearchSentinel); the scroll-based fixed-search math below
      // is classic-only.
      return;
    }
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

    /* Bug 1917937: The logic presented below is fragile but accurate to the pixel. As new tab experiments with layouts, we have a tech debt of competing styles and classes the slightly modify where the search bar sits on the page. The larger solution for this is to replace everything with an intersection observer, but would require a larger refactor of this file. In the interim, we can programmatically calculate when to fire the fixed-scroll event and account for the moved elements so that topsites/etc stays in the same place. The CSS this references has been flagged to reference this logic so (hopefully) keep them in sync. */

    let SCROLL_THRESHOLD = 0; // When the fixed-scroll event fires
    let MAIN_OFFSET_PADDING = 0; // The padding to compensate for the moved elements

    const CSS_VAR_SPACE_XXLARGE = 32.04; // Custom Acorn themed variable (8 * 0.267rem);

    let layout = {
      outerWrapperPaddingTop: 32.04,
      searchWrapperPaddingTop: 16.02,
      searchWrapperPaddingBottom: CSS_VAR_SPACE_XXLARGE,
      searchWrapperFixedScrollPaddingTop: 24.03,
      searchWrapperFixedScrollPaddingBottom: 24.03,
      searchInnerWrapperMinHeight: 52,
      logoAndWordmarkWrapperHeight: 0,
      logoAndWordmarkWrapperMarginBottom: 0,
    };

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
  }

  closeCustomizationMenu() {
    if (this.props.App.customizeMenuVisible) {
      this.props.dispatch({ type: at.HIDE_PERSONALIZE });
      this.props.dispatch(ac.UserEvent({ event: "HIDE_PERSONALIZE" }));
    }
  }

  setPref(pref, value) {
    this.props.dispatch(ac.SetPref(pref, value));
  }

  applyBodyClasses() {
    const { body, documentElement } = this.props.document;

    if (documentElement) {
      documentElement.classList.toggle(
        "nova-tokens",
        !!this.props.Prefs.values[PREF_NOVA_ENABLED]
      );
    }

    if (!body) {
      return;
    }

    if (!body.classList.contains("activity-stream")) {
      body.classList.add("activity-stream");
    }
  }

  // The saved image the page is showing, used to read its attribution.
  appliedSavedWallpaper() {
    const { customWallpapers } = this.props.Wallpapers;
    const filename =
      this.props.Prefs.values["newtabWallpapers.customWallpaper.uuid"];
    if (!filename) {
      return null;
    }
    return customWallpapers?.find(wallpaper => wallpaper.filename === filename);
  }

  renderWallpaperAttribution() {
    const { wallpaperList } = this.props.Wallpapers;
    const activeWallpaper =
      this.props.Prefs.values[`newtabWallpapers.wallpaper`] ||
      this.props.Prefs.values[`newtabWallpapers.initialWallpaper`];
    const attribution =
      activeWallpaper === "custom"
        ? this.appliedSavedWallpaper()?.attribution
        : wallpaperList.find(wp => wp.title === activeWallpaper)?.attribution;
    // make sure a wallpaper is selected and that the attribution also exists
    if (!attribution) {
      return null;
    }

    const { name: authorDetails, webpage } = attribution;
    if (activeWallpaper && authorDetails?.url && webpage?.url) {
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
    // Bump every call so a newer selection supersedes an in-flight decode.
    const applyToken = this.nextWallpaperToken();
    const novaEnabled = prefs[PREF_NOVA_ENABLED];
    const wallpapersEnabled = prefs["newtabWallpapers.enabled"];
    const wallpapersUserEnabled = prefs["newtabWallpapers.user.enabled"];
    const isWallpaperVisible = novaEnabled
      ? wallpapersEnabled && wallpapersUserEnabled
      : wallpapersEnabled;
    const selectedWallpaper = isWallpaperVisible
      ? prefs["newtabWallpapers.wallpaper"] ||
        prefs["newtabWallpapers.initialWallpaper"]
      : null;
    const { wallpaperList, uploadedWallpaper: uploadedWallpaperUrl } =
      this.props.Wallpapers;
    const uploadedWallpaperTheme =
      prefs["newtabWallpapers.customWallpaper.theme"];
    // Uuse this.prefersDarkQuery since this.state.colorMode can be undefined when this is called
    const colorMode = this.prefersDarkQuery?.matches ? "dark" : "light";
    let url = "";
    let color = "transparent";
    let newTheme = colorMode;
    let backgroundPosition = "center";

    // if no selected wallpaper fallback to browser/theme styles
    if (!selectedWallpaper) {
      global.document?.body.style.removeProperty("--newtab-wallpaper");
      global.document?.body.style.removeProperty("--newtab-wallpaper-color");
      global.document?.body.style.removeProperty(
        "--newtab-wallpaper-backgroundPosition"
      );
      global.document?.documentElement.classList.remove(
        "lightWallpaper",
        "darkWallpaper"
      );
      return;
    }

    // Keep the current background until the custom wallpaper's URL hydrates.
    if (selectedWallpaper === "custom" && !uploadedWallpaperUrl) {
      return;
    }

    // uploaded wallpaper
    if (selectedWallpaper === "custom" && uploadedWallpaperUrl) {
      url = uploadedWallpaperUrl;
      color = "transparent";
      // Nobody picks a position. An upload is centered, and a saved Firefox
      // wallpaper keeps the crop it shipped with through this pref.
      backgroundPosition =
        prefs["newtabWallpapers.customWallpaper.position"] || "center";
      newTheme = uploadedWallpaperTheme || colorMode;
    } else if (wallpaperList) {
      const wallpaper = wallpaperList.find(
        wp => wp.title === selectedWallpaper
      );
      // solid color picker
      if (selectedWallpaper.includes("solid-color-picker")) {
        const regexRGB = /#([a-fA-F0-9]{6})/;
        const hex = selectedWallpaper.match(regexRGB)?.[0];
        url = "";
        color = hex;
        const rgbColors = this.getRGBColors(hex);
        newTheme = this.isWallpaperColorDark(rgbColors) ? "dark" : "light";
        // standard wallpaper & solid colors
      } else if (selectedWallpaper) {
        url = wallpaper?.wallpaperUrl || "";
        backgroundPosition = wallpaper?.background_position || "center";
        color = wallpaper?.solid_color || "transparent";
        newTheme = wallpaper?.theme || colorMode;
        // if a solid color, determine if dark or light
        if (wallpaper?.solid_color) {
          const rgbColors = this.getRGBColors(wallpaper.solid_color);
          const isColorDark = this.isWallpaperColorDark(rgbColors);
          newTheme = isColorDark ? "dark" : "light";
        }
      }
    }

    // Decode a replacement before swapping so the current wallpaper stays up
    // until it's ready; initial renders (nothing painted) apply synchronously.
    if (
      selectedWallpaper === "custom" &&
      url &&
      global.document?.body.style.getPropertyValue("--newtab-wallpaper") &&
      !(await this.decodeWallpaper(url, applyToken))
    ) {
      return;
    }

    global.document?.body.style.setProperty(
      "--newtab-wallpaper",
      `url(${url})`
    );
    global.document?.body.style.setProperty(
      "--newtab-wallpaper-backgroundPosition",
      backgroundPosition
    );
    global.document?.body.style.setProperty(
      "--newtab-wallpaper-color",
      color || "transparent"
    );

    global.document?.documentElement.classList.remove(
      "lightWallpaper",
      "darkWallpaper"
    );
    global.document?.documentElement.classList.add(
      newTheme === "dark" ? "darkWallpaper" : "lightWallpaper"
    );
  }

  // Monotonic token to detect if a newer updateWallpaper ran mid-decode.
  nextWallpaperToken() {
    this._wallpaperApplyToken = (this._wallpaperApplyToken ?? 0) + 1;
    return this._wallpaperApplyToken;
  }

  // Returns false if decode fails or a newer update supersedes this one, so the
  // current wallpaper stays painted.
  async decodeWallpaper(url, applyToken) {
    try {
      const image = new global.Image();
      image.src = url;
      await image.decode();
    } catch {
      return false;
    }
    return applyToken === this._wallpaperApplyToken;
  }

  toggleDownloadHighlight() {
    this.setState(prevState => {
      const override = !(
        prevState.showDownloadHighlightOverride ??
        shouldShowOMCHighlight(
          this.props.Messages,
          "DownloadMobilePromoHighlight"
        )
      );

      if (override) {
        // Emit an open event manually since OMC isn't handling it
        this.props.dispatch(
          ac.DiscoveryStreamUserEvent({
            event: "FEATURE_HIGHLIGHT_OPEN",
            source: "FEATURE_HIGHLIGHT",
            value: { feature: "FEATURE_DOWNLOAD_MOBILE_PROMO" },
          })
        );
      }

      return {
        showDownloadHighlightOverride: override,
      };
    });
  }

  handleDismissDownloadHighlight() {
    this.setState({ showDownloadHighlightOverride: false });
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

  toggleSubpanel(id) {
    this.setState(prevState => ({
      activeSubpanel: prevState.activeSubpanel === id ? null : id,
    }));
  }

  toggleSectionsMgmtPanel() {
    this.toggleSubpanel(CUSTOMIZE_SUBPANELS.SECTIONS);
  }

  toggleWidgetsManagementPanel() {
    this.toggleSubpanel(CUSTOMIZE_SUBPANELS.WIDGETS);
  }

  toggleThemesPanel() {
    this.toggleSubpanel(CUSTOMIZE_SUBPANELS.THEMES);
  }

  openWallpapersPanel(categoryId) {
    this.setState({
      activeSubpanel: CUSTOMIZE_SUBPANELS.WALLPAPERS,
      wallpapersPanelCategory: categoryId,
    });
  }

  // Keeps wallpapersPanelCategory so the heading and wallpaper list stay
  // populated while the subpanel slides out. The next open overwrites it.
  closeWallpapersPanel() {
    this.setState(prevState =>
      prevState.activeSubpanel === CUSTOMIZE_SUBPANELS.WALLPAPERS
        ? { activeSubpanel: null }
        : null
    );
  }

  closeSubpanels() {
    this.setState(CLOSED_SUBPANELS);
  }

  openWidgetsPanel() {
    this.openCustomizationMenu();
    this.setState({ activeSubpanel: CUSTOMIZE_SUBPANELS.WIDGETS });
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

    // @nova-cleanup(remove-conditional):
    const novaEnabled = prefs[PREF_NOVA_ENABLED];

    const activeWallpaper =
      prefs[`newtabWallpapers.wallpaper`] ||
      prefs[`newtabWallpapers.initialWallpaper`];
    const wallpapersEnabled = prefs["newtabWallpapers.enabled"];
    const wallpapersUserEnabled = prefs["newtabWallpapers.user.enabled"];
    // @nova-cleanup(remove-conditional): Remove conditional; replace with prefs["widgets.weather.enabled"]
    const weatherEnabled = novaEnabled
      ? prefs["widgets.weather.enabled"]
      : prefs.showWeather;
    const { showTopicSelection } = DiscoveryStream;
    const mayShowTopicSelection =
      showTopicSelection && prefs["discoverystream.topicSelection.enabled"];

    const isDiscoveryStream =
      props.DiscoveryStream.config && props.DiscoveryStream.config.enabled;
    let filteredSections = props.Sections.filter(
      section => section.id !== "topstories"
    );

    const topSitesEnabled = prefs["feeds.topsites"];
    const pocketEnabled =
      (prefs["feeds.section.topstories"] ||
        isSpaceOverridden(SPACE_IDS.STORIES, prefs)) &&
      prefs["feeds.system.topstories"];
    // @nova-cleanup(remove): pre-Nova; `filteredSections` is the legacy
    // Sections redux slice that no longer drives Nova layout. Nova uses
    // `noContentSectionsEnabled` (declared in the Nova branch below).
    const noSectionsEnabled =
      !topSitesEnabled &&
      !pocketEnabled &&
      filteredSections.filter(section => section.enabled).length === 0;
    const enabledSections = {
      topSitesEnabled,
      // So the toggle does not read off while the Stories space is showing.
      pocketEnabled:
        prefs["feeds.section.topstories"] ||
        isSpaceOverridden(SPACE_IDS.STORIES, prefs),
      showInferredPersonalizationEnabled:
        prefs[PREF_INFERRED_PERSONALIZATION_USER],
      topSitesRowsCount: prefs.topSitesRows,
      webNotificationsEnabled: prefs.showWebNotifications,
      weatherEnabled: novaEnabled
        ? prefs["widgets.weather.enabled"]
        : prefs.showWeather,
    };

    const pocketRegion = prefs["feeds.system.topstories"];
    const mayHaveInferredPersonalization =
      prefs[PREF_INFERRED_PERSONALIZATION_SYSTEM];
    const mayHaveWeather = isWeatherAvailable(prefs);
    const mayHaveWebNotifications =
      prefs["system.showWebNotifications"] ||
      prefs.trainhopConfig?.webNotifications?.enabled;
    const supportUrl = prefs["support.url"];

    // Widget toggle visibility is resolved by the shared registry helpers, which
    // are additive across the system pref, the legacy widgetsConfig variable,
    // trainhopConfig.widgets (addable), and trainhopConfig.widgetsSettings.
    // @nova-cleanup(remove-conditional): Delete widgetVisibleById, the eight
    // mayHave*Widget constants below and the now-unused isWidgetToggleVisible
    // import once the classic widget toggle block in ContentSection.jsx is
    // deleted. Keep mayHaveWidgets: the Nova widgets section and the standalone
    // Weather row both read it.
    const widgetVisibleById = id =>
      isWidgetToggleVisible(
        WIDGET_REGISTRY.find(w => w.id === id),
        prefs
      );
    const mayHaveWidgets = isWidgetsContainerVisible(prefs);
    const mayHaveListsWidget = widgetVisibleById("lists");
    const mayHaveTimerWidget = widgetVisibleById("focusTimer");
    const mayHaveClocksWidget = widgetVisibleById("clocks");
    const mayHavePrivacyWidget = widgetVisibleById("privacy");
    const mayHaveCrosswordWidget = widgetVisibleById("crossword");
    const mayHaveStocksWidget = widgetVisibleById("stocks");
    const mayHavePictureOfTheDayWidget = widgetVisibleById("pictureOfTheDay");
    const mayHaveRecentSearchesWidget = widgetVisibleById("recentSearches");

    // These prefs set the initial values on the Customize panel toggle switches
    // @nova-cleanup(remove-conditional): Delete every *Enabled member of
    // enabledWidgets once the classic widget toggle block in ContentSection.jsx
    // is deleted; keep widgetsMaximized and widgetsMayBeMaximized, which the
    // widget size telemetry in ContentSection.jsx reads.
    const enabledWidgets = {
      listsEnabled: prefs["widgets.lists.enabled"],
      timerEnabled: prefs["widgets.focusTimer.enabled"],
      clocksEnabled: prefs["widgets.clocks.enabled"],
      weatherEnabled: novaEnabled
        ? prefs["widgets.weather.enabled"]
        : prefs.showWeather,
      privacyEnabled: prefs["widgets.privacy.enabled"],
      crosswordEnabled: prefs["widgets.crossword.enabled"],
      stocksEnabled: prefs["widgets.stocks.enabled"],
      pictureOfTheDayEnabled: prefs["widgets.pictureOfTheDay.enabled"],
      recentSearchesEnabled: prefs["widgets.recentSearches.enabled"],
      widgetsMaximized: prefs["widgets.maximized"],
      widgetsMayBeMaximized: prefs["widgets.system.maximized"],
    };

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
    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    const sectionsCustomizeMenuPanelEnabled =
      prefs["discoverystream.sections.customizeMenuPanel.enabled"];
    const sectionsPersonalizationEnabled =
      prefs["discoverystream.sections.personalization.enabled"];

    // Logic to show follow/block topic mgmt panel in Customize panel
    const mayHavePersonalizedTopicSections =
      sectionsPersonalizationEnabled &&
      sectionsEnabled &&
      sectionsCustomizeMenuPanelEnabled &&
      DiscoveryStream.feeds.loaded;

    const featureClassName = [
      // Nova helper class to target pre-Nova CSS styles
      "classic-enabled",
      mobileDownloadPromoEnabled &&
        mobileDownloadPromoVariantABorC &&
        "has-mobile-download-promo", // Mobile download promo modal is enabled/visible
      weatherEnabled && mayHaveWeather && "has-weather", // Weather widget is enabled/visible
      prefs.showSearch ? "has-search" : "no-search",
      // layoutsVariantAEnabled ? "layout-variant-a" : "", // Layout experiment variant A
      // layoutsVariantBEnabled ? "layout-variant-b" : "", // Layout experiment variant B
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
      topSitesEnabled && !pocketEnabled && !prefs.showSearch && "only-topsites",
      noSectionsEnabled && "no-sections",
      prefs["logowordmark.alwaysVisible"] && "visible-logo",
    ]
      .filter(v => v)
      .join(" ");

    // If state.showDownloadHighlightOverride has value, let it override the logic
    // Otherwise, defer to OMC message display logic
    const shouldShowDownloadHighlight =
      this.state.showDownloadHighlightOverride ??
      shouldShowOMCHighlight(
        this.props.Messages,
        "DownloadMobilePromoHighlight"
      );

    const multistageMessageFeed = shouldShowOMCHighlight(
      this.props.Messages,
      "ASRouterMultistageMessage"
    ) ? (
      <ErrorBoundary>
        <MessageWrapper dispatch={this.props.dispatch}>
          <ExternalComponentWrapper
            type="ASROUTER_MULTISTAGE_MESSAGE"
            messageData={this.props.Messages.messageData}
            className="asrouter-multistage-message-wrapper"
          />
        </MessageWrapper>
      </ErrorBoundary>
    ) : null;

    const baseContextValue = { openWidgetsPanel: this.openWidgetsPanel };

    // The experiment can turn the Widgets space on for a profile that had
    // widgets off, and both the layout and the customize menu toggle have to
    // agree with what is on the page.
    const widgetsEnabled =
      prefs["widgets.enabled"] || isSpaceOverridden(SPACE_IDS.WIDGETS, prefs);

    // @nova-cleanup(remove-conditional): Remove this conditional and
    // always render the Nova layout below. The classic render() return
    // and all its supporting variables (featureClassName, outerClassName,
    //  mobileDownloadPromo*, etc.) will become dead code and should
    // be deleted — expect lint errors for unused vars.
    if (novaEnabled) {
      // Logo placement: when there's no Pocket feed and no content-area
      // widget, the Logo renders centered in .content; otherwise it
      // anchors the inline-start sidebar. If the page has nothing on it
      // (no content sections, no search, no widgets), the Logo is
      // suppressed entirely via `isPageEmpty`.
      const hasAnyEnabledWidget = WIDGET_REGISTRY.some(w =>
        isWidgetEnabled(w, prefs, widgetsEnabled)
      );
      const hasContentWidgets = hasContentAreaWidgets(prefs, widgetsEnabled);
      const highlightsEnabled = prefs["feeds.section.highlights"];
      const noContentSectionsEnabled =
        !topSitesEnabled && !pocketEnabled && !highlightsEnabled;
      const isPageEmpty =
        noContentSectionsEnabled && !prefs.showSearch && !hasAnyEnabledWidget;
      const hasManyTopSitesRows = topSitesEnabled && prefs.topSitesRows > 2;
      // Recent activity is then alone in the band, and the logo leaves the sidebar.
      const noFeedOrContentWidgets = !pocketEnabled && !hasContentWidgets;
      const spacesActive = isSpacesActive(prefs, props.DiscoveryStream);
      // Gated here rather than in CSS, so the stylesheet never has to infer
      // whether widgets or stories exist. The lead class alone means the
      // experiment is assigned, which is enough to frame a lone section; the
      // two-column layout additionally needs both sections.
      const bandClassName = [
        "content-full-width",
        ...sideBySideBandClasses(prefs),
        isSideBySideActive(prefs) && "side-by-side-active",
        // Unlike side-by-side, an assigned-but-inactive spaces variant renders
        // the ordinary band, so there is no Spaces container for these classes
        // to describe.
        // The feed state too, or the thematic variant can class the band for
        // spaces while DiscoveryStreamBase, which drops a space with no
        // sections, has fallen back to the flat layout.
        ...(spacesActive ? spacesBandClasses(prefs) : []),
        noFeedOrContentWidgets && "highlights-only",
      ]
        .filter(Boolean)
        .join(" ");
      // Variant B of the search bar carries its own row above the input, which
      // a centered logo would sit under.
      const searchHasOwnRow =
        prefs.showSearch &&
        "variant-b" in
          (this.props.ExternalComponents.components.find(
            c => c.type === "SEARCH"
          )?.attributes ?? {});
      const hasMultistageMessage = shouldShowOMCHighlight(
        this.props.Messages,
        "ASRouterMultistageMessage"
      );
      const contentShouldBeLowered =
        noFeedOrContentWidgets && !hasManyTopSitesRows && !hasMultistageMessage;
      const logoShouldBeCentered = contentShouldBeLowered && !searchHasOwnRow;
      const searchShouldBeLowered = contentShouldBeLowered && searchHasOwnRow;
      // The 5-column story grid is driven by the layout data alone: the content
      // band only widens when every section has a columnCount: 5 entry. Sections
      // share one subgrid track count, so a layout set where only some sections
      // define 5 columns has to stay at 4 — widening it would leave the others
      // with no tile for the active breakpoint, and nothing to render.
      const sectionsWithLayouts = Object.values(
        props.DiscoveryStream.feeds?.data ?? {}
      ).find(feed => feed?.data?.sections?.length)?.data?.sections;
      const hasFiveColumnLayout =
        !!sectionsWithLayouts?.length &&
        sectionsWithLayouts.every(section =>
          section.layout?.responsiveLayouts?.some(
            layout => layout.columnCount === 5
          )
        );
      // Rendered as a direct child of .container unless the logo is centered,
      // so position: sticky is bounded by .container (which spans the whole
      // page) rather than .content (which now ends above the content band).
      // With a centered logo the search has to stay under it inside .content.
      const searchArea = prefs.showSearch && (
        <>
          <div
            ref={this.attachSearchSentinel}
            className="sticky-search-sentinel"
            aria-hidden="true"
          />
          <ErrorBoundary>
            <Search showLogo={false} {...props.Search} />
          </ErrorBoundary>
        </>
      );
      // Slotted into DiscoveryStreamBase between the widgets and the feed,
      // which is the only place that boundary exists now that both live in the
      // .content-full-width band.
      const aboveContentFeedMessage = shouldShowASRouterNewTabMessage(
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
      );
      // Widgets (injected via DiscoveryStreamBase) + content feed. Always render
      // in the .content-full-width band below the grid.
      const contentFeed = isDiscoveryStream && (
        <ErrorBoundary className="borderless-error">
          <DiscoveryStreamBase
            locale={props.App.locale}
            spocsLoading={this.isSpocsOnDemandExpired}
            aboveContentFeed={aboveContentFeedMessage}
          />
        </ErrorBoundary>
      );

      return (
        <BaseContext.Provider value={baseContextValue}>
          <div
            className={`nova-outer-wrapper${this.state.fixedSearch ? " stuck-search" : ""}${searchHasOwnRow ? " search-has-own-row" : ""}`}
          >
            <div
              className={`container nova-enabled${logoShouldBeCentered ? " logo-in-content" : ""}${searchHasOwnRow ? " search-has-own-row" : ""}${searchShouldBeLowered ? " search-lowered" : ""}${hasFiveColumnLayout ? " sections-5-col" : ""}`}
            >
              <aside className="sidebar-inline-start">
                {!prefs.hideLogo && !logoShouldBeCentered && !isPageEmpty && (
                  <ErrorBoundary>
                    <Logo />
                  </ErrorBoundary>
                )}
                {/* Future: Page Nav  */}
              </aside>
              {/* Bug 2021460 - Placed before <main> in DOM order so small widgets
            are tab-focused before the main content feed. */}
              <aside className="sidebar-inline-end">
                {novaEnabled && (
                  <ErrorBoundary>
                    <WidgetsSidebar dispatch={props.dispatch} />
                  </ErrorBoundary>
                )}
              </aside>

              {/* Search */}
              {!logoShouldBeCentered && searchArea}

              {/* display: contents, so .content and .content-full-width stay
              direct grid items of .container while both sit inside the main
              landmark. See _Grid.scss. */}
              <main className="content-main">
                <div className="content">
                  {!prefs.hideLogo && logoShouldBeCentered && !isPageEmpty && (
                    <ErrorBoundary>
                      <Logo />
                    </ErrorBoundary>
                  )}

                  {logoShouldBeCentered && searchArea}

                  {/* ASRouterNewTabMessage (ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_TOPSITES) */}
                  {shouldShowASRouterNewTabMessage(
                    this.props.Messages,
                    "ASRouterNewTabMessage",
                    ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_TOPSITES
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

                  {/* ActivationWindowMessage */}
                  {shouldShowOMCHighlight(
                    this.props.Messages,
                    "ActivationWindowMessage"
                  ) && (
                    <ErrorBoundary>
                      <MessageWrapper dispatch={this.props.dispatch}>
                        <ActivationWindowMessage
                          dispatch={this.props.dispatch}
                          messageData={this.props.Messages.messageData}
                        />
                      </MessageWrapper>
                    </ErrorBoundary>
                  )}

                  {/* TODO: Break out Topsites, Widgets from DiscoveryStreamBase */}
                  {/* Shortcuts / Topsites */}
                  {topSitesEnabled && (
                    <ErrorBoundary>
                      <TopSites />
                    </ErrorBoundary>
                  )}

                  {/* ASRouterNewTabMessage (ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_WIDGETS) */}
                  {shouldShowASRouterNewTabMessage(
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

                  {/* ASRouterNewTabMessage (ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_CONTENT_FEED)
                renders inside the band, see aboveContentFeedMessage above. */}

                  {!pocketEnabled && multistageMessageFeed}
                </div>
                {/* Widgets + content feed, in a band spanning all three columns
              on the row below the grid. See _Grid.scss. */}
                {contentFeed && (
                  <div className={bandClassName}>{contentFeed}</div>
                )}
                {/* Nova only shows the wallpaper when both prefs are on, unlike
              classic; see updateWallpaper. */}
                {wallpapersEnabled &&
                  wallpapersUserEnabled &&
                  this.renderWallpaperAttribution()}
              </main>
            </div>
            {/* The floating arrows panel runs to the bottom of the page, past
            the band. See _Spaces.scss. */}
            {spacesActive && isSpacesArrowsAssigned(prefs) && (
              <div className="spaces-panel-backdrop" />
            )}
            <ConfirmDialog />
            <menu className="personalizeButtonWrapper nova-enabled">
              <CustomizeMenu
                onClose={this.closeCustomizationMenu}
                onOpen={this.openCustomizationMenu}
                openPreferences={this.openPreferences}
                setPref={this.setPref}
                enabledSections={enabledSections}
                enabledWidgets={enabledWidgets}
                wallpapersEnabled={wallpapersEnabled}
                wallpapersUserEnabled={wallpapersUserEnabled}
                activeWallpaper={activeWallpaper}
                pocketRegion={pocketRegion}
                mayHaveTopicSections={mayHavePersonalizedTopicSections}
                mayHaveInferredPersonalization={mayHaveInferredPersonalization}
                mayHaveWeather={mayHaveWeather}
                mayHaveWebNotifications={mayHaveWebNotifications}
                mayHaveWidgets={mayHaveWidgets}
                // @nova-cleanup(remove-conditional): Delete the eight
                // mayHave*Widget props from this render site once the classic
                // widget toggle block in ContentSection.jsx is deleted. Keep
                // enabledWidgets: its widgetsMaximized and widgetsMayBeMaximized
                // members are read by the widget size telemetry in
                // ContentSection.jsx.
                mayHaveTimerWidget={mayHaveTimerWidget}
                mayHaveListsWidget={mayHaveListsWidget}
                mayHaveClocksWidget={mayHaveClocksWidget}
                mayHavePrivacyWidget={mayHavePrivacyWidget}
                mayHaveCrosswordWidget={mayHaveCrosswordWidget}
                mayHaveStocksWidget={mayHaveStocksWidget}
                mayHavePictureOfTheDayWidget={mayHavePictureOfTheDayWidget}
                mayHaveRecentSearchesWidget={mayHaveRecentSearchesWidget}
                mayHaveWeatherForecast={
                  prefs["widgets.system.weatherForecast.enabled"]
                }
                weatherDisplay={prefs["weather.display"]}
                showing={customizeMenuVisible}
                toggleSectionsMgmtPanel={this.toggleSectionsMgmtPanel}
                activeSubpanel={this.state.activeSubpanel}
                toggleWidgetsManagementPanel={this.toggleWidgetsManagementPanel}
                toggleThemesPanel={this.toggleThemesPanel}
                wallpapersPanelCategory={this.state.wallpapersPanelCategory}
                openWallpapersPanel={this.openWallpapersPanel}
                closeWallpapersPanel={this.closeWallpapersPanel}
                closeSubpanels={this.closeSubpanels}
                widgetsEnabled={widgetsEnabled}
                dispatch={this.props.dispatch}
              />
              {shouldShowOMCHighlight(
                this.props.Messages,
                "CustomWallpaperHighlight"
              ) && (
                <MessageWrapper dispatch={this.props.dispatch}>
                  <WallpaperFeatureHighlight
                    position="inset-block-start inset-inline-start"
                    dispatch={this.props.dispatch}
                  />
                </MessageWrapper>
              )}
            </menu>
            {this.props.Notifications?.showNotifications && (
              <ErrorBoundary>
                <Notifications dispatch={this.props.dispatch} />
              </ErrorBoundary>
            )}
          </div>
        </BaseContext.Provider>
      );
    }

    // @nova-cleanup(remove-conditional): Delete this entire classic return block along with all variables only used here
    return (
      <BaseContext.Provider value={baseContextValue}>
        <div className={featureClassName}>
          <div className="weatherWrapper">
            {!novaEnabled && weatherEnabled && (
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
                  isActive={shouldShowDownloadHighlight}
                  onClick={this.toggleDownloadHighlight}
                />
                {shouldShowDownloadHighlight && (
                  <MessageWrapper
                    hiddenOverride={shouldShowDownloadHighlight}
                    onDismiss={this.handleDismissDownloadHighlight}
                    dispatch={this.props.dispatch}
                  >
                    <DownloadMobilePromoHighlight
                      position={`inset-inline-start inset-block-end`}
                      dispatch={this.props.dispatch}
                    />
                  </MessageWrapper>
                )}
              </ErrorBoundary>
            )}
          </div>

          <div className={outerClassName}>
            <main className="newtab-main" style={this.state.fixedNavStyle}>
              {prefs.showSearch && (
                <div className="non-collapsible-section">
                  <ErrorBoundary>
                    <Search
                      showLogo={
                        noSectionsEnabled || prefs["logowordmark.alwaysVisible"]
                      }
                      {...props.Search}
                    />
                  </ErrorBoundary>
                </div>
              )}
              {/* Bug 1914055: Show logo regardless if search is enabled */}
              {!prefs.showSearch && !noSectionsEnabled && <Logo />}
              <div className={`body-wrapper${initialized ? " on" : ""}`}>
                {shouldShowOMCHighlight(
                  this.props.Messages,
                  "ActivationWindowMessage"
                ) && (
                  <MessageWrapper dispatch={this.props.dispatch}>
                    <ActivationWindowMessage
                      dispatch={this.props.dispatch}
                      messageData={this.props.Messages.messageData}
                    />
                  </MessageWrapper>
                )}

                {shouldShowASRouterNewTabMessage(
                  this.props.Messages,
                  "ASRouterNewTabMessage",
                  ASROUTER_NEWTAB_MESSAGE_POSITIONS.ABOVE_TOPSITES
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

                {isDiscoveryStream ? (
                  <ErrorBoundary className="borderless-error">
                    <DiscoveryStreamBase
                      locale={props.App.locale}
                      spocsLoading={this.isSpocsOnDemandExpired}
                    />
                  </ErrorBoundary>
                ) : (
                  <Sections />
                )}
              </div>
              <ConfirmDialog />
              {wallpapersEnabled && this.renderWallpaperAttribution()}
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
          {/* Floating menu for customize menu toggle */}
          <menu className="personalizeButtonWrapper">
            <CustomizeMenu
              onClose={this.closeCustomizationMenu}
              onOpen={this.openCustomizationMenu}
              openPreferences={this.openPreferences}
              setPref={this.setPref}
              enabledSections={enabledSections}
              enabledWidgets={enabledWidgets}
              wallpapersEnabled={wallpapersEnabled}
              wallpapersUserEnabled={wallpapersUserEnabled}
              activeWallpaper={activeWallpaper}
              pocketRegion={pocketRegion}
              mayHaveTopicSections={mayHavePersonalizedTopicSections}
              mayHaveInferredPersonalization={mayHaveInferredPersonalization}
              mayHaveWeather={mayHaveWeather}
              mayHaveWebNotifications={mayHaveWebNotifications}
              mayHaveWidgets={mayHaveWidgets}
              mayHaveTimerWidget={mayHaveTimerWidget}
              mayHaveListsWidget={mayHaveListsWidget}
              mayHaveClocksWidget={mayHaveClocksWidget}
              mayHavePrivacyWidget={mayHavePrivacyWidget}
              mayHaveCrosswordWidget={mayHaveCrosswordWidget}
              mayHaveStocksWidget={mayHaveStocksWidget}
              mayHavePictureOfTheDayWidget={mayHavePictureOfTheDayWidget}
              mayHaveRecentSearchesWidget={mayHaveRecentSearchesWidget}
              mayHaveWeatherForecast={
                prefs["widgets.system.weatherForecast.enabled"]
              }
              weatherDisplay={prefs["weather.display"]}
              showing={customizeMenuVisible}
              toggleSectionsMgmtPanel={this.toggleSectionsMgmtPanel}
              activeSubpanel={this.state.activeSubpanel}
              toggleThemesPanel={this.toggleThemesPanel}
              wallpapersPanelCategory={this.state.wallpapersPanelCategory}
              openWallpapersPanel={this.openWallpapersPanel}
              closeWallpapersPanel={this.closeWallpapersPanel}
              closeSubpanels={this.closeSubpanels}
            />
            {shouldShowOMCHighlight(
              this.props.Messages,
              "CustomWallpaperHighlight"
            ) && (
              <MessageWrapper dispatch={this.props.dispatch}>
                <WallpaperFeatureHighlight
                  position="inset-block-start inset-inline-start"
                  dispatch={this.props.dispatch}
                />
              </MessageWrapper>
            )}
          </menu>
        </div>
      </BaseContext.Provider>
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
  ExternalComponents: state.ExternalComponents,
  Messages: state.Messages,
  Notifications: state.Notifications,
  Search: state.Search,
  Wallpapers: state.Wallpapers,
  Weather: state.Weather,
}))(_Base);
