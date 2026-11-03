/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.content.Context
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlin.LazyThreadSafetyMode.NONE
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.service.fxa.store.SyncStore
import org.mozilla.fenix.R
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
import org.mozilla.fenix.components.PermissionStorage
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.accounts.MozillaAccountMenuItemProvider
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.customtabs.CustomTabAppMenuItemProvider
import org.mozilla.fenix.downloads.DownloadsMenuItemProvider
import org.mozilla.fenix.home.CustomizeHomepageMenuItemProvider
import org.mozilla.fenix.home.topsites.ShortcutMenuItemProvider
import org.mozilla.fenix.ipprotection.VpnMenuItemProvider
import org.mozilla.fenix.library.history.HistoryMenuItemProvider
import org.mozilla.fenix.pdf.SaveAsPdfMenuItemProvider
import org.mozilla.fenix.print.PrintMenuItemProvider
import org.mozilla.fenix.settings.SettingsMenuItemProvider
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DataStorage
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DeleteDataUseCases
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.Stores
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataController
import org.mozilla.fenix.settings.deletebrowsingdata.QuitMenuItemProvider
import org.mozilla.fenix.settings.logins.PasswordsMenuItemProvider
import org.mozilla.fenix.shortcut.AddToHomeScreenMenuItemProvider
import org.mozilla.fenix.summarization.SummarizePageMenuItemProvider
import org.mozilla.fenix.summarization.eligibility.SummarizationEligibilityChecker
import org.mozilla.fenix.summarization.onboarding.FenixSummarizationFeatureConfiguration
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.translations.TranslationsMenuItemProvider
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.webcompat.DefaultWebCompatReporterMoreInfoSender
import org.mozilla.fenix.webcompat.ReportBrokenSiteMenuItemProvider
import org.mozilla.fenix.webcompat.middleware.DefaultWebCompatReporterRetrievalService

/** Builds the providers for all menu items shown in the browser menu. */
@Suppress("LongParameterList")
class MenuItemProvidersFactory(
    private val context: Context,
    private val accessPoint: MenuAccessPoint,
    private val target: MenuTarget,
    private val browserStore: BrowserStore,
    private val appStore: AppStore,
    private val ipProtectionStore: IPProtectionStore,
    private val syncStore: SyncStore,
    private val httpClient: Client,
    private val historyStorage: PlacesHistoryStorage,
    private val bookmarksStorage: BookmarksStorage,
    private val pinnedSiteStorage: PinnedSiteStorage,
    private val tabCollectionStorage: TabCollectionStorage,
    private val permissionStorage: PermissionStorage,
    private val useCases: UseCases,
    private val addonManager: AddonManager,
    private val engine: Engine,
    private val settings: Settings,
    private val summarizeFeatureSettings: FenixSummarizationFeatureConfiguration,
    private val summarizeEligibilityChecker: SummarizationEligibilityChecker,
    private val translationsSettings: TranslationsEnabledSettings,
    private val isAndroidAutomotiveAvailable: Boolean,
    private val materialAlertDialogBuilder: MaterialAlertDialogBuilder,
    private val quitApplicationDelegate: () -> Unit,
    private val setAsDefaultBrowserDelegate: () -> Unit,
    private val menuViewScope: CoroutineScope,
    private val applicationScope: CoroutineScope,
) {

    /** Build a [MenuItemProvider] for the given [FenixMenuItem]. */
    @Suppress("LongMethod", "CyclomaticComplexMethod")
    fun buildProviderFor(item: FenixMenuItem): MenuItemProvider =
        when (item) {
            FenixMenuItem.DefaultBrowserBanner ->
                DefaultBrowserMenuItemProvider(
                    settings = settings,
                    appName = context.getString(R.string.app_name),
                    setAsDefaultBrowser = setAsDefaultBrowserDelegate,
                )

            FenixMenuItem.CustomizeReaderView ->
                ReaderViewMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    appStore = appStore,
                    scope = menuViewScope,
                )

            FenixMenuItem.IPProtection ->
                VpnMenuItemProvider(
                    ipProtectionStore = ipProtectionStore,
                    target = target,
                    scope = menuViewScope,
                )

            FenixMenuItem.Bookmark ->
                BookmarkMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    bookmarksStorage = bookmarksStorage,
                    addBookmark = useCases.bookmarksUseCases.addBookmark,
                    appStore = appStore,
                    scope = menuViewScope,
                    applicationScope = applicationScope,
                )

            FenixMenuItem.FindInPage ->
                FindInPageMenuItemProvider(
                    appStore = appStore,
                    target = target,
                )

            FenixMenuItem.DesktopSite ->
                DesktopSiteMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    requestDesktopSite = useCases.sessionUseCases.requestDesktopSite,
                    scope = menuViewScope,
                )
            FenixMenuItem.Extensions ->
                ExtensionsMenuItemProvider(
                    context = context.applicationContext,
                    browserStore = browserStore,
                    target = target,
                    addonManager = addonManager,
                    viewLifecycleScope = menuViewScope,
                    applicationScope = applicationScope,
                    appStore = appStore,
                    fenixBrowserUseCases = useCases.fenixBrowserUseCases,
                )
            is FenixMenuItem.More ->
                MoreMenuItemsProvider(
                    browserStore = browserStore,
                    target = target,
                    summarizationSettings = summarizeFeatureSettings,
                    scope = menuViewScope,
                )

            FenixMenuItem.Translate ->
                TranslationsMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    translationsSettings = translationsSettings,
                    scope = menuViewScope,
                )

            FenixMenuItem.SummarizePage ->
                SummarizePageMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    summarizationSettings = summarizeFeatureSettings,
                    eligibilityChecker = summarizeEligibilityChecker,
                    scope = menuViewScope,
                )

            FenixMenuItem.Back ->
                BackMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    goBack = useCases.sessionUseCases.goBack,
                    settings = settings,
                    scope = menuViewScope,
                )

            FenixMenuItem.Forward ->
                ForwardMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    goForward = useCases.sessionUseCases.goForward,
                    scope = menuViewScope,
                )

            FenixMenuItem.Share ->
                ShareMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    shareUseCases = useCases.shareUseCases,
                )

            FenixMenuItem.Refresh ->
                RefreshMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    reload = useCases.sessionUseCases.reload,
                    stopLoading = useCases.sessionUseCases.stopLoading,
                    scope = menuViewScope,
                )

            FenixMenuItem.MoveToNormalTabs ->
                MoveToNormalTabsMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    migratePrivateTab = useCases.tabsUseCases.migratePrivateTabUseCase,
                )

            FenixMenuItem.ReportBrokenSite ->
                ReportBrokenSiteMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    settings = settings,
                    webCompatReporterMoreInfoSender =
                        DefaultWebCompatReporterMoreInfoSender(DefaultWebCompatReporterRetrievalService(browserStore)),
                    appStore = appStore,
                    fenixBrowserUseCases = useCases.fenixBrowserUseCases,
                    scope = menuViewScope,
                )

            FenixMenuItem.Shortcut ->
                ShortcutMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    pinnedSiteStorage = pinnedSiteStorage,
                    areShortcutsEnabled = settings.showTopSitesFeature,
                    topSitesUseCases = useCases.topSitesUseCase,
                    appStore = appStore,
                    settings = settings,
                    materialAlertDialogBuilder = materialAlertDialogBuilder,
                    scope = menuViewScope,
                    applicationScope = applicationScope,
                )

            FenixMenuItem.AddToHomeScreen ->
                AddToHomeScreenMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    webAppUseCases = useCases.webAppUseCases,
                    settings = settings,
                    scope = menuViewScope,
                )

            FenixMenuItem.SaveToCollection ->
                SaveToCollectionMenuItemProvider(
                    settings = settings,
                    tabCollectionStorage = tabCollectionStorage,
                    browserStore = browserStore,
                    target = target,
                )

            FenixMenuItem.OpenInApp ->
                OpenInAppMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    appStore = appStore,
                    appLinksUseCases = useCases.appLinksUseCases,
                    settings = settings,
                    scope = menuViewScope,
                )

            FenixMenuItem.OpenInFirefox ->
                OpenInFirefoxMenuItemProvider(
                    appName = context.getString(R.string.app_name),
                    appStore = appStore,
                    target = target,
                )

            FenixMenuItem.CustomTabAppItems ->
                CustomTabAppMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    context = context,
                )

            FenixMenuItem.SaveAsPdf ->
                SaveAsPdfMenuItemProvider(
                    browserStore = browserStore,
                    target = target,
                    saveToPdf = useCases.sessionUseCases.saveToPdf,
                )

            FenixMenuItem.Print ->
                PrintMenuItemProvider(
                    isAndroidAutomotiveAvailable = isAndroidAutomotiveAvailable,
                    browserStore = browserStore,
                    target = target,
                    printContent = useCases.sessionUseCases.printContent,
                )

            FenixMenuItem.History -> HistoryMenuItemProvider(target = target)

            FenixMenuItem.Bookmarks -> BookmarksScreenMenuItemProvider(target = target)

            FenixMenuItem.Downloads ->
                DownloadsMenuItemProvider(
                    appStore = appStore,
                    target = target,
                    scope = menuViewScope,
                )

            FenixMenuItem.Passwords ->
                PasswordsMenuItemProvider(
                    isAutofillSupported = settings.isAutofillSupported,
                    target = target,
                )

            FenixMenuItem.MozillaAccount ->
                MozillaAccountMenuItemProvider(
                    syncStore = syncStore,
                    httpClient = httpClient,
                    context = context,
                    accessPoint = accessPoint,
                    target = target,
                    scope = menuViewScope,
                )

            FenixMenuItem.CustomizeHomepage -> CustomizeHomepageMenuItemProvider(target = target)

            FenixMenuItem.Settings -> SettingsMenuItemProvider(target = target)

            FenixMenuItem.Quit ->
                QuitMenuItemProvider(
                    appName = context.getString(R.string.app_name),
                    deletesBrowsingDataOnQuit = settings.shouldDeleteBrowsingDataOnQuit,
                    deleteBrowsingDataController = { deleteBrowsingDataController },
                    quitApplicationDelegate = quitApplicationDelegate,
                    applicationScope = applicationScope,
                    target = target,
                )
        }

    // Only ever needed by users who asked for their data to be deleted when they quit, and only once they do.
    private val deleteBrowsingDataController: DeleteBrowsingDataController by
        lazy(NONE) {
            DefaultDeleteBrowsingDataController(
                deleteDataUseCases =
                    DeleteDataUseCases(
                        removeAllTabs = useCases.tabsUseCases.removeAllTabs,
                        removeAllDownloads = useCases.downloadUseCases.removeAllDownloads,
                    ),
                dataStorage =
                    DataStorage(
                        history = historyStorage,
                        permissions = permissionStorage,
                    ),
                stores = Stores(appStore = appStore, browserStore = browserStore),
                engine = engine,
                settings = settings,
            )
        }
}
