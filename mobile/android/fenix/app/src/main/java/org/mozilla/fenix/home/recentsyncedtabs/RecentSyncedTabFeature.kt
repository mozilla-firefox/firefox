/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recentsyncedtabs

import android.content.Context
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.withContext
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.concept.storage.HistoryStorage
import mozilla.components.concept.sync.Device
import mozilla.components.concept.sync.DeviceType
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.feature.syncedtabs.storage.SyncedTabsStorage
import mozilla.components.lib.state.ext.flow
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.service.fxa.manager.ext.withConstellationIfExists
import mozilla.components.service.fxa.store.SyncStatus
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.service.fxa.sync.SyncReason
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.telemetry.glean.GleanTimerId
import org.mozilla.fenix.GleanMetrics.RecentSyncedTabs
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction

/**
 * Delegate to handle layout updates and dispatch actions related to the recent synced tab.
 *
 * @param context An Android [Context].
 * @param appStore Store to dispatch actions to when synced tabs are updated or errors encountered.
 * @param syncStore Store to observe for changes to Sync and account status.
 * @param storage Storage layer for synced tabs.
 * @param accountManager Account manager to initiate Syncs and refresh devices.
 * @param historyStorage Storage for searching history for preview image URLs matching synced tab.
 * @param coroutineScope The scope to collect Sync state Flow updates in.
 * @param ioDispatcher The dispatcher to be used for background IO work.
 * @param currentTimeMillis provider for the current time in milliseconds, injectable for testing.
 */
class RecentSyncedTabFeature(
    private val context: Context,
    private val appStore: AppStore,
    private val syncStore: SyncStore,
    private val storage: SyncedTabsStorage,
    private val accountManager: FxaAccountManager,
    private val historyStorage: HistoryStorage,
    private val coroutineScope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
) : LifecycleAwareFeature {

    private var syncStartId: GleanTimerId? = null
    private var lastSyncedTabs: List<RecentSyncedTab>? = null

    override fun start() {
        collectAccountUpdates()
        collectStatusUpdates()
    }

    override fun stop() = Unit

    private fun collectAccountUpdates() {
        syncStore
            .flow()
            .distinctUntilChangedBy { state ->
                state.account != null
            }
            .onEach { state ->
                if (state.account != null) {
                    dispatchLoading()
                    // Sync tabs storage will fail to retrieve tabs aren't refreshed, as that action
                    // is what populates the device constellation state
                    accountManager.withConstellationIfExists { refreshDevices() }
                    accountManager.syncNow(
                        reason = SyncReason.User,
                        debounce = true,
                        customEngineSubset = listOf(SyncEngine.Tabs),
                    )
                }
            }
            .launchIn(coroutineScope)
    }

    private fun collectStatusUpdates() {
        syncStore
            .flow()
            .distinctUntilChangedBy { state ->
                state.status
            }
            .onEach { state ->
                when (state.status) {
                    SyncStatus.Idle -> dispatchSyncedTabs()
                    SyncStatus.Error -> onError()
                    SyncStatus.LoggedOut ->
                        appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.None))
                    else -> Unit
                }
            }
            .launchIn(coroutineScope)
    }

    private fun dispatchLoading() {
        syncStartId?.let { RecentSyncedTabs.recentSyncedTabTimeToLoad.cancel(it) }
        syncStartId = RecentSyncedTabs.recentSyncedTabTimeToLoad.start()
        if (appStore.state.recentSyncedTabState == RecentSyncedTabState.None) {
            appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.Loading))
        }
    }

    private suspend fun dispatchSyncedTabs() {
        if (!isSyncedTabsEngineEnabled()) {
            appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.None))

            return
        }

        val syncedTabs =
            storage
                .getSyncedDeviceTabs()
                .filterNot { it.device.isCurrentDevice || it.tabs.isEmpty() }
                .flatMap {
                    it.tabs.map { tab ->
                        SyncedDeviceTab(it.device, tab)
                    }
                }
                .ifEmpty {
                    return
                }
                // We want to get the last device used based on the most recent accessed tab,
                // as described here: https://github.com/mozilla-mobile/fenix/issues/26398
                .sortedByDescending { deviceTab -> deviceTab.tab.lastUsed }
                .take(MAX_RECENT_SYNCED_TABS)
                .map { deviceTab ->
                    val activeTabEntry = deviceTab.tab.active()

                    val currentTime = currentTimeMillis()
                    val maxAgeInMs = TimeUnit.DAYS.toMillis(DAYS_HISTORY_FOR_PREVIEW_IMAGE)
                    val history =
                        historyStorage.getDetailedVisits(
                            start = currentTime - maxAgeInMs,
                            end = currentTime,
                        )

                    // Searching history entries for any that share a top level domain and have a
                    // preview image URL available casts a wider net for finding a suitable image.
                    val previewImageUrl =
                        history
                            .find { entry ->
                                entry.url.contains(activeTabEntry.url.tryGetHostFromUrl()) &&
                                    entry.previewImageUrl != null
                            }
                            ?.previewImageUrl

                    RecentSyncedTab(
                        deviceDisplayName = deviceTab.device.displayName,
                        deviceType = deviceTab.device.deviceType,
                        title = activeTabEntry.title,
                        url = activeTabEntry.url,
                        previewImageUrl = previewImageUrl,
                    )
                }
        if (syncedTabs.isEmpty()) {
            appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.None))
        } else {
            recordMetrics(syncedTabs.first(), lastSyncedTabs?.first())
            appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.Success(syncedTabs)))
            lastSyncedTabs = syncedTabs
        }
    }

    private fun onError() {
        if (appStore.state.recentSyncedTabState == RecentSyncedTabState.Loading) {
            appStore.dispatch(AppAction.RecentSyncedTabStateChange(RecentSyncedTabState.None))
        }
    }

    private fun recordMetrics(
        tab: RecentSyncedTab,
        lastSyncedTab: RecentSyncedTab?,
    ) {
        RecentSyncedTabs.recentSyncedTabShown[tab.deviceType.name.lowercase()].add()
        syncStartId?.let {
            RecentSyncedTabs.recentSyncedTabTimeToLoad.stopAndAccumulate(it)
            syncStartId = null
        }
        if (tab == lastSyncedTab) {
            RecentSyncedTabs.latestSyncedTabIsStale.add()
        }
    }

    private suspend fun isSyncedTabsEngineEnabled(): Boolean =
        withContext(ioDispatcher) {
            SyncEnginesStorage(context).getStatus()[SyncEngine.Tabs] ?: true
        }

    companion object {
        /** The number of days to search history for a preview image URL to display for a synced tab. */
        const val DAYS_HISTORY_FOR_PREVIEW_IMAGE = 3L

        /** Number of recent synced tabs we want to keep in the success state. */
        const val MAX_RECENT_SYNCED_TABS = 8
    }
}

/** The state of the recent synced tab. */
sealed class RecentSyncedTabState {
    /** There is no synced tab, or a user is not authenticated. */
    object None : RecentSyncedTabState()

    /** A user is authenticated and the sync is running. */
    object Loading : RecentSyncedTabState()

    /** A user is authenticated and most recent synced tabs have been found. */
    data class Success(val tabs: List<RecentSyncedTab>) : RecentSyncedTabState()
}

/**
 * A tab that was recently viewed on a synced device.
 *
 * @property deviceDisplayName The device the tab was viewed on.
 * @property deviceType The type of a device the tab was viewed on - mobile, desktop.
 * @property title The title of the tab.
 * @property url The url of the tab.
 * @property previewImageUrl The url used to retrieve the preview image of the tab.
 */
data class RecentSyncedTab(
    val deviceDisplayName: String,
    val deviceType: DeviceType,
    val title: String,
    val url: String,
    val previewImageUrl: String?,
)

/**
 * Class representing a tab from a synced device.
 *
 * @property device The synced [Device].
 * @property tab The tab from the synced device.
 */
private data class SyncedDeviceTab(
    val device: Device,
    val tab: Tab,
)
