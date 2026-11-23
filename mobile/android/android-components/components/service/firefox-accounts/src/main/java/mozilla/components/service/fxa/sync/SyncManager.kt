/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa.sync

import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PROTECTED
import androidx.work.WorkInfo
import java.io.Closeable
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.concept.storage.KeyProvider
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry

/** A collection of objects describing a reason for running a sync. */
sealed class SyncReason {
    /** Application is starting up, and wants to sync data. */
    object Startup : SyncReason()

    /** User is requesting a sync (e.g. pressed a "sync now" button). */
    object User : SyncReason()

    /** User changed enabled/disabled state of one or more [SyncEngine]s. */
    object EngineChange : SyncReason()

    /** Internal use only: first time running a sync. */
    internal object FirstSync : SyncReason()

    /** Internal use only: running a periodic sync. */
    internal object Scheduled : SyncReason()
}

/** An interface for consumers that wish to observer "sync lifecycle" events. */
interface SyncStatusObserver {
    /** Gets called at the start of a sync, before any configured syncable is synchronized. */
    fun onStarted()

    /**
     * Gets called at the end of a sync, after every configured syncable has been synchronized. A set of enabled
     * [SyncEngine]s could have changed, so observers are expected to query [SyncEnginesStorage.getStatus].
     */
    fun onIdle()

    /**
     * Gets called if sync encounters an error that's worthy of processing by status observers.
     *
     * @param error Optional relevant exception.
     */
    fun onError(error: Exception?)
}

/**
 * A lazy instance of a [SyncableStore] with an optional [KeyProvider] instance, used if this store has encrypted
 * contents. Lazy wrapping is in place to ensure we don't eagerly instantiate the storage.
 *
 * @property lazyStore A [SyncableStore] wrapped in [Lazy].
 * @property keyProvider An optional [KeyProvider] wrapped in [Lazy]. If present, it'll be used for crypto operations on
 *   the storage.
 */
data class LazyStoreWithKey(
    val lazyStore: Lazy<SyncableStore>,
    val keyProvider: Lazy<KeyProvider>? = null,
)

/**
 * A singleton registry of [SyncableStore] objects. [WorkManagerSyncDispatcher] will use this to access configured
 * [SyncableStore] instances.
 *
 * This pattern provides a safe way for library-defined background workers to access globally available instances of
 * stores within an application.
 */
object GlobalSyncableStoreProvider {
    private val stores: MutableMap<SyncEngine, LazyStoreWithKey> = mutableMapOf()

    /**
     * Configure an instance of [SyncableStore] for a [SyncEngine] enum.
     *
     * @param storePair A pair associating [SyncableStore] with a [SyncEngine].
     */
    fun configureStore(storePair: Pair<SyncEngine, Lazy<SyncableStore>>, keyProvider: Lazy<KeyProvider>? = null) {
        stores[storePair.first] = LazyStoreWithKey(lazyStore = storePair.second, keyProvider = keyProvider)
    }

    internal fun getLazyStoreWithKey(syncEngine: SyncEngine): LazyStoreWithKey? {
        return stores[syncEngine]
    }
}

/** Internal interface to enable testing SyncManager implementations independently from SyncDispatcher. */
internal interface SyncDispatcher : Closeable, Observable<SyncStatusObserver> {

    fun initialize()

    fun isSyncActive(): Boolean

    fun syncNow(
        reason: SyncReason,
        debounce: Boolean = false,
        customEngineSubset: List<SyncEngine> = listOf(),
    )

    fun startPeriodicSync(unit: TimeUnit, period: Long, initialDelay: Long)

    fun stopPeriodicSync()

    fun workersStateChanged(currentWorkStates: List<WorkInfo.State>?)

    fun setEngineEnabled(engine: SyncEngine, enabled: Boolean)
}

/**
 * A base sync manager implementation.
 *
 * @param syncConfig A [SyncConfig] object describing how sync should behave.
 */
internal abstract class SyncManager(private val syncConfig: SyncConfig) {
    open val logger = Logger("SyncManager")

    // A SyncDispatcher instance bound to an account and a set of syncable stores.
    @VisibleForTesting(otherwise = PROTECTED) internal var syncDispatcher: SyncDispatcher? = null

    private val syncStatusObserverRegistry = ObserverRegistry<SyncStatusObserver>()

    // Manager encapsulates events emitted by the wrapped observers, and passes them on to the outside world.
    // This split allows us to define a nice public observer API (manager -> consumers), along with
    // a more robust internal observer API (dispatcher -> manager).
    // Currently the interfaces are the same, hence the name "pass-through".
    private val dispatcherStatusObserver = PassThroughSyncStatusObserver(syncStatusObserverRegistry)

    /**
     * Emits whether sync is connected on this device, and if it is, whether it is able to run.
     *
     * Emits [SyncConnectionState.Uninitialized] until [initialize] has been called.
     */
    abstract val syncConnectionState: StateFlow<SyncConnectionState>

    /** Initializes the manager. */
    internal abstract fun initialize()

    /** Indicates if sync is currently running. */
    internal fun isSyncActive() = syncDispatcher?.isSyncActive() ?: false

    internal fun registerSyncStatusObserver(observer: SyncStatusObserver) {
        syncStatusObserverRegistry.register(observer)
    }

    /**
     * Configures and initializes sync, persisting that sync is connected on this device.
     *
     * @return [ConnectResult.Success] once sync is connected, or [ConnectResult.Failure] if connect did not succeed
     */
    abstract suspend fun connect(): ConnectResult

    /** Disconnect sync on this device. */
    abstract suspend fun disconnect()

    /**
     * Request an immediate synchronization of all configured stores.
     *
     * @param reason A [SyncReason] indicating why this sync is being requested.
     * @param debounce Whether or not this sync should debounced.
     * @param customEngineSubset A subset of supported engines to sync. Defaults to all supported engines.
     */
    internal fun now(
        reason: SyncReason,
        debounce: Boolean = false,
        customEngineSubset: List<SyncEngine> = listOf(),
    ) {
        synchronized(this) {
            if (syncDispatcher == null) {
                logger.info("Sync is not enabled. Ignoring 'sync now' request.")
            }
            syncDispatcher?.syncNow(reason, debounce, customEngineSubset)
        }
    }

    internal fun setEngineEnabled(engine: SyncEngine, enabled: Boolean) {
        syncDispatcher?.setEngineEnabled(engine, enabled)
    }

    /** Enables synchronization, with behaviour described in [syncConfig]. */
    internal fun start() =
        synchronized(this) {
            logger.debug("Enabling...")
            syncDispatcher = initDispatcher(newDispatcher(syncDispatcher, syncConfig.supportedEngines))
            logger.debug("set and initialized new dispatcher: $syncDispatcher")
        }

    /** Disables synchronization. */
    internal fun stop() =
        synchronized(this) {
            logger.debug("Disabling...")
            syncDispatcher?.close()
            syncDispatcher = null
        }

    internal abstract fun createDispatcher(supportedEngines: Set<SyncEngine>): SyncDispatcher

    internal abstract fun dispatcherUpdated(dispatcher: SyncDispatcher)

    private fun newDispatcher(
        currentDispatcher: SyncDispatcher?,
        supportedEngines: Set<SyncEngine>,
    ): SyncDispatcher {
        // Let the existing dispatcher, if present, cleanup.
        currentDispatcher?.close()
        // TODO will events from old and new dispatchers overlap..? How do we ensure correct sequence
        // for outside observers?
        currentDispatcher?.unregister(dispatcherStatusObserver)

        // Create a new dispatcher bound to current stores and account.
        return createDispatcher(supportedEngines)
    }

    private fun initDispatcher(dispatcher: SyncDispatcher): SyncDispatcher {
        dispatcher.register(dispatcherStatusObserver)
        syncConfig.periodicSyncConfig?.let {
            dispatcher.startPeriodicSync(
                TimeUnit.MINUTES,
                period = it.periodMinutes.toLong(),
                initialDelay = it.initialDelayMinutes.toLong(),
            )
        }
        dispatcherUpdated(dispatcher)
        dispatcher.initialize()
        return dispatcher
    }

    /**
     * Checks that when [requestedEngines] is not empty, it should be a subset of the supported engines.
     *
     * Throws an [IllegalStateException] if the requested engines are not a subset of the supported engines.
     */
    protected fun checkSupportedEngines(requestedEngines: Set<SyncEngine>) {
        check(requestedEngines.isEmpty() || syncConfig.supportedEngines.containsAll(requestedEngines)) {
            "Custom engines for sync must be a subset of supported engines."
        }
    }

    private class PassThroughSyncStatusObserver(private val passThroughRegistry: ObserverRegistry<SyncStatusObserver>) :
        SyncStatusObserver {
        override fun onStarted() {
            passThroughRegistry.notifyObservers { onStarted() }
        }

        override fun onIdle() {
            passThroughRegistry.notifyObservers { onIdle() }
        }

        override fun onError(error: Exception?) {
            passThroughRegistry.notifyObservers { onError(error) }
        }
    }
}
