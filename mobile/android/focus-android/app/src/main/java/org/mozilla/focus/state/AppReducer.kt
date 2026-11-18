/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.state

import androidx.annotation.VisibleForTesting
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.state.Reducer

/** Reducer creating a new [AppState] for dispatched [AppAction]s. */
object AppReducer : Reducer<AppState, AppAction> {
    override fun invoke(state: AppState, action: AppAction): AppState {
        return when (action) {
            is AppAction.SelectionChanged -> selectionChanged(state, action)
            is AppAction.NoTabs -> noTabs(state)
            is AppAction.EditAction -> editAction(state, action)
            is AppAction.FinishEdit -> finishEditing(state, action)
            is AppAction.HideTabs -> hideTabs(state)
            is AppAction.ShowFirstRun -> showFirstRun(state)
            is AppAction.FinishFirstRun -> finishFirstRun(state, action)
            is AppAction.Lock -> lock(state, action)
            is AppAction.Unlock -> unlock(state, action)
            is AppAction.ResetHomeAccessPoint -> resetHomeAccessPoint(state)
            is AppAction.OpenSettings -> openSettings(state, action)
            is AppAction.NavigateUp -> navigateUp(state, action)
            is AppAction.OpenTab -> openTab(state, action)
            is AppAction.TopSitesChange -> topSitesChanged(state, action)
            is AppAction.SitePermissionOptionChange -> sitePermissionOptionChanged(state, action)
            is AppAction.SecretSettingsStateChange ->
                secretSettingsStateChanged(
                    state,
                    action,
                )
            is AppAction.ShowEraseTabsCfrChange -> showEraseTabsCfrChanged(state, action)
            is AppAction.ShowStartBrowsingCfrChange -> showStartBrowsingCfrChanged(state, action)
            is AppAction.ShowTrackingProtectionCfrChange ->
                showTrackingProtectionCfrChanged(
                    state,
                    action,
                )
            is AppAction.OpenSitePermissionOptionsScreen ->
                openSitePermissionOptionsScreen(
                    state,
                    action,
                )
            is AppAction.ShowHomeScreen -> showHomeScreen(state, action.accessPoint)
            is AppAction.ShowOnboardingSecondScreen -> showOnBoardingSecondScreen(state)
            is AppAction.OpenCrashList -> openCrashlist(state)
            is AppAction.ShowSearchWidgetSnackBar -> showSearchWidgetSnackBarChanged(state, action)
            is AppAction.UpdateIsPinningSupported -> updateIsPinningSupported(state, action)
        }
    }
}

/** The currently selected tab has changed. */
private fun selectionChanged(state: AppState, action: AppAction.SelectionChanged): AppState {
    if (state.screen is Screen.FirstRun || state.screen is Screen.Locked) {
        return state
    }

    if (state.screen is Screen.EditUrl) {
        return state
    }

    return state.copy(screen = Screen.Browser(tabId = action.tabId, showTabs = false))
}

/** All tabs have been closed. */
private fun noTabs(state: AppState): AppState {
    if (state.screen is Screen.Home || state.screen is Screen.FirstRun || state.screen is Screen.Locked) {
        return state
    }
    return state.copy(screen = Screen.Home())
}

/** The user wants to edit the URL of a tab. */
private fun editAction(state: AppState, action: AppAction.EditAction): AppState {
    return state.copy(screen = Screen.EditUrl(action.tabId))
}

/** The user finished editing the URL. */
private fun finishEditing(state: AppState, action: AppAction.FinishEdit): AppState {
    return state.copy(screen = Screen.Browser(tabId = action.tabId, showTabs = false))
}

/** Hide the tabs tray. */
private fun hideTabs(state: AppState): AppState {
    return if (state.screen is Screen.Browser) {
        state.copy(screen = state.screen.copy(showTabs = false))
    } else {
        state
    }
}

/** The user finished the first run onboarding. */
private fun finishFirstRun(state: AppState, action: AppAction.FinishFirstRun): AppState {
    return if (action.tabId != null) {
        state.copy(screen = Screen.Browser(action.tabId, showTabs = false))
    } else {
        state.copy(screen = Screen.Home())
    }
}

/** Force showing the first run screen (for testing). */
@VisibleForTesting
internal fun showFirstRun(state: AppState): AppState {
    if (state.screen is Screen.FirstRun) {
        return state
    }
    return state.copy(screen = Screen.FirstRun)
}

@VisibleForTesting
internal fun showOnBoardingSecondScreen(state: AppState): AppState {
    if (state.screen is Screen.OnboardingSecondScreen) {
        return state
    }
    return state.copy(screen = Screen.OnboardingSecondScreen)
}

/** Force showing the home screen. */
@VisibleForTesting
internal fun showHomeScreen(state: AppState, accessPoint: String): AppState {
    if (state.screen == Screen.Home(accessPoint)) {
        return state
    }
    return state.copy(screen = Screen.Home(accessPoint))
}

/** Lock the application. */
@VisibleForTesting
internal fun lock(state: AppState, action: AppAction.Lock): AppState {
    if (state.screen is Screen.Locked) {
        return state
    }
    val homeAccessPoint =
        if (state.screen is Screen.Home) {
            state.screen.accessPoint
        } else {
            SearchAccessPoint.DEFAULT
        }
    return state.copy(screen = Screen.Locked(action.bundle, homeAccessPoint))
}

/** Unlock the application. */
private fun unlock(state: AppState, action: AppAction.Unlock): AppState {
    val screen = state.screen
    if (screen !is Screen.Locked) {
        return state
    }

    return if (action.tabId != null) {
        state.copy(screen = Screen.Browser(action.tabId, showTabs = false))
    } else {
        state.copy(screen = Screen.Home(screen.homeAccessPoint))
    }
}

/** Clear the search access point of the home screen, including the one to show once unlocked. */
@VisibleForTesting
internal fun resetHomeAccessPoint(state: AppState): AppState {
    return when (val screen = state.screen) {
        is Screen.Home ->
            if (screen.accessPoint == SearchAccessPoint.DEFAULT) {
                state
            } else {
                state.copy(screen = screen.copy(accessPoint = SearchAccessPoint.DEFAULT))
            }
        is Screen.Locked ->
            if (screen.homeAccessPoint == SearchAccessPoint.DEFAULT) {
                state
            } else {
                state.copy(screen = screen.copy(homeAccessPoint = SearchAccessPoint.DEFAULT))
            }
        else -> state
    }
}

private fun openSettings(state: AppState, action: AppAction.OpenSettings): AppState {
    return state.copy(screen = Screen.Settings(page = action.page))
}

private fun openCrashlist(state: AppState): AppState {
    return state.copy(screen = Screen.CrashListScreen)
}

private fun openTab(state: AppState, action: AppAction.OpenTab): AppState {
    return state.copy(screen = Screen.Browser(tabId = action.tabId, showTabs = false))
}

/** The list of [TopSite] has changed. */
private fun topSitesChanged(state: AppState, action: AppAction.TopSitesChange): AppState {
    return state.copy(topSites = action.topSites)
}

/** The rules of site permissions autoplay has changed. */
private fun sitePermissionOptionChanged(
    state: AppState,
    action: AppAction.SitePermissionOptionChange,
): AppState {
    return state.copy(sitePermissionOptionChange = action.value)
}

/** The state of secret settings has changed. */
private fun secretSettingsStateChanged(
    state: AppState,
    action: AppAction.SecretSettingsStateChange,
): AppState {
    return state.copy(secretSettingsEnabled = action.enabled)
}

/** The state of erase tabs CFR changed */
private fun showEraseTabsCfrChanged(
    state: AppState,
    action: AppAction.ShowEraseTabsCfrChange,
): AppState {
    return state.copy(showEraseTabsCfr = action.value)
}

/** Update whether the start browsing CFR should be shown or not */
private fun showStartBrowsingCfrChanged(
    state: AppState,
    action: AppAction.ShowStartBrowsingCfrChange,
): AppState {
    return state.copy(showStartBrowsingTabsCfr = action.value)
}

/** The state of search widget snackBar changed */
private fun showSearchWidgetSnackBarChanged(
    state: AppState,
    action: AppAction.ShowSearchWidgetSnackBar,
): AppState {
    return state.copy(showSearchWidgetSnackbar = action.value)
}

/** The state of tracking protection CFR changed */
private fun showTrackingProtectionCfrChanged(
    state: AppState,
    action: AppAction.ShowTrackingProtectionCfrChange,
): AppState {
    return state.copy(showTrackingProtectionCfrForTab = action.value)
}

private fun openSitePermissionOptionsScreen(
    state: AppState,
    action: AppAction.OpenSitePermissionOptionsScreen,
): AppState {
    return state.copy(screen = Screen.SitePermissionOptionsScreen(sitePermission = action.sitePermission))
}

private fun updateIsPinningSupported(
    state: AppState,
    action: AppAction.UpdateIsPinningSupported,
): AppState {
    return state.copy(isPinningSupported = action.value)
}

private fun navigateUp(state: AppState, action: AppAction.NavigateUp): AppState {
    val nextScreen =
        when (val currentScreen = state.screen) {
            is Screen.Browser ->
                if (action.tabId != null) {
                    Screen.Browser(action.tabId, false)
                } else {
                    Screen.Home()
                }

            is Screen.SitePermissionOptionsScreen -> {
                Screen.Settings(Screen.Settings.Page.SitePermissions)
            }

            is Screen.Settings -> {
                if (currentScreen.page == Screen.Settings.Page.Start) {
                    if (action.tabId != null) Screen.Browser(action.tabId, false) else Screen.Home()
                } else {
                    val parentPage = settingsParentMap[currentScreen.page] ?: Screen.Settings.Page.Start
                    Screen.Settings(parentPage)
                }
            }

            else -> currentScreen
        }

    return state.copy(screen = nextScreen)
}

private val settingsParentMap =
    mapOf(
        Screen.Settings.Page.General to Screen.Settings.Page.Start,
        Screen.Settings.Page.Privacy to Screen.Settings.Page.Start,
        Screen.Settings.Page.Search to Screen.Settings.Page.Start,
        Screen.Settings.Page.Advanced to Screen.Settings.Page.Start,
        Screen.Settings.Page.Mozilla to Screen.Settings.Page.Start,
        Screen.Settings.Page.PrivacyExceptions to Screen.Settings.Page.Privacy,
        Screen.Settings.Page.SitePermissions to Screen.Settings.Page.Privacy,
        Screen.Settings.Page.SecretSettings to Screen.Settings.Page.Advanced,
        Screen.Settings.Page.SearchList to Screen.Settings.Page.Search,
        Screen.Settings.Page.SearchAutocomplete to Screen.Settings.Page.Search,
        Screen.Settings.Page.About to Screen.Settings.Page.Mozilla,
        Screen.Settings.Page.Licenses to Screen.Settings.Page.Mozilla,
        Screen.Settings.Page.CrashList to Screen.Settings.Page.Mozilla,
        Screen.Settings.Page.Locale to Screen.Settings.Page.General,
        Screen.Settings.Page.PrivacyExceptionsRemove to Screen.Settings.Page.PrivacyExceptions,
        Screen.Settings.Page.SearchRemove to Screen.Settings.Page.SearchList,
        Screen.Settings.Page.SearchAdd to Screen.Settings.Page.SearchList,
        Screen.Settings.Page.SearchAutocompleteList to Screen.Settings.Page.SearchAutocomplete,
        Screen.Settings.Page.SearchAutocompleteAdd to Screen.Settings.Page.SearchAutocompleteList,
        Screen.Settings.Page.SearchAutocompleteRemove to Screen.Settings.Page.SearchAutocompleteList,
    )
