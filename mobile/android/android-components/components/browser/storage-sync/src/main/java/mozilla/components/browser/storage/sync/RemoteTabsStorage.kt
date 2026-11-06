/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.storage.sync

import android.content.Context
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.withContext
import mozilla.appservices.remotetabs.PendingCommand
import mozilla.appservices.remotetabs.RemoteCommand
import mozilla.appservices.remotetabs.RemoteTab
import mozilla.appservices.remotetabs.TabsApiException as RemoteTabProviderException
import mozilla.appservices.remotetabs.TabsStore as RemoteTabsProvider
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.storage.Storage
import mozilla.components.concept.sync.Device
import mozilla.components.concept.sync.DeviceCommandOutgoing
import mozilla.components.concept.sync.DeviceCommandQueue
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry
import mozilla.components.support.ktx.kotlin.toWellFormed
import mozilla.components.support.utils.logElapsedTime

private const val TABS_DB_NAME = "tabs.sqlite"

/** An interface which defines read/write methods for remote tabs data. */
open class RemoteTabsStorage(
    private val context: Context,
    private val crashReporter: CrashReporting,
) : Storage, SyncableStore {
    internal val api by lazy { RemoteTabsProvider(File(context.filesDir, TABS_DB_NAME).canonicalPath) }
    private val scope by lazy { CoroutineScope(Dispatchers.IO) }
    internal val logger = Logger("RemoteTabsStorage")

    override suspend fun warmUp() {
        logElapsedTime(logger, "Warming up storage") { api }
    }

    /**
     * Store the locally opened tabs.
     *
     * @param tabs The list of opened tabs, for all opened non-private windows, on this device.
     */
    suspend fun store(tabs: List<Tab>) {
        return withContext(scope.coroutineContext) {
            try {
                api.setLocalTabs(
                    tabs.map {
                        val activeTab = it.active()
                        val urlHistory = listOf(activeTab.url) + it.previous().reversed().map { it.url }
                        RemoteTab(
                            activeTab.title.toWellFormed(),
                            urlHistory,
                            activeTab.iconUrl,
                            it.lastUsed,
                            it.inactive,
                        )
                    }
                )
                logger.info("Told the tabs store we have ${tabs.size}")
            } catch (e: RemoteTabProviderException) {
                logger.error("Failed to tell the tabs store about our tabs", e)
                crashReporter.submitCaughtException(e)
            }
        }
    }

    /**
     * Get all remote devices tabs.
     *
     * @return A mapping of opened tabs per device.
     */
    suspend fun getAll(): Map<SyncClient, List<Tab>> {
        return withContext(scope.coroutineContext) {
            try {
                api.getAll()
                    .map { device ->
                        val tabs =
                            device.remoteTabs.map { tab ->
                                // Map RemoteTab to TabEntry
                                val title = tab.title
                                val icon = tab.icon
                                val lastUsed = tab.lastUsed
                                val history = tab.urlHistory.reversed().map { url -> TabEntry(title, url, icon) }
                                Tab(history, tab.urlHistory.lastIndex, lastUsed, tab.inactive)
                            }
                        // Map device to tabs
                        SyncClient(device.clientId) to tabs
                    }
                    .toMap()
            } catch (e: RemoteTabProviderException) {
                crashReporter.submitCaughtException(e)
                return@withContext emptyMap()
            }
        }
    }

    override suspend fun runMaintenance(dbSizeLimit: UInt) {
        // Storage maintenance workflow for remote tabs is not implemented yet.
    }

    override fun cleanup() {
        scope.coroutineContext.cancelChildren()
    }

    override fun registerWithSyncManager() {
        return api.registerWithSyncManager()
    }
}

/**
 * A command queue for managing synced tabs on other devices.
 *
 * @property storage Persistent storage for the queued commands.
 * @property closeTabsCommandSender A function that sends a queued "close tabs" command.
 */
class RemoteTabsCommandQueue(
    internal val storage: RemoteTabsStorage,
    internal val closeTabsCommandSender: CommandSender<DeviceCommandOutgoing.CloseTab, SendCloseTabsResult>,
) :
    DeviceCommandQueue<DeviceCommandQueue.Type.RemoteTabs>,
    Observable<DeviceCommandQueue.Observer> by ObserverRegistry() {

    // This queue is backed by `appservices.remotetabs.RemoteCommandStore`,
    // but the actual commands are eventually sent via
    // `appservices.fxaclient.FxaClient`.

    // The `appservices.remotetabs` and `appservices.fxaclient` packages use
    // different types to represent the same commands, but we want to
    // smooth over this difference for consumers, so we parameterize
    // the queue over `concept.sync.DeviceCommandQueue.Type.RemoteTabs`, and
    // map those to the `appservices.remotetab` types below.

    internal val api by lazy { storage.api.newRemoteCommandStore() }
    private val scope by lazy { CoroutineScope(Dispatchers.IO) }

    override suspend fun add(deviceId: String, command: DeviceCommandQueue.Type.RemoteTabs) =
        withContext(scope.coroutineContext) {
            when (command) {
                is DeviceCommandOutgoing.CloseTab -> {
                    command.urls.forEach {
                        api.addRemoteCommand(deviceId, RemoteCommand.CloseTab(it))
                    }
                    notifyObservers { onAdded() }
                }
            }
        }

    override suspend fun remove(deviceId: String, command: DeviceCommandQueue.Type.RemoteTabs) =
        withContext(scope.coroutineContext) {
            when (command) {
                is DeviceCommandOutgoing.CloseTab -> {
                    command.urls.forEach {
                        api.removeRemoteCommand(deviceId, RemoteCommand.CloseTab(it))
                    }
                    notifyObservers { onRemoved() }
                }
            }
        }

    @Suppress("CognitiveComplexMethod")
    override suspend fun flush(): DeviceCommandQueue.FlushResult =
        withContext(scope.coroutineContext) {
            api.getUnsentCommands()
                .groupBy {
                    when (it.command) {
                        is RemoteCommand.CloseTab -> PendingCommandGroup.Key.CloseTab(it.deviceId)
                    // Add `is ... ->` branches for future pending commands here...
                    }.asAnyKey
                }
                .map { (key, pendingCommands) ->
                    when (key) {
                        is PendingCommandGroup.Key.CloseTab -> {
                            // We want to limit the number of outgoing commands that we send to FxA,
                            // because (1) the FxA server imposes a request rate limit, and
                            // (2) we don't want to inundate target devices with notifications.
                            // Grouping all `appservices.remotetabs.RemoteCommand.CloseTab` pending
                            // commands into one `concept.sync.DeviceCommandOutgoing.CloseTab`
                            // outgoing command lets us reduce the number of outgoing commands we send.
                            PendingCommandGroup(
                                deviceId = key.deviceId,
                                command =
                                    DeviceCommandOutgoing.CloseTab(
                                        pendingCommands.map {
                                            val providerCommand = it.command as RemoteCommand.CloseTab
                                            providerCommand.url
                                        }
                                    ),
                                pendingCommands = pendingCommands,
                            )
                        }
                    // Add `is ... ->` branches for future pending command grouping keys here...
                    }.asAnyGroup
                }
                .mapNotNull { group ->
                    when (group.command) {
                        is DeviceCommandOutgoing.CloseTab ->
                            async {
                                when (val result = closeTabsCommandSender.send(group.deviceId, group.command)) {
                                    is SendCloseTabsResult.Ok -> {
                                        for (pendingCommand in group.pendingCommands) {
                                            api.setPendingCommandSent(pendingCommand)
                                        }
                                        DeviceCommandQueue.FlushResult.ok()
                                    }
                                    is SendCloseTabsResult.RetryFor -> {
                                        val urlsToRetry = result.urls.toSet()
                                        for (pendingCommand in group.pendingCommands) {
                                            val providerCommand = pendingCommand.command as RemoteCommand.CloseTab
                                            val wasPendingCommandSent = !urlsToRetry.contains(providerCommand.url)
                                            if (wasPendingCommandSent) {
                                                api.setPendingCommandSent(pendingCommand)
                                            }
                                        }
                                        DeviceCommandQueue.FlushResult.retry()
                                    }
                                    // If the user isn't signed in, or the
                                    // target device isn't there, retrying without
                                    // user intervention won't help. Keep the pending
                                    // command in the queue, but return `Ok` so that
                                    // the flush isn't rescheduled.
                                    is SendCloseTabsResult.NoAccount -> DeviceCommandQueue.FlushResult.ok()
                                    is SendCloseTabsResult.NoDevice -> DeviceCommandQueue.FlushResult.ok()
                                    is SendCloseTabsResult.Error -> DeviceCommandQueue.FlushResult.retry()
                                }
                            }
                        // Add `is ... ->` branches for future outgoing commands here...
                        else -> {
                            // Ignore any other outgoing commands that we don't support.
                            null
                        }
                    }
                }
                .awaitAll()
                .fold(DeviceCommandQueue.FlushResult.ok(), DeviceCommandQueue.FlushResult::and)
        }

    /** Sends a [DeviceCommandOutgoing] to another device. */
    fun interface CommandSender<in T : DeviceCommandOutgoing, out U> {
        /**
         * Sends the command.
         *
         * @param deviceId The target device ID.
         * @param command The command to send.
         * @return The result of sending the command to the target device.
         */
        suspend fun send(deviceId: String, command: T): U
    }

    /** The result of sending a [DeviceCommandOutgoing.CloseTab]. */
    sealed interface SendCloseTabsResult {
        /** The command was successfully sent. */
        data object Ok : SendCloseTabsResult

        /** The command was partially sent, and the [urls] that weren't sent should be resent in a new command. */
        data class RetryFor(val urls: List<String>) : SendCloseTabsResult

        /** The command couldn't be sent because the user isn't authenticated. */
        data object NoAccount : SendCloseTabsResult

        /** The command couldn't be sent because the target device is unavailable, or doesn't support the command. */
        data object NoDevice : SendCloseTabsResult

        /** The command couldn't be sent for any other reason. */
        data object Error : SendCloseTabsResult
    }

    /** Groups one or more [PendingCommand]s into a single [DeviceCommandOutgoing]. */
    internal data class PendingCommandGroup<T : DeviceCommandOutgoing>(
        val deviceId: String,
        val command: T,
        val pendingCommands: List<PendingCommand>,
    ) {
        /** Returns this group as a type-erased [PendingCommandGroup]. */
        val asAnyGroup: PendingCommandGroup<*> = this

        sealed interface Key {
            data class CloseTab(val deviceId: String) : Key

            // Add data classes for future pending command grouping keys here...

            /** Returns this grouping key as a type-erased [Key]. */
            val asAnyKey: Key
                get() = this
        }
    }
}

/** Represents a Sync client that can be associated with a list of opened tabs. */
data class SyncClient(val id: String)

/**
 * A tab, which is defined by an history (think the previous/next button in your web browser) and a currently active
 * history entry.
 */
data class Tab(
    val history: List<TabEntry>,
    val active: Int,
    val lastUsed: Long,
    val inactive: Boolean,
) {
    /** The current active tab entry. In other words, this is the page that's currently shown for a tab. */
    fun active(): TabEntry {
        return history[active]
    }

    /**
     * The list of tabs history entries that come before this tab. In other words, the "previous" navigation button
     * history list.
     */
    fun previous(): List<TabEntry> {
        return history.subList(0, active)
    }

    /**
     * The list of tabs history entries that come after this tab. In other words, the "next" navigation button history
     * list.
     */
    fun next(): List<TabEntry> {
        return history.subList(active + 1, history.lastIndex + 1)
    }
}

/** A synced device and its list of tabs. */
data class SyncedDeviceTabs(
    val device: Device,
    val tabs: List<Tab>,
)

/** A Tab history entry. */
data class TabEntry(
    val title: String,
    val url: String,
    val iconUrl: String?,
)
