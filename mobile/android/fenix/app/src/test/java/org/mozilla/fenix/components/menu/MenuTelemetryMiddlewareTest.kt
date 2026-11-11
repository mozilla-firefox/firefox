/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertNotNull
import mozilla.components.compose.menu.store.MenuState as CustomizableMenuState
import mozilla.components.compose.menu.store.MenuStore as CustomizableMenuStore
import mozilla.components.feature.addons.Addon
import mozilla.components.service.fxa.manager.AccountState
import mozilla.components.support.test.robolectric.testContext
import mozilla.telemetry.glean.private.CounterMetricType
import mozilla.telemetry.glean.private.EventMetricType
import mozilla.telemetry.glean.private.NoExtras
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.AppMenu
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.HomeMenu
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.components.menu.middleware.MenuTelemetryMiddleware
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MenuTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN adding a bookmark THEN record the bookmark browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.AddBookmark)

        assertTelemetryRecorded(Events.browserMenuAction, item = "add_bookmark")
    }

    @Test
    fun `GIVEN middleware is registered to the customizable menu store WHEN an action is dispatched THEN record telemetry`() {
        val store =
            CustomizableMenuStore(
                initialState = CustomizableMenuState(emptyList()),
                middleware = listOf(MenuTelemetryMiddleware(accessPoint = MenuAccessPoint.Browser)),
            )
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.AddBookmark)

        assertTelemetryRecorded(Events.browserMenuAction, item = "add_bookmark")
    }

    @Test
    fun `WHEN navigating to edit a bookmark THEN record the edit bookmark browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.EditBookmark())

        assertTelemetryRecorded(Events.browserMenuAction, item = "edit_bookmark")
    }

    @Test
    fun `WHEN navigating to bookmarks THEN record the bookmarks browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Bookmarks)

        assertTelemetryRecorded(Events.browserMenuAction, item = "bookmarks")
    }

    @Test
    fun `WHEN adding a shortcut THEN record the add to top sites browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.AddShortcut)

        assertTelemetryRecorded(Events.browserMenuAction, item = "add_to_top_sites")
    }

    @Test
    fun `WHEN removing a shortcut from top sites THEN record the remove from top sites browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.RemoveShortcut)

        assertTelemetryRecorded(Events.browserMenuAction, item = "remove_from_top_sites")
    }

    fun `WHEN navigating to add site to home screen THEN record the add_to_homescreen browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.AddToHomeScreen)

        assertTelemetryRecorded(Events.browserMenuAction, item = "add_to_homescreen")
    }

    @Test
    fun `WHEN navigating to downloads THEN record the downloads browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Downloads)

        assertTelemetryRecorded(Events.browserMenuAction, item = "downloads")
    }

    @Test
    fun `WHEN navigating to history THEN record the history browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.History)

        assertTelemetryRecorded(Events.browserMenuAction, item = "history")
    }

    @Test
    fun `WHEN navigating to manage extensions THEN record the manage extensions browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.ManageExtensions)

        assertTelemetryRecorded(Events.browserMenuAction, item = "addons_manager")
    }

    @Test
    fun `WHEN navigating to sync account THEN record the sync account browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(
            MenuAction.Navigate.MozillaAccount(
                accountState = AccountState.NotAuthenticated,
                accesspoint = MenuAccessPoint.Browser,
            )
        )

        assertTelemetryRecorded(Events.browserMenuAction, item = "sync_account")
        assertTelemetryRecorded(AppMenu.signIntoSync)
    }

    @Test
    fun `WHEN opening a site in app THEN record the open in app menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.OpenInApp)

        assertTelemetryRecorded(Events.browserMenuAction, item = "open_in_app")
    }

    @Test
    fun `WHEN navigating to passwords THEN record the passwords browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Passwords)

        assertTelemetryRecorded(Events.browserMenuAction, item = "passwords")
    }

    @Test
    fun `WHEN navigating to the save to collection sheet THEN record the share browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.SaveToCollection(hasCollection = true))

        assertTelemetryRecorded(Events.browserMenuAction, item = "save_to_collection")
    }

    @Test
    fun `WHEN navigating back without view history THEN record back telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Back(viewHistory = false))

        assertTelemetryRecorded(Events.browserMenuAction, item = "back")
    }

    @Test
    fun `WHEN navigating back with view history THEN record back long press telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Back(viewHistory = true))

        assertTelemetryRecorded(Events.browserMenuAction, item = "back_long_press")
    }

    @Test
    fun `WHEN navigating back from external menu without view history THEN record custom back telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Back(viewHistory = false))

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_back")
    }

    @Test
    fun `WHEN navigating back from external menu with view history THEN record custom back long press telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Back(viewHistory = true))

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_back_long_press")
    }

    @Test
    fun `WHEN navigating forward without view history THEN record forward telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Forward(viewHistory = false))

        assertTelemetryRecorded(Events.browserMenuAction, item = "forward")
    }

    @Test
    fun `WHEN navigating forward with view history THEN record forward long press telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Forward(viewHistory = true))

        assertTelemetryRecorded(Events.browserMenuAction, item = "forward_long_press")
    }

    @Test
    fun `WHEN navigating forward from external menu without view history THEN record custom forward telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Forward(viewHistory = false))

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_forward")
    }

    @Test
    fun `WHEN navigating forward from external menu with view history THEN record custom forward long press telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Forward(viewHistory = true))

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_forward_long_press")
    }

    @Test
    fun `WHEN reloading page THEN record reload telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Reload(bypassCache = false))

        assertTelemetryRecorded(Events.browserMenuAction, item = "reload")
    }

    @Test
    fun `WHEN stopping page load THEN record stop telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Stop)

        assertTelemetryRecorded(Events.browserMenuAction, item = "stop")
    }

    @Test
    fun `WHEN navigating to the share sheet THEN record the share browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Share)

        assertTelemetryRecorded(Events.browserMenuAction, item = "share")
    }

    @Test
    fun `WHEN navigating to the share sheet in custom tab THEN record the share custom tab telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.Share)

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_share")
    }

    @Test
    fun `GIVEN the menu accesspoint is from the browser WHEN navigating to the settings THEN record the settings browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())
        assertNull(HomeMenu.settingsItemClicked.testGetValue())

        store.dispatch(MenuAction.Navigate.Settings)

        assertTelemetryRecorded(Events.browserMenuAction, item = "settings")
        assertNull(HomeMenu.settingsItemClicked.testGetValue())
    }

    @Test
    fun `WHEN navigating to the homepage settings THEN record the change wallpaper browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.CustomizeHomepage)

        assertTelemetryRecorded(Events.browserMenuAction, item = "customize_homepage")
    }

    @Test
    fun `GIVEN the menu accesspoint is from the home screen WHEN navigating to the settings THEN record the home menu interaction telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.Home)
        assertNull(Events.browserMenuAction.testGetValue())
        assertNull(HomeMenu.settingsItemClicked.testGetValue())

        store.dispatch(MenuAction.Navigate.Settings)

        assertTelemetryRecorded(HomeMenu.settingsItemClicked)
        assertNull(Events.browserMenuAction.testGetValue())
    }

    @Test
    fun `WHEN navigating to translations dialog THEN record the translate browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())
        assertNull(Translations.action.testGetValue())

        store.dispatch(MenuAction.Navigate.Translate)

        assertTelemetryRecorded(Events.browserMenuAction, item = "translate")

        val snapshot = Translations.action.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("main_flow_browser", snapshot.single().extra?.getValue("item"))
    }

    @Test
    fun `WHEN deleting browsing data and quitting THEN record the quit browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.DeleteBrowsingDataAndQuit)

        assertTelemetryRecorded(Events.browserMenuAction, item = "quit")
    }

    @Test
    fun `WHEN find in page feature is started THEN record the find in page browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.FindInPage)

        assertTelemetryRecorded(Events.browserMenuAction, item = "find_in_page")
    }

    @Test
    fun `WHEN find in page feature is started in custom tab THEN record the find in page custom tab telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.FindInPage)

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_find_in_page")
    }

    @Test
    fun `WHEN menu banner action is dispatched THEN record the menu banner telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.DefaultBrowserMenuBannerClicked)

        assertTelemetryRecorded(Events.browserMenuAction, item = "menu_banner")
    }

    @Test
    fun `WHEN dismiss menu banner action is dispatched THEN record the menu banner telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.DefaultBrowserMenuBannerDismissed)

        assertTelemetryRecorded(Events.browserMenuAction, item = "dismiss_menu_banner")
    }

    @Test
    fun `WHEN customize reader view action is dispatched THEN record the reader mode appearance telemetry`() {
        val store = createStore()
        assertNull(ReaderMode.appearance.testGetValue())

        store.dispatch(MenuAction.CustomizeReaderView)

        assertTelemetryRecorded(ReaderMode.appearance)
    }

    @Test
    fun `WHEN requesting desktop site THEN record the desktop view ON telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.RequestDesktopSite)

        assertTelemetryRecorded(Events.browserMenuAction, item = "desktop_view_on")
    }

    @Test
    fun `WHEN requesting desktop site in custom tab THEN record the desktop view ON custom tab telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.RequestDesktopSite)

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_desktop_view_on")
    }

    @Test
    fun `WHEN requesting mobile site THEN record the desktop view OFF telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.RequestMobileSite)

        assertTelemetryRecorded(Events.browserMenuAction, item = "desktop_view_off")
    }

    @Test
    fun `WHEN requesting mobile site in custom tab THEN record the desktop view OFF custom tab telemetry`() {
        val store = createStore(accessPoint = MenuAccessPoint.External)
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.RequestMobileSite)

        assertTelemetryRecorded(Events.browserMenuAction, item = "custom_desktop_view_off")
    }

    fun `When opening a site in browser THEN record the open in Fenix telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.OpenInFirefox)

        assertTelemetryRecorded(Events.browserMenuAction, item = "open_in_fenix")
    }

    @Test
    fun `WHEN navigating to the discover more extensions page THEN record the discover more extensions browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.DiscoverMoreExtensions)

        assertTelemetryRecorded(Events.browserMenuAction, item = "discover_more_extensions")
    }

    @Test
    fun `WHEN navigating to an add-on's details THEN record the addon details browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.AddonDetails(Addon("")))

        assertTelemetryRecorded(Events.browserMenuAction, item = "addon_details")
    }

    @Test
    fun `WHEN installing an add-on THEN record the install addon browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.InstallAddon(Addon("")))

        assertTelemetryRecorded(Events.browserMenuAction, item = "install_addon")
    }

    @Test
    fun `WHEN navigating to an installed add-on's details THEN record the installed addon details browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.InstalledAddonDetails(Addon("")))

        assertTelemetryRecorded(Events.browserMenuAction, item = "installed_addon_details")
    }

    @Test
    fun `WHEN moving to a non-private tab THEN record the move to non-private tab browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.MoveToNonPrivateTab)

        assertTelemetryRecorded(Events.browserMenuAction, item = "move_to_non_private_tab")
    }

    @Test
    fun `WHEN navigating to web compat reporter THEN record the web compat reporter telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.WebCompatReporter)

        assertTelemetryRecorded(Events.browserMenuAction, item = "report_broken_site")
    }

    @Test
    fun `WHEN adding a tab to a group THEN record the group flow telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(
            MenuAction.Navigate.OpenTabGroupFlow(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        )

        assertTelemetryRecorded(Events.browserMenuAction, item = "add_to_group")
    }

    @Test
    fun `WHEN navigating to open a new tab THEN record the new tab browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.NewTab)

        assertTelemetryRecorded(Events.browserMenuAction, item = "new_tab")
    }

    @Test
    fun `WHEN navigating to open a new private tab THEN record the new private tab browser menu telemetry`() {
        val store = createStore()
        assertNull(Events.browserMenuAction.testGetValue())

        store.dispatch(MenuAction.Navigate.NewPrivateTab)

        assertTelemetryRecorded(Events.browserMenuAction, item = "new_private_tab")
    }

    private fun assertTelemetryRecorded(
        event: EventMetricType<Events.BrowserMenuActionExtra>,
        item: String,
    ) {
        assertNotNull(event.testGetValue())

        val snapshot = event.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(item, snapshot.single().extra?.getValue("item"))
    }

    private fun assertTelemetryRecorded(event: EventMetricType<NoExtras>) {
        assertNotNull(event.testGetValue())
        assertEquals(1, event.testGetValue()!!.size)
    }

    private fun assertTelemetryRecorded(event: CounterMetricType) {
        assertNotNull(event.testGetValue())
        assertEquals(1, event.testGetValue()!!)
    }

    private fun createStore(
        menuState: MenuState = MenuState(),
        accessPoint: MenuAccessPoint = MenuAccessPoint.Browser,
    ) =
        MenuStore(
            initialState = menuState,
            middleware = listOf(MenuTelemetryMiddleware(accessPoint = accessPoint)),
        )
}
