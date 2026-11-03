/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import kotlin.reflect.KClass
import kotlin.test.assertEquals
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.addons.ExtensionsMenuItemProvider
import org.mozilla.fenix.bookmarks.BookmarkMenuItemProvider
import org.mozilla.fenix.bookmarks.BookmarksScreenMenuItemProvider
import org.mozilla.fenix.browser.BackMenuItemProvider
import org.mozilla.fenix.browser.DefaultBrowserMenuItemProvider
import org.mozilla.fenix.browser.DesktopSiteMenuItemProvider
import org.mozilla.fenix.browser.ForwardMenuItemProvider
import org.mozilla.fenix.browser.RefreshMenuItemProvider
import org.mozilla.fenix.browser.ShareMenuItemProvider
import org.mozilla.fenix.browser.applinks.OpenInAppMenuItemProvider
import org.mozilla.fenix.browser.applinks.OpenInFirefoxMenuItemProvider
import org.mozilla.fenix.browser.menu.MoreMenuItemsProvider
import org.mozilla.fenix.browser.menu.MoveToNormalTabsMenuItemProvider
import org.mozilla.fenix.browser.readermode.ReaderViewMenuItemProvider
import org.mozilla.fenix.collections.SaveToCollectionMenuItemProvider
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.FindInPageMenuItemProvider
import org.mozilla.fenix.components.accounts.MozillaAccountMenuItemProvider
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.customtabs.CustomTabAppMenuItemProvider
import org.mozilla.fenix.downloads.DownloadsMenuItemProvider
import org.mozilla.fenix.home.CustomizeHomepageMenuItemProvider
import org.mozilla.fenix.home.topsites.ShortcutMenuItemProvider
import org.mozilla.fenix.ipprotection.VpnMenuItemProvider
import org.mozilla.fenix.library.history.HistoryMenuItemProvider
import org.mozilla.fenix.pdf.SaveAsPdfMenuItemProvider
import org.mozilla.fenix.print.PrintMenuItemProvider
import org.mozilla.fenix.settings.SettingsMenuItemProvider
import org.mozilla.fenix.settings.deletebrowsingdata.QuitMenuItemProvider
import org.mozilla.fenix.settings.logins.PasswordsMenuItemProvider
import org.mozilla.fenix.shortcut.AddToHomeScreenMenuItemProvider
import org.mozilla.fenix.summarization.SummarizePageMenuItemProvider
import org.mozilla.fenix.translations.TranslationsMenuItemProvider
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.webcompat.ReportBrokenSiteMenuItemProvider

@RunWith(AndroidJUnit4::class)
class MenuItemProvidersFactoryTest {
    private val scope = CoroutineScope(StandardTestDispatcher())

    @After
    fun tearDown() {
        scope.cancel()
    }

    @Test
    fun `WHEN building the provider of a menu item THEN get the right instance`() {
        val factory = createFactory()

        allMenuItems().forEach { item ->
            assertEquals(expectedProviders.getValue(item::class), factory.buildProviderFor(item)::class, "$item")
        }
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `WHEN building the home menu THEN show all its items and handle each of their events exactly once`() {
        val settings: Settings =
            mockk(relaxed = true) {
                every { shouldShowMenuBanner } returns true
                every { isDefaultBrowser } returns false
                every { isAutofillSupported } returns true
                every { shouldDeleteBrowsingDataOnQuit } returns true
            }
        val eligibleState = IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible)
        val factory =
            createFactory(
                accessPoint = MenuAccessPoint.Home,
                target = MenuTarget.Home,
                ipProtectionStore = IPProtectionStore(eligibleState),
                settings = settings,
            )
        val registry =
            MenuItemsRegistry(configuration = HomeMenuConfiguration.sections(), resolver = factory::buildProviderFor)

        val shown = registry.providers.filterValues { it.itemsFlow.value.isNotEmpty() }.keys
        assertEquals(
            listOf(
                FenixMenuItem.DefaultBrowserBanner,
                FenixMenuItem.IPProtection,
                FenixMenuItem.Extensions,
                FenixMenuItem.History,
                FenixMenuItem.Bookmarks,
                FenixMenuItem.Downloads,
                FenixMenuItem.Passwords,
                FenixMenuItem.MozillaAccount,
                FenixMenuItem.CustomizeHomepage,
                FenixMenuItem.Settings,
                FenixMenuItem.Quit,
            ),
            shown.toList(),
        )
        registry.providers.values
            .flatMap { it.itemsFlow.value }
            .flatMap { it.reachableEvents() }
            .forEach { event ->
                assertEquals(1, registry.providers.values.count { it.handles(event) }, "Handlers of $event")
            }
    }

    private fun allMenuItems(): List<FenixMenuItem> =
        FenixMenuItem::class.sealedSubclasses.map { type ->
            type.objectInstance
                ?: when (type) {
                    FenixMenuItem.More::class -> FenixMenuItem.More(subMenuItems = emptyList())
                    else -> error("Unknown menu item type $type")
                }
        }

    private fun createFactory(
        accessPoint: MenuAccessPoint = MenuAccessPoint.Browser,
        target: MenuTarget = MenuTarget.BrowserTab,
        ipProtectionStore: IPProtectionStore = IPProtectionStore(),
        settings: Settings = mockk(relaxed = true),
    ) =
        MenuItemProvidersFactory(
            context = testContext,
            accessPoint = accessPoint,
            target = target,
            browserStore = BrowserStore(),
            appStore = AppStore(),
            ipProtectionStore = ipProtectionStore,
            syncStore = SyncStore(),
            httpClient = mockk(relaxed = true),
            historyStorage = mockk(relaxed = true),
            bookmarksStorage = mockk(relaxed = true),
            pinnedSiteStorage = mockk(relaxed = true),
            tabCollectionStorage = mockk(relaxed = true),
            permissionStorage = mockk(relaxed = true),
            useCases = mockk(relaxed = true),
            addonManager = mockk(relaxed = true),
            engine = mockk(relaxed = true),
            settings = settings,
            summarizeFeatureSettings = mockk(relaxed = true),
            summarizeEligibilityChecker = mockk(relaxed = true),
            translationsSettings = mockk(relaxed = true),
            isAndroidAutomotiveAvailable = false,
            materialAlertDialogBuilder = mockk(relaxed = true),
            quitApplicationDelegate = {},
            setAsDefaultBrowserDelegate = {},
            menuViewScope = scope,
            applicationScope = scope,
        )

    private val expectedProviders: Map<KClass<out FenixMenuItem>, KClass<out MenuItemProvider>> =
        mapOf(
            FenixMenuItem.DefaultBrowserBanner::class to DefaultBrowserMenuItemProvider::class,
            FenixMenuItem.CustomizeReaderView::class to ReaderViewMenuItemProvider::class,
            FenixMenuItem.IPProtection::class to VpnMenuItemProvider::class,
            FenixMenuItem.Bookmark::class to BookmarkMenuItemProvider::class,
            FenixMenuItem.Extensions::class to ExtensionsMenuItemProvider::class,
            FenixMenuItem.FindInPage::class to FindInPageMenuItemProvider::class,
            FenixMenuItem.DesktopSite::class to DesktopSiteMenuItemProvider::class,
            FenixMenuItem.More::class to MoreMenuItemsProvider::class,
            FenixMenuItem.Translate::class to TranslationsMenuItemProvider::class,
            FenixMenuItem.SummarizePage::class to SummarizePageMenuItemProvider::class,
            FenixMenuItem.MoveToNormalTabs::class to MoveToNormalTabsMenuItemProvider::class,
            FenixMenuItem.ReportBrokenSite::class to ReportBrokenSiteMenuItemProvider::class,
            FenixMenuItem.Shortcut::class to ShortcutMenuItemProvider::class,
            FenixMenuItem.AddToHomeScreen::class to AddToHomeScreenMenuItemProvider::class,
            FenixMenuItem.SaveToCollection::class to SaveToCollectionMenuItemProvider::class,
            FenixMenuItem.OpenInApp::class to OpenInAppMenuItemProvider::class,
            FenixMenuItem.OpenInFirefox::class to OpenInFirefoxMenuItemProvider::class,
            FenixMenuItem.CustomTabAppItems::class to CustomTabAppMenuItemProvider::class,
            FenixMenuItem.SaveAsPdf::class to SaveAsPdfMenuItemProvider::class,
            FenixMenuItem.Print::class to PrintMenuItemProvider::class,
            FenixMenuItem.MozillaAccount::class to MozillaAccountMenuItemProvider::class,
            FenixMenuItem.CustomizeHomepage::class to CustomizeHomepageMenuItemProvider::class,
            FenixMenuItem.Settings::class to SettingsMenuItemProvider::class,
            FenixMenuItem.Quit::class to QuitMenuItemProvider::class,
            FenixMenuItem.Back::class to BackMenuItemProvider::class,
            FenixMenuItem.History::class to HistoryMenuItemProvider::class,
            FenixMenuItem.Bookmarks::class to BookmarksScreenMenuItemProvider::class,
            FenixMenuItem.Downloads::class to DownloadsMenuItemProvider::class,
            FenixMenuItem.Passwords::class to PasswordsMenuItemProvider::class,
            FenixMenuItem.Forward::class to ForwardMenuItemProvider::class,
            FenixMenuItem.Share::class to ShareMenuItemProvider::class,
            FenixMenuItem.Refresh::class to RefreshMenuItemProvider::class,
        )
}
