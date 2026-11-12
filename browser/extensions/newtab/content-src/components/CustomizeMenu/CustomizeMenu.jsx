/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ContentSection } from "content-src/components/CustomizeMenu/ContentSection/ContentSection";
import { CUSTOMIZE_SUBPANELS } from "content-src/lib/constants";
import {
  PANEL_HIDDEN,
  notifyThemePickersOnTransition,
} from "content-src/lib/theme-picker-shown";
import { recordCustomizePanelTransitions } from "content-src/lib/customize-panel-telemetry";
import { connect } from "react-redux";
import React from "react";

const PREF_NOVA_ENABLED = "nova.enabled";
// eslint-disable-next-line no-shadow
import { CSSTransition } from "react-transition-group";

const THEME_PICKER_ELEMENTS = [
  "chrome://global/content/elements/moz-visual-picker.mjs",
  "chrome://global/content/elements/moz-segmented-control.mjs",
  "chrome://global/content/elements/theme-picker.mjs",
];

const THEME_PICKER_FTL = "toolkit/global/theme-picker.ftl";
let themePickerElementsLoaded = false;

/**
 * Pulls in the THEME_PICKER_ELEMENTS dependencies the first time the customize
 * panel is opened with the theme picker enabled.
 *
 * The element's own `insertFTLIfNeeded` does not run in the newtab content
 * context (no `MozXULElement`), so its ftl is registered here. Rejections are
 * swallowed so a load failure leaves the panel without a theme picker rather
 * than raising an unhandled rejection.
 */
function loadThemePickerElements() {
  if (themePickerElementsLoaded) {
    return;
  }
  themePickerElementsLoaded = true;
  document.l10n?.addResourceIds([THEME_PICKER_FTL]);
  for (const url of THEME_PICKER_ELEMENTS) {
    // eslint-disable-next-line no-unsanitized/method
    import(/* webpackIgnore: true */ url).catch(() => {});
  }
}

export class _CustomizeMenu extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onEntered = this.onEntered.bind(this);
    this.onExited = this.onExited.bind(this);
    this.onCancel = this.onCancel.bind(this);
    this.onDialogClick = this.onDialogClick.bind(this);
    this.personalizeButtonRef = React.createRef();
    this.dialogRef = React.createRef();
    this.closeButtonRef = React.createRef();
    this._hadLockedPrefs = false;
  }

  componentDidMount() {
    if (this.props.showing && this.props.Prefs.values.browserNovaEnabled) {
      loadThemePickerElements();
    }
    this.disableLockedControls();
    // A panel that is already showing when it mounts never sees an update for
    // that, so start from hidden here to notify its picker too.
    notifyThemePickersOnTransition(
      this.dialogRef.current,
      PANEL_HIDDEN,
      () => this.props
    );
  }

  componentDidUpdate(prevProps) {
    if (this.props.showing && !prevProps.showing) {
      if (this.props.Prefs.values.browserNovaEnabled) {
        loadThemePickerElements();
      }
      if (!this.dialogRef.current?.open) {
        this.dialogRef.current?.showModal();
      }
    }
    this.disableLockedControls();
    notifyThemePickersOnTransition(
      this.dialogRef.current,
      prevProps,
      () => this.props
    );
    recordCustomizePanelTransitions(this.props.dispatch, prevProps, this.props);
  }

  /**
   * Disables the controls whose pref an administrator has locked. Controls with
   * a `disabled` condition of their own set `data-lock-managed` and apply the
   * lock in their own render, so this stays the only writer of `disabled` for
   * everything it touches.
   */
  disableLockedControls() {
    const lockedPrefs = this.props.Prefs.values.lockedPrefs ?? [];
    if (!lockedPrefs.length && !this._hadLockedPrefs) {
      return;
    }
    this._hadLockedPrefs = !!lockedPrefs.length;

    const controls =
      this.dialogRef.current?.querySelectorAll(
        "[data-preference]:not([data-lock-managed])"
      ) ?? [];
    for (const control of controls) {
      control.disabled = lockedPrefs.includes(control.dataset.preference);
    }
  }

  onCancel(e) {
    e.preventDefault();
    this.props.onClose();
  }

  onDialogClick(e) {
    if (e.target === this.dialogRef.current) {
      this.props.onClose();
    }
  }

  onEntered() {
    if (this.closeButtonRef.current) {
      this.closeButtonRef.current.focus();
    }
  }

  onExited() {
    if (this.dialogRef.current?.open) {
      this.dialogRef.current.close();
    }
    this.props.closeSubpanels();
    if (this.personalizeButtonRef.current) {
      this.personalizeButtonRef.current.focus();
    }
  }

  render() {
    const activationWindowVariant =
      this.props.Prefs.values["activationWindow.variant"];

    const activationWindowClass = activationWindowVariant
      ? `activation-window-variant-${activationWindowVariant}`
      : "";
    // @nova-cleanup(remove-pref): remove nova pref
    const novaEnabled = this.props.Prefs.values[PREF_NOVA_ENABLED];
    // Browser-wide Nova gate for the theme picker (distinct from novaEnabled).
    const { browserNovaEnabled, lockedPrefs } = this.props.Prefs.values;
    const { activeSubpanel } = this.props;

    return (
      <span>
        <CSSTransition
          nodeRef={this.personalizeButtonRef}
          timeout={300}
          classNames="personalize-animate"
          in={!this.props.showing}
          appear={true}
        >
          {
            // @nova-cleanup(remove-conditional): replace with moz-button only
            novaEnabled ? (
              <moz-button
                ref={this.personalizeButtonRef}
                className={`open-customization-button${activationWindowClass ? ` ${activationWindowClass}` : ""}`}
                data-l10n-id="newtab-customize-panel-label"
                aria-haspopup="dialog"
                aria-expanded={this.props.showing ? "true" : "false"}
                onClick={() => this.props.onOpen()}
                iconsrc="chrome://global/skin/icons/edit-outline.svg"
                iconposition="end"
                type="primary"
              ></moz-button>
            ) : (
              <button
                ref={this.personalizeButtonRef}
                className={`${activationWindowClass} personalize-button`}
                data-l10n-id="newtab-customize-panel-icon-button"
                aria-haspopup="dialog"
                aria-expanded={this.props.showing}
                onClick={() => this.props.onOpen()}
              >
                <label data-l10n-id="newtab-customize-panel-icon-button-label" />
                <div>
                  <img
                    alt=""
                    src="chrome://global/skin/icons/edit-outline.svg"
                  />
                </div>
              </button>
            )
          }
        </CSSTransition>
        <CSSTransition
          nodeRef={this.dialogRef}
          timeout={250}
          classNames="customize-animate"
          in={this.props.showing}
          onEntered={this.onEntered}
          onExited={this.onExited}
          appear={true}
        >
          <dialog
            ref={this.dialogRef}
            // @nova-cleanup(remove-conditional): Remove nova-enabled class
            className={`customize-menu ${novaEnabled ? "nova-enabled" : ""}`}
            data-l10n-id="newtab-settings-dialog-label"
            onCancel={this.onCancel}
            onClick={this.onDialogClick}
          >
            <div
              className={`customize-menu-content${activeSubpanel ? " subpanel-open" : ""}`}
            >
              <div className="close-button-wrapper">
                <moz-button
                  onClick={() => this.props.onClose()}
                  id="close-button"
                  type="icon ghost"
                  data-l10n-id="newtab-custom-close-menu-button"
                  iconsrc="chrome://global/skin/icons/close.svg"
                  ref={this.closeButtonRef}
                ></moz-button>
              </div>
              <ContentSection
                panelShowing={this.props.showing}
                openPreferences={this.props.openPreferences}
                setPref={this.props.setPref}
                enabledSections={this.props.enabledSections}
                enabledWidgets={this.props.enabledWidgets}
                wallpapersEnabled={this.props.wallpapersEnabled}
                wallpapersUserEnabled={this.props.wallpapersUserEnabled}
                activeWallpaper={this.props.activeWallpaper}
                pocketRegion={this.props.pocketRegion}
                mayHaveTopicSections={this.props.mayHaveTopicSections}
                mayHaveInferredPersonalization={
                  this.props.mayHaveInferredPersonalization
                }
                mayHaveWeather={this.props.mayHaveWeather}
                mayHaveWebNotifications={this.props.mayHaveWebNotifications}
                mayHaveWidgets={this.props.mayHaveWidgets}
                mayHaveWeatherForecast={this.props.mayHaveWeatherForecast}
                weatherDisplay={this.props.weatherDisplay}
                // @nova-cleanup(remove-conditional): Delete the eight
                // mayHave*Widget props forwarded below once the classic widget
                // toggle block in ContentSection.jsx is deleted. Keep
                // enabledWidgets: its widgetsMaximized and widgetsMayBeMaximized
                // members are read by the widget size telemetry in
                // ContentSection.jsx.
                mayHaveTimerWidget={this.props.mayHaveTimerWidget}
                mayHaveListsWidget={this.props.mayHaveListsWidget}
                mayHaveClocksWidget={this.props.mayHaveClocksWidget}
                mayHavePrivacyWidget={this.props.mayHavePrivacyWidget}
                mayHaveCrosswordWidget={this.props.mayHaveCrosswordWidget}
                mayHaveStocksWidget={this.props.mayHaveStocksWidget}
                mayHavePictureOfTheDayWidget={
                  this.props.mayHavePictureOfTheDayWidget
                }
                mayHaveRecentSearchesWidget={
                  this.props.mayHaveRecentSearchesWidget
                }
                dispatch={this.props.dispatch}
                toggleSectionsMgmtPanel={this.props.toggleSectionsMgmtPanel}
                showSectionsMgmtPanel={
                  activeSubpanel === CUSTOMIZE_SUBPANELS.SECTIONS
                }
                novaEnabled={novaEnabled}
                browserNovaEnabled={browserNovaEnabled}
                toggleThemesPanel={this.props.toggleThemesPanel}
                showThemesPanel={activeSubpanel === CUSTOMIZE_SUBPANELS.THEMES}
                showWallpapersPanel={
                  activeSubpanel === CUSTOMIZE_SUBPANELS.WALLPAPERS
                }
                wallpapersPanelCategory={this.props.wallpapersPanelCategory}
                openWallpapersPanel={this.props.openWallpapersPanel}
                closeWallpapersPanel={this.props.closeWallpapersPanel}
                toggleWidgetsManagementPanel={
                  this.props.toggleWidgetsManagementPanel
                }
                showWidgetsManagementPanel={
                  activeSubpanel === CUSTOMIZE_SUBPANELS.WIDGETS
                }
                widgetsEnabled={this.props.widgetsEnabled}
                lockedPrefs={lockedPrefs}
              />
            </div>
          </dialog>
        </CSSTransition>
      </span>
    );
  }
}

export const CustomizeMenu = connect(state => ({
  DiscoveryStream: state.DiscoveryStream,
  Prefs: state.Prefs,
  customizePanelSource: state.App.customizePanelSource,
  customizePanelWallpaperCategory: state.App.customizePanelWallpaperCategory,
}))(_CustomizeMenu);
