/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import android.os.StrictMode
import android.util.Size
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.concept.storage.HistoryStorage
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.contextmenu.ContextMenuUseCases
import mozilla.components.feature.downloads.DownloadsUseCases
import mozilla.components.feature.pwa.WebAppShortcutManager
import mozilla.components.feature.pwa.WebAppUseCases
import mozilla.components.feature.search.SearchUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.SettingsUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.syncedtabs.commands.SyncedTabsCommands
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.top.sites.TopSitesStorage
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.service.mars.MozAdsClientProvider
import mozilla.components.service.mars.MozAdsUseCases
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.locale.LocaleUseCases
import mozilla.components.support.utils.DefaultDownloadFileUtils
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.bookmarks.LastSavedFolderCache
import org.mozilla.fenix.components.share.DefaultShareSheetLauncher
import org.mozilla.fenix.components.share.ShareSheetLauncher
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.home.mars.MARSUseCases
import org.mozilla.fenix.pbmlock.PrivateBrowsingLockUseCases
import org.mozilla.fenix.perf.StrictModeManager
import org.mozilla.fenix.settings.downloads.DownloadLocationManager
import org.mozilla.fenix.wallpapers.WallpapersUseCases

/**
 * Component group for all use cases. Use cases are provided by feature modules and can be triggered by UI interactions.
 */
@Suppress("LongParameterList")
class UseCases(
    private val context: Context,
    private val crashReporter: Lazy<CrashReporter>,
    private val engine: Lazy<Engine>,
    private val store: Lazy<BrowserStore>,
    private val shortcutManager: Lazy<WebAppShortcutManager>,
    private val topSitesStorage: Lazy<TopSitesStorage>,
    private val bookmarksStorage: Lazy<BookmarksStorage>,
    private val historyStorage: Lazy<HistoryStorage>,
    private val lastSavedFolderCache: Lazy<LastSavedFolderCache>,
    private val syncedTabsCommands: Lazy<SyncedTabsCommands>,
    adsClientProvider: Lazy<MozAdsClientProvider>,
    appStore: Lazy<AppStore>,
    client: Lazy<Client>,
    strictMode: Lazy<StrictModeManager>,
) {
    /** Use cases that provide engine interactions for a given browser session. */
    val sessionUseCases by lazy { SessionUseCases(store.value) }

    /** Use cases that provide tab management. */
    val tabsUseCases: TabsUseCases by lazy { TabsUseCases(store.value) }

    /** Use cases for managing custom tabs. */
    val customTabsUseCases: CustomTabsUseCases by lazy {
        CustomTabsUseCases(store.value, sessionUseCases.loadUrl)
    }

    /** Use cases that provide search engine integration. */
    val searchUseCases by lazy {
        SearchUseCases(
            store.value,
            tabsUseCases,
            sessionUseCases,
        )
    }

    /** Use cases that provide settings management. */
    val settingsUseCases by lazy { SettingsUseCases(engine.value, store.value) }

    val appLinksUseCases by lazy { AppLinksUseCases(context.applicationContext) }

    val webAppUseCases by lazy {
        WebAppUseCases(context, store.value, shortcutManager.value)
    }

    val downloadUseCases by lazy {
        DownloadsUseCases(
            store = store.value,
            downloadFileUtils =
                DefaultDownloadFileUtils(
                    context = context.applicationContext,
                    downloadLocation = {
                        DownloadLocationManager(context.components.settings, context.contentResolver).defaultLocation
                    },
                ),
        )
    }

    val contextMenuUseCases by lazy { ContextMenuUseCases(store.value) }

    val trackingProtectionUseCases by lazy { TrackingProtectionUseCases(store.value, engine.value) }

    /** Use cases that provide top sites management. */
    val topSitesUseCase by lazy { TopSitesUseCases(topSitesStorage.value) }

    /** Use cases that handle locale management. */
    val localeUseCases by lazy { LocaleUseCases(store.value) }

    /** Use cases that provide bookmark management. */
    val bookmarksUseCases by lazy {
        BookmarksUseCase(bookmarksStorage.value, historyStorage.value, lastSavedFolderCache.value)
    }

    val wallpaperUseCases by lazy {
        // Required to even access context.filesDir property and to retrieve current locale
        val (rootStorageDirectory, currentLocale) =
            strictMode.value.allowViolation(StrictMode::allowThreadDiskReads) {
                val rootStorageDirectory = context.filesDir
                val currentLocale =
                    LocaleManager.getCurrentLocale(context)?.toLanguageTag()
                        ?: LocaleManager.getSystemDefault().toLanguageTag()
                rootStorageDirectory to currentLocale
            }
        WallpapersUseCases(
            context.components.settings,
            rootStorageDirectory,
            appStore.value,
            client.value,
            rootStorageDirectory,
            currentLocale,
            getDisplaySize = { displaySize(context) },
        )
    }

    val closeSyncedTabsUseCases by lazy { CloseTabsUseCases(syncedTabsCommands.value) }

    val marsUseCases by lazy { MARSUseCases(client.value) }

    val mozAdsUseCases by lazy {
        MozAdsUseCases(
            adsClientProvider = adsClientProvider,
            crashReporter = crashReporter.value,
        )
    }

    val fenixBrowserUseCases by lazy {
        FenixBrowserUseCases(
            appStore = appStore.value,
            tabsUseCases = tabsUseCases,
            loadUrlUseCase = sessionUseCases.loadUrl,
            searchUseCases = searchUseCases,
            homepageTitle = context.getString(R.string.tab_tray_homepage_tab),
            profiler = engine.value.profiler,
        )
    }

    val shareSheetLauncher: ShareSheetLauncher by lazy {
        DefaultShareSheetLauncher(
            applicationContext = context.applicationContext,
            homeActivityClass = HomeActivity::class.java,
            crashReporter = crashReporter.value,
        )
    }

    /** Use cases for sharing content via the system share sheet or the in-app share fragment. */
    val shareUseCases by lazy {
        ShareUseCases(
            browserStore = store.value,
            shareSheetLauncher = shareSheetLauncher,
            settings = context.components.settings,
        )
    }

    val privateBrowsingLockUseCases by lazy {
        PrivateBrowsingLockUseCases(appStore.value)
    }

    companion object {
        /** The size, in pixels, of the display a full screen wallpaper has to cover. */
        @VisibleForTesting
        internal fun displaySize(context: Context): Size {
            val metrics = context.resources.displayMetrics
            return Size(metrics.widthPixels, metrics.heightPixels)
        }
    }
}
