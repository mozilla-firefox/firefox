/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { SectionsMgmtPanel } from "../SectionsMgmtPanel/SectionsMgmtPanel";
import { ThemesManagementPanel } from "../ThemesManagementPanel/ThemesManagementPanel";
import { WallpaperCategories } from "../../WallpaperCategories/WallpaperCategories";
// @nova-cleanup(move-directory): Update import path after WidgetsManagementPanel moves to components/CustomizeMenu/
import { WidgetsManagementPanel } from "content-src/components/Nova/CustomizeMenu/WidgetsManagementPanel/WidgetsManagementPanel";

const PREF_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.user.enabled";

// `theme-picker` is imported lazily, so it may still be an undefined custom element
// when React renders it. In that state React sets props as attributes, and the lit
// `showLabels` boolean (default true) can't be turned off via an attribute — so set the
// property directly via a ref; lit preserves it across element upgrade.
function hideThemePickerLabels(el) {
  if (el) {
    el.showLabels = false;
  }
}

export class ContentSection extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onPreferenceSelect = this.onPreferenceSelect.bind(this);

    // Refs are necessary for dynamically measuring drawer heights for slide animations
    this.topSitesDrawerRef = React.createRef();
    this.pocketDrawerRef = React.createRef();
    this.widgetsMgmtDrawerRef = React.createRef();
  }

  inputUserEvent(eventSource, eventValue) {
    this.props.dispatch(
      ac.UserEvent({
        event: "PREF_CHANGED",
        source: eventSource,
        value: { status: eventValue, menu_source: "CUSTOMIZE_MENU" },
      })
    );

    // Dispatch unified widget telemetry for widget toggles.
    // Map the event source from the customize panel to the widget name
    // for the unified telemetry event.
    let widgetName;
    switch (eventSource) {
      case "WEATHER":
        widgetName = "weather";
        break;
      case "WIDGET_LISTS":
        widgetName = "lists";
        break;
      case "WIDGET_TIMER":
        widgetName = "focus_timer";
        break;
      case "WIDGET_CLOCKS":
        widgetName = "clocks";
        break;
      case "WIDGET_PRIVACY":
        widgetName = "privacy";
        break;
      case "WIDGET_CROSSWORD":
        widgetName = "crossword";
        break;
      case "WIDGET_RECENT_SEARCHES":
        widgetName = "recent_searches";
        break;
      case "WIDGET_PICTURE_OF_THE_DAY":
        widgetName = "picture_of_the_day";
        break;
    }

    if (widgetName) {
      const { widgetsMaximized, widgetsMayBeMaximized } =
        this.props.enabledWidgets;

      let widgetSize;
      if (widgetName === "weather") {
        if (
          this.props.mayHaveWeatherForecast &&
          this.props.weatherDisplay === "detailed"
        ) {
          widgetSize =
            widgetsMayBeMaximized && !widgetsMaximized ? "medium" : "large";
        } else {
          widgetSize = "small";
        }
      } else {
        widgetSize =
          widgetsMayBeMaximized && !widgetsMaximized ? "medium" : "large";
      }

      const data = {
        widget_name: widgetName,
        widget_source: "customize_panel",
        enabled: eventValue,
        widget_size: widgetSize,
      };

      this.props.dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_ENABLED,
          data,
        })
      );
    }
  }

  onPreferenceSelect(e) {
    // eventSource: WALLPAPERS | WEATHER | TOP_SITES | TOP_STORIES | WIDGET_LISTS | WIDGET_TIMER
    const { preference, eventSource } = e.target.dataset;
    let value;
    if (e.target.nodeName === "MOZ-SELECT") {
      value = parseInt(e.target.value, 10);
    } else if (
      e.target.nodeName === "INPUT" ||
      e.target.nodeName === "MOZ-CHECKBOX"
    ) {
      value = e.target.checked;
      if (eventSource) {
        this.inputUserEvent(eventSource, value);
      }
    } else if (e.target.nodeName === "MOZ-TOGGLE") {
      value = e.target.pressed;
      if (eventSource) {
        this.inputUserEvent(eventSource, value);
      }
    }
    this.props.setPref(preference, value);
  }

  componentDidMount() {
    this.setDrawerMargins();
  }

  componentDidUpdate() {
    this.setDrawerMargins();
  }

  setDrawerMargins() {
    this.setDrawerMargin(
      `TOP_SITES`,
      this.props.enabledSections.topSitesEnabled
    );
    this.setDrawerMargin(
      `TOP_STORIES`,
      this.props.enabledSections.pocketEnabled
    );
    this.setDrawerMargin(`WIDGETS`, this.props.widgetsEnabled);
  }

  setDrawerMargin(drawerID, isOpen) {
    let drawerRef;

    switch (drawerID) {
      case `TOP_SITES`:
        drawerRef = this.topSitesDrawerRef.current;
        break;
      case `TOP_STORIES`:
        drawerRef = this.pocketDrawerRef.current;
        break;
      case `WIDGETS`:
        drawerRef = this.widgetsMgmtDrawerRef.current;
        break;
      default:
        return;
    }

    if (drawerRef) {
      // Use measured height if valid, otherwise use a large fallback
      // since overflow:hidden on the parent safely hides the drawer
      let drawerHeight = drawerRef.offsetHeight || 100;

      if (isOpen) {
        // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep the marginTop assignment
        drawerRef.style.marginTop = this.props.novaEnabled
          ? ""
          : "var(--space-small)";
      } else {
        drawerRef.style.marginTop = `-${drawerHeight + 3}px`;
      }
    }
  }

  render() {
    const {
      enabledSections,
      enabledWidgets,
      pocketRegion,
      mayHaveInferredPersonalization,
      mayHaveWeather,
      mayHaveWebNotifications,
      mayHaveWidgets,
      mayHaveTimerWidget,
      mayHaveListsWidget,
      mayHaveClocksWidget,
      mayHavePrivacyWidget,
      mayHaveCrosswordWidget,
      mayHavePictureOfTheDayWidget,
      mayHaveRecentSearchesWidget,
      openPreferences,
      wallpapersUserEnabled,
      activeWallpaper,
      setPref,
      mayHaveTopicSections,
      panelShowing,
      toggleSectionsMgmtPanel,
      showSectionsMgmtPanel,
      // @nova-cleanup(remove-conditional): Remove novaEnabled
      novaEnabled,
      browserNovaEnabled,
      toggleThemesPanel,
      showThemesPanel,
      showWallpapersPanel,
      wallpapersPanelCategory,
      openWallpapersPanel,
      closeWallpapersPanel,
      wallpapersEnabled,
      toggleWidgetsManagementPanel,
      showWidgetsManagementPanel,
      widgetsEnabled,
      lockedPrefs = [],
    } = this.props;
    const {
      topSitesEnabled,
      pocketEnabled,
      weatherEnabled,
      showInferredPersonalizationEnabled,
      topSitesRowsCount,
      webNotificationsEnabled,
    } = enabledSections;
    const {
      timerEnabled,
      listsEnabled,
      clocksEnabled,
      privacyEnabled,
      crosswordEnabled,
      pictureOfTheDayEnabled,
      recentSearchesEnabled,
    } = enabledWidgets;

    // @nova-cleanup(remove-conditional): Remove novaEnabled check and newtab-custom-stories-toggle, default to newtab-recommended-stories-toggle
    let pocketToggleL10nId;
    if (mayHaveInferredPersonalization) {
      pocketToggleL10nId = "newtab-custom-stories-personalized-toggle";
    } else if (novaEnabled) {
      pocketToggleL10nId = "newtab-recommended-stories-toggle";
    } else {
      pocketToggleL10nId = "newtab-custom-stories-toggle";
    }

    // @nova-cleanup(remove-conditional): This conditional adds the toggle for wallpaper visibility.
    return (
      <>
        <div className="home-section">
          {browserNovaEnabled && (
            <div className="appearance-section section">
              <h2 data-l10n-id="newtab-custom-appearance-section-title"></h2>
              <theme-picker
                ref={hideThemePickerLabels}
                layout="compact"
                installsource="about:newtab"
              ></theme-picker>
              <ThemesManagementPanel
                togglePanel={toggleThemesPanel}
                showPanel={showThemesPanel}
              />
            </div>
          )}
          {wallpapersEnabled && (
            <>
              <div className="wallpapers-section">
                {novaEnabled && (
                  <moz-toggle
                    id="wallpapers-toggle"
                    pressed={
                      (wallpapersUserEnabled && !!activeWallpaper) || null
                    }
                    ontoggle={this.onPreferenceSelect}
                    data-preference="newtabWallpapers.user.enabled"
                    data-event-source="WALLPAPERS"
                    data-l10n-id="newtab-wallpaper-toggle-title"
                  />
                )}
                <WallpaperCategories
                  setPref={setPref}
                  activeWallpaper={activeWallpaper}
                  panelShowing={panelShowing}
                  showPanel={showWallpapersPanel}
                  activeCategory={wallpapersPanelCategory}
                  openPanel={openWallpapersPanel}
                  closePanel={closeWallpapersPanel}
                />
              </div>
            </>
          )}
          {/* eslint-disable-next-line jsdoc/no-bad-blocks */}
          {/* @nova-cleanup(remove-conditional): Delete this widget toggle block.
              With it, delete the mayHave*Widget names and the per-widget
              enabledWidgets destructure in render() above, and the
              mayHave*Widget props and per-widget *Enabled members of
              enabledWidgets that Base.jsx and CustomizeMenu.jsx pass down only
              for this block. Keep the enabledWidgets prop itself: inputUserEvent
              reads its widgetsMaximized and widgetsMayBeMaximized members
              through this.props. */}
          {mayHaveWidgets && !novaEnabled && (
            <div className="widgets-section">
              <div className="category-header">
                <h2 data-l10n-id="newtab-custom-widget-section-title"></h2>
              </div>
              <div className="settings-widgets">
                {/* Weather */}
                {mayHaveWeather && (
                  <div id="weather-section" className="section">
                    <moz-toggle
                      id="weather-toggle"
                      pressed={weatherEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="showWeather"
                      data-event-source="WEATHER"
                      data-l10n-id="newtab-custom-widget-weather-toggle"
                    />
                  </div>
                )}

                {/* Lists */}
                {mayHaveListsWidget && (
                  <div id="lists-widget-section" className="section">
                    <moz-toggle
                      id="lists-toggle"
                      pressed={listsEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.lists.enabled"
                      data-event-source="WIDGET_LISTS"
                      data-l10n-id="newtab-custom-widget-lists-toggle"
                    />
                  </div>
                )}

                {/* Timer */}
                {mayHaveTimerWidget && (
                  <div id="timer-widget-section" className="section">
                    <moz-toggle
                      id="timer-toggle"
                      pressed={timerEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.focusTimer.enabled"
                      data-event-source="WIDGET_TIMER"
                      data-l10n-id="newtab-custom-widget-timer-toggle"
                    />
                  </div>
                )}

                {/* Clocks */}
                {mayHaveClocksWidget && (
                  <div id="clocks-widget-section" className="section">
                    <moz-toggle
                      id="clocks-toggle"
                      pressed={!!clocksEnabled}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.clocks.enabled"
                      data-event-source="WIDGET_CLOCKS"
                      data-l10n-id="newtab-custom-widget-clock-toggle"
                    />
                  </div>
                )}

                {/* Privacy */}
                {mayHavePrivacyWidget && (
                  <div id="privacy-widget-section" className="section">
                    <moz-toggle
                      id="privacy-toggle"
                      pressed={privacyEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.privacy.enabled"
                      data-event-source="WIDGET_PRIVACY"
                      data-l10n-id="newtab-custom-widget-privacy-toggle"
                    />
                  </div>
                )}

                {/* Crossword */}
                {mayHaveCrosswordWidget && (
                  <div id="crossword-widget-section" className="section">
                    <moz-toggle
                      id="crossword-toggle"
                      pressed={!!crosswordEnabled}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.crossword.enabled"
                      data-event-source="WIDGET_CROSSWORD"
                      data-l10n-id="newtab-custom-widget-crossword-toggle"
                    ></moz-toggle>
                  </div>
                )}

                {/* Picture of the day */}
                {mayHavePictureOfTheDayWidget && (
                  <div id="picture-widget-section" className="section">
                    <moz-toggle
                      id="picture-toggle"
                      pressed={pictureOfTheDayEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.pictureOfTheDay.enabled"
                      data-event-source="WIDGET_PICTURE_OF_THE_DAY"
                      data-l10n-id="newtab-custom-widget-picture-toggle"
                    />
                  </div>
                )}
                {/* Recent searches */}
                {mayHaveRecentSearchesWidget && (
                  <div id="recent-searches-widget-section" className="section">
                    <moz-toggle
                      id="recent-searches-toggle"
                      pressed={recentSearchesEnabled || null}
                      ontoggle={this.onPreferenceSelect}
                      data-preference="widgets.recentSearches.enabled"
                      data-event-source="WIDGET_RECENT_SEARCHES"
                      data-l10n-id="newtab-custom-widget-search-toggle"
                    />
                  </div>
                )}
              </div>
            </div>
          )}
          <div className="settings-toggles">
            {/* Note: If widgets are enabled, the weather toggle will be moved under Widgets subsection */}
            {
              // @nova-cleanup(remove-conditional): Remove novaEnabled conditional on data-preference; replace with data-preference="widgets.weather.enabled"
              !mayHaveWidgets && mayHaveWeather && (
                <div id="weather-section" className="section">
                  <moz-toggle
                    id="weather-toggle"
                    pressed={weatherEnabled || null}
                    ontoggle={this.onPreferenceSelect}
                    data-preference={
                      novaEnabled ? "widgets.weather.enabled" : "showWeather"
                    }
                    data-event-source="WEATHER"
                    data-l10n-id="newtab-custom-weather-toggle"
                  />
                </div>
              )
            }

            <span className="divider" role="separator"></span>

            <div id="shortcuts-section" className="section">
              <moz-toggle
                id="shortcuts-toggle"
                pressed={topSitesEnabled || null}
                ontoggle={this.onPreferenceSelect}
                data-preference="feeds.topsites"
                data-event-source="TOP_SITES"
                data-l10n-id={
                  novaEnabled
                    ? "newtab-custom-shortcuts-nova"
                    : "newtab-custom-shortcuts-toggle"
                }
              >
                <div slot="nested">
                  <div className="more-info-top-wrapper">
                    <div
                      className="more-information"
                      ref={this.topSitesDrawerRef}
                    >
                      <moz-select
                        id="row-selector"
                        className="selector"
                        name="row-count"
                        data-preference="topSitesRows"
                        value={topSitesRowsCount}
                        aria-labelledby="custom-shortcuts-title"
                        onChange={this.onPreferenceSelect}
                        // @nova-cleanup(remove-conditional): Remove novaEnabled conditional and spread operator, keep the attributes
                        {...(novaEnabled && {
                          "data-l10n-id": "newtab-custom-row-description",
                          inputLayout: "inline-end",
                        })}
                      >
                        {[1, 2, 3, 4].map(num =>
                          // @nova-cleanup(remove-conditional): Remove the conditional and "else" block after Nova lands
                          novaEnabled ? (
                            <moz-option
                              key={num}
                              value={String(num)}
                              label={String(num)}
                            />
                          ) : (
                            <moz-option
                              key={num}
                              value={String(num)}
                              data-l10n-id="newtab-custom-row-selector2"
                              data-l10n-args={`{"num": ${num}}`}
                            />
                          )
                        )}
                      </moz-select>
                    </div>
                    {mayHaveWebNotifications && (
                      <div className="more-information">
                        <moz-toggle
                          id="web-notifications-toggle"
                          pressed={webNotificationsEnabled || null}
                          ontoggle={this.onPreferenceSelect}
                          data-preference="showWebNotifications"
                          data-l10n-id="newtab-custom-web-notifications-toggle"
                        ></moz-toggle>
                      </div>
                    )}
                  </div>
                </div>
              </moz-toggle>
            </div>

            {
              // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep divider
              novaEnabled && mayHaveWidgets && (
                <span className="divider" role="separator"></span>
              )
            }
            {
              // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep toggle and WidgetsManagementPanel
              novaEnabled && mayHaveWidgets && (
                <div id="widgets-section" className="section">
                  <moz-toggle
                    id="widgets-system-toggle"
                    pressed={widgetsEnabled || null}
                    ontoggle={this.onPreferenceSelect}
                    data-preference="widgets.enabled"
                    data-event-source="WIDGETS_SYSTEM"
                    data-l10n-id="newtab-custom-widget-section-toggle"
                  >
                    <div slot="nested">
                      <div className="more-info-widgets-wrapper">
                        <div
                          className="more-information"
                          ref={this.widgetsMgmtDrawerRef}
                        >
                          <WidgetsManagementPanel
                            setPref={setPref}
                            togglePanel={toggleWidgetsManagementPanel}
                            showPanel={showWidgetsManagementPanel}
                          />
                        </div>
                      </div>
                    </div>
                  </moz-toggle>
                </div>
              )
            }

            {
              // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep divider
              // The pocketRegion check makes sure there is only one divider present if it's false
              novaEnabled && pocketRegion && (
                <span className="divider" role="separator"></span>
              )
            }

            {pocketRegion && (
              <div id="pocket-section" className="section">
                <moz-toggle
                  id="pocket-toggle"
                  pressed={pocketEnabled || null}
                  ontoggle={this.onPreferenceSelect}
                  data-preference="feeds.section.topstories"
                  data-event-source="TOP_STORIES"
                  data-l10n-id={pocketToggleL10nId}
                >
                  <div slot="nested">
                    {(mayHaveInferredPersonalization ||
                      mayHaveTopicSections) && (
                      <div className="more-info-pocket-wrapper">
                        <div
                          className="more-information"
                          ref={this.pocketDrawerRef}
                        >
                          {mayHaveInferredPersonalization && (
                            <moz-checkbox
                              id="inferred-personalization"
                              className="customize-menu-checkbox"
                              disabled={
                                !pocketEnabled ||
                                lockedPrefs.includes(
                                  PREF_INFERRED_PERSONALIZATION
                                )
                              }
                              // Renders its own `disabled`, so it opts out of
                              // CustomizeMenu's lock sweep and applies the lock
                              // itself.
                              data-lock-managed=""
                              checked={showInferredPersonalizationEnabled}
                              onChange={this.onPreferenceSelect}
                              data-preference={PREF_INFERRED_PERSONALIZATION}
                              data-event-source="INFERRED_PERSONALIZATION"
                              data-l10n-id="newtab-custom-stories-personalized-checkbox"
                            />
                          )}
                          {mayHaveTopicSections && (
                            <SectionsMgmtPanel
                              pocketEnabled={pocketEnabled}
                              togglePanel={toggleSectionsMgmtPanel}
                              showPanel={showSectionsMgmtPanel}
                              novaEnabled={novaEnabled}
                            />
                          )}
                        </div>
                      </div>
                    )}
                  </div>
                </moz-toggle>
              </div>
            )}
          </div>
          {
            // @nova-cleanup(remove-conditional): Remove this divider once Nova lands
            !novaEnabled && <span className="divider" role="separator"></span>
          }
          {
            // @nova-cleanup(remove-conditional): Remove this block once Nova ships
            !novaEnabled && (
              <div>
                <button
                  id="settings-link"
                  className="external-link"
                  onClick={openPreferences}
                  data-l10n-id="newtab-custom-settings"
                />
              </div>
            )
          }
        </div>

        {
          // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep manage-settings-footer
          novaEnabled && (
            <div className="manage-settings-footer">
              <button
                id="settings-link"
                className="external-link"
                onClick={openPreferences}
                data-l10n-id="newtab-custom-settings"
              />
            </div>
          )
        }
      </>
    );
  }
}
