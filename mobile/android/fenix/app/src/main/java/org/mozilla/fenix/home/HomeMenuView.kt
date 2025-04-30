/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import mozilla.appservices.fxaclient.FxaServer
import mozilla.appservices.fxaclient.contentUrl
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.menu.view.MenuButton
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.HomeScreen
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.AccountState
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.deletebrowsingdata.deleteAndQuit
import org.mozilla.fenix.theme.ThemeManager
import org.mozilla.fenix.whatsnew.WhatsNew
import java.lang.ref.WeakReference
import org.mozilla.fenix.GleanMetrics.HomeMenu as HomeMenuMetrics

/**
 * Helper class for building the [HomeMenu].
 *
 * @param context An Android [Context].
 * @param lifecycleOwner [LifecycleOwner] for the view.
 * @param homeActivity [HomeActivity] used to open URLs in a new tab.
 * @param navController [NavController] used for navigation.
 * @param homeFragment [HomeFragment] used to attach the biometric prompt.
 * @param menuButton The [MenuButton] that will be used to create a menu when the button is
 * clicked.
 * @param fxaEntrypoint The source entry point to FxA.
 */
@Suppress("LongParameterList")
class HomeMenuView(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val homeActivity: HomeActivity,
    private val navController: NavController,
    private val homeFragment: HomeFragment,
    private val menuButton: WeakReference<MenuButton>,
    private val fxaEntrypoint: FxAEntryPoint = FenixFxAEntryPoint.HomeMenu,
) {

    /**
     * Builds the [HomeMenu].
     */
    fun build() {
        if (!context.settings().enableMenuRedesign) {
            HomeMenu(
                lifecycleOwner = lifecycleOwner,
                context = context,
                onItemTapped = ::onItemTapped,
                onHighlightPresent = { menuButton.get()?.setHighlight(it) },
                onMenuBuilderChanged = { menuButton.get()?.menuBuilder = it },
            )
        }

        menuButton.get()?.setColorFilter(
            ContextCompat.getColor(
                context,
                ThemeManager.resolveAttribute(R.attr.textPrimary, context),
            ),
        )

        menuButton.get()?.register(
            object : mozilla.components.concept.menu.MenuButton.Observer {
                override fun onShow() {
                    if (context.settings().enableMenuRedesign) {
                        navController.nav(
                            R.id.homeFragment,
                            HomeFragmentDirections.actionGlobalMenuDialogFragment(
                                accesspoint = MenuAccessPoint.Home,
                            ),
                        )
                        Events.toolbarMenuVisible.record(NoExtras())
                    } else {
                        // MenuButton used in [HomeMenuView] doesn't emit toolbar facts.
                        // A wrapper is responsible for that, but we are using the button
                        // directly, hence recording the event directly.
                        // Should investigate further: https://bugzilla.mozilla.org/show_bug.cgi?id=1868207
                        Events.toolbarMenuVisible.record(NoExtras())
                    }
                }
            },
        )
    }

    /**
     * Dismisses the menu.
     */
    fun dismissMenu() {
        menuButton.get()?.dismissMenu()
    }

    /**
     * Callback invoked when a menu item is tapped on.
     */
    @Suppress("LongMethod", "ComplexMethod")
    @VisibleForTesting(otherwise = PRIVATE)
    internal fun onItemTapped(item: HomeMenu.Item) {
        when (item) {
            HomeMenu.Item.Settings -> {
                HomeMenuMetrics.settingsItemClicked.record(NoExtras())

                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalSettingsFragment(),
                )
            }
            HomeMenu.Item.CustomizeHome -> {
                HomeScreen.customizeHomeClicked.record(NoExtras())

                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
                )
            }
            is HomeMenu.Item.SyncAccount -> {
                navController.nav(
                    R.id.homeFragment,
                    when (item.accountState) {
                        AccountState.AUTHENTICATED ->
                            HomeFragmentDirections.actionGlobalAccountSettingsFragment()
                        AccountState.NEEDS_REAUTHENTICATION ->
                            HomeFragmentDirections.actionGlobalAccountProblemFragment(
                                entrypoint = fxaEntrypoint as FenixFxAEntryPoint,
                            )
                        AccountState.NO_ACCOUNT ->
                            HomeFragmentDirections.actionGlobalTurnOnSync(
                                entrypoint = fxaEntrypoint as FenixFxAEntryPoint,
                            )
                    },
                )
            }
            HomeMenu.Item.ManageAccountAndDevices -> {
                homeActivity.openToBrowserAndLoad(
                    searchTermOrURL = FxaServer.Release.contentUrl() + "/settings",
                    newTab = true,
                    from = BrowserDirection.FromHome,
                )
            }
            HomeMenu.Item.Bookmarks -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalBookmarkFragment(BookmarkRoot.Mobile.id),
                )
            }
            HomeMenu.Item.History -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalHistoryFragment(),
                )
            }
            HomeMenu.Item.Downloads -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalDownloadsFragment(),
                )
            }
            HomeMenu.Item.Passwords -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionHomeFragmentToLoginsListFragment(),
                )
            }
            HomeMenu.Item.Help -> {
                HomeMenuMetrics.helpTapped.record(NoExtras())
                homeActivity.openToBrowserAndLoad(
                    searchTermOrURL = SupportUtils.getSumoURLForTopic(
                        context = context,
                        topic = SupportUtils.SumoTopic.HELP,
                    ),
                    newTab = true,
                    from = BrowserDirection.FromHome,
                )
            }
            HomeMenu.Item.WhatsNew -> {
                WhatsNew.userViewedWhatsNew(context)
                Events.whatsNewTapped.record(Events.WhatsNewTappedExtra(source = "HOME"))

                homeActivity.openToBrowserAndLoad(
                    searchTermOrURL = SupportUtils.WHATS_NEW_URL,
                    newTab = true,
                    from = BrowserDirection.FromHome,
                )
            }
            HomeMenu.Item.Quit -> {
                deleteAndQuit(
                    activity = homeActivity,
                    coroutineScope = homeActivity.lifecycleScope,
                )
            }
            HomeMenu.Item.ReconnectSync -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalAccountProblemFragment(
                        entrypoint = fxaEntrypoint as FenixFxAEntryPoint,
                    ),
                )
            }
            HomeMenu.Item.Extensions -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalAddonsManagementFragment(),
                )
            }
        }
    }
}
