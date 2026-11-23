/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa.sync

import android.content.Context
import androidx.annotation.UiThread
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.Data
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequest
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequest
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import java.io.Closeable
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.fxaclient.FxaException
import mozilla.appservices.sync15.EngineChoicesModel
import mozilla.appservices.sync15.SyncTelemetryPing
import mozilla.appservices.syncmanager.ServiceStatus
import mozilla.appservices.syncmanager.SyncAuthInfo
import mozilla.appservices.syncmanager.SyncEngineSelection
import mozilla.appservices.syncmanager.SyncParams
import mozilla.appservices.syncmanager.SyncTelemetry
import mozilla.components.concept.storage.KeyProvider
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthFlowError
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.fxa.FxaDeviceSettingsCache
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.GlobalAccountManager
import mozilla.components.service.fxa.manager.SCOPE_SYNC
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry

@VisibleForTesting
internal enum class SyncWorkerTag {
    Common,
    Immediate, // will not debounce a sync
    Debounce, // will debounce if another sync happened recently
}

internal enum class SyncWorkerName {
    Periodic,
    Immediate,
}

private const val KEY_DATA_STORES = "stores"
private const val KEY_REASON = "reason"

private const val KEY_IS_FULL_SYNC = "is_full_sync"

private const val SYNC_WORKER_BACKOFF_DELAY_MINUTES = 3L

/**
 * A [SyncManager] implementation which uses WorkManager APIs to schedule sync tasks.
 *
 * Must be initialized on the main thread.
 */
internal class WorkManagerSyncManager(
    private val context: Context,
    private val syncConfig: SyncConfig,
    private val syncStateStorageProvider: SyncStateStorage.Provider,
    private val syncEnginesStorage: SyncEnginesStorage = SyncEnginesStorage(context),
    private val rustSyncManager: RustSyncManager = DefaultRustSyncManager,
    private val accountManager: FxaAccountManager = GlobalAccountManager.requireAccountManager(),
    private val coroutineContext: CoroutineContext,
) : SyncManager(syncConfig) {
    override val logger = Logger("BgSyncManager")

    private val coroutineScope = CoroutineScope(coroutineContext + SupervisorJob())

    override val syncConnectionState: StateFlow<SyncConnectionState>
        field = MutableStateFlow<SyncConnectionState>(SyncConnectionState.Uninitialized)

    private val accountChangesObserver = AccountChangesObserver()

    init {
        GlobalAccountManager.setRustSyncManager(rustSyncManager)

        WorkersLiveDataObserver.init(context)

        if (syncConfig.periodicSyncConfig == null) {
            logger.info("Periodic syncing is disabled.")
        } else {
            logger.info("Periodic syncing enabled: ${syncConfig.periodicSyncConfig}")
        }
    }

    override fun initialize() {
        accountManager.register(accountChangesObserver)

        updateSyncConnectionState()
    }

    /**
     * Keeps [syncConnectionState] up to date with the account state, and with the stored connection preference when
     * sync decoupling is enabled. Never returns: it collects for as long as this manager lives.
     */
    private fun updateSyncConnectionState() = coroutineScope.launch {
        val syncConnectedFlow =
            if (syncConfig.syncDecouplingEnabled) {
                syncStateStorageProvider.get().syncConnectedFlow
            } else {
                flowOf(true)
            }

        accountChangesObserver.accountChangedFlow
            .onStart { emit(Unit) }
            .combine(syncConnectedFlow) { _, syncConnected: Boolean? ->
                // A `null` stored state means sync was never explicitly connected or disconnected, in which case we
                // assume it is connected: the user may be coming from a version where sync was always on.
                resolveConnectionState(syncConnected = syncConnected ?: true)
            }
            .distinctUntilChanged()
            .collect { syncConnectionState.value = it }
    }

    /**
     * Resolves the current [SyncConnectionState].
     *
     * @param syncConnected Whether sync is connected.
     * @return [SyncConnectionState.Disconnected] if there's no authenticated account, or sync is explicitly
     *   disconnected
     */
    private fun resolveConnectionState(syncConnected: Boolean): SyncConnectionState {
        return when {
            accountManager.authenticatedAccount() == null || syncConnected.not() -> SyncConnectionState.Disconnected
            else -> SyncConnectionState.Connected
        }
    }

    override suspend fun connect(): ConnectResult {
        return withContext(coroutineContext) {
            logger.info("connect - setting up sync")

            val account = accountManager.connectedAccount()
            val result =
                when {
                    account == null -> ConnectResult.Failure.NeedsAuthentication
                    !account.hasScope(SCOPE_SYNC) -> ConnectResult.Failure.NeedsSyncAuthorization
                    else -> connectSync()
                }
            logger.info("connect - result = $result")
            result
        }
    }

    override suspend fun disconnect() {
        withContext(coroutineContext) {
            logger.info("disconnect - disabling sync")
            stop()
            rustSyncManager.disconnect()
            syncStateStorageProvider.get().reset()
            syncEnginesStorage.clear()
        }
    }

    private suspend fun connectSync(): ConnectResult.Success {
        syncStateStorageProvider.get().storeSyncConnected(connected = true)

        // Call start only if we don't already have a sync dispatcher
        if (syncDispatcher == null) start()

        now(
            reason = SyncReason.FirstSync,
            debounce = false,
        )

        return ConnectResult.Success
    }

    override fun createDispatcher(supportedEngines: Set<SyncEngine>): SyncDispatcher {
        return WorkManagerSyncDispatcher(
            context = context,
            supportedEngines = supportedEngines,
            coroutineContext = coroutineContext,
        )
    }

    override fun dispatcherUpdated(dispatcher: SyncDispatcher) {
        WorkersLiveDataObserver.setDispatcher(dispatcher)
    }

    /**
     * Observes the account for changes that can connect or disconnect sync. The callbacks are used purely as triggers
     * to cause the account manager states to be re-read.
     */
    private class AccountChangesObserver : AccountObserver {
        val accountChangedFlow =
            MutableSharedFlow<Unit>(extraBufferCapacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)

        override fun onReady(authenticatedAccount: OAuthAccount?) = onAccountChanged()

        override fun onAuthenticated(account: OAuthAccount, authType: AuthType) = onAccountChanged()

        override fun onAuthenticationProblems() = onAccountChanged()

        override fun onLoggedOut() = onAccountChanged()

        override fun onFlowError(error: AuthFlowError) = onAccountChanged()

        private fun onAccountChanged() {
            accountChangedFlow.tryEmit(Unit)
        }
    }
}

/**
 * A singleton wrapper around the the LiveData "forever" observer - i.e. an observer not bound to a lifecycle owner.
 * This observer is always active. We will have different dispatcher instances throughout the lifetime of the app, but
 * always a single LiveData instance.
 */
internal object WorkersLiveDataObserver {
    private lateinit var workManager: WorkManager
    private val workersLiveData by lazy {
        workManager.getWorkInfosByTagLiveData(SyncWorkerTag.Common.name)
    }

    private var dispatcher: SyncDispatcher? = null

    /**
     * Initializes the Observer.
     *
     * @param context the context that will be used to with the [WorkManager] to observe workers.
     */
    @UiThread
    fun init(context: Context) {
        workManager = WorkManager.getInstance(context)

        // Only set our observer once.
        if (workersLiveData.hasObservers()) return

        // This must be called on the UI thread.
        workersLiveData.observeForever { workers ->
            dispatcher?.workersStateChanged(workers?.map { it.state })

            // TODO process errors coming out of worker.outputData
        }
    }

    fun setDispatcher(dispatcher: SyncDispatcher) {
        this.dispatcher = dispatcher
    }
}

internal class WorkManagerSyncDispatcher(
    private val context: Context,
    private val supportedEngines: Set<SyncEngine>,
    private val coroutineContext: CoroutineContext,
) : SyncDispatcher, Observable<SyncStatusObserver> by ObserverRegistry(), Closeable {
    private val logger = Logger("WMSyncDispatcher")
    private val coroutineScope = CoroutineScope(coroutineContext + SupervisorJob())

    private var isSyncActive = false

    init {
        // Stop any currently active periodic syncing. Consumers of this class are responsible for
        // starting periodic syncing via [startPeriodicSync] if they need it.
        stopPeriodicSync()
    }

    override fun initialize() {
        coroutineScope.launch(context = coroutineContext) {
            SyncOperationTracker.isSyncInProgress.collect { active ->
                isSyncActive = active
                notifyObservers {
                    if (active) {
                        onStarted()
                    } else {
                        onIdle()
                    }
                }
            }
        }
    }

    override fun workersStateChanged(currentWorkStates: List<WorkInfo.State>?) = Unit

    override fun isSyncActive(): Boolean {
        return isSyncActive
    }

    override fun syncNow(
        reason: SyncReason,
        debounce: Boolean,
        customEngineSubset: List<SyncEngine>,
    ) {
        val engines = customEngineSubset.joinToString { it.nativeName }
        logger.debug("Immediate sync requested, reason = $reason, debounce = $debounce, custom engines = $engines")
        val delayMs =
            if (reason == SyncReason.Startup) {
                // Startup delay is there to avoid SQLITE_BUSY crashes, since we currently do a poor job
                // of managing database connections, and we expect there to be database writes at the start.
                // We've done bunch of work to make this better (see
                // https://github.com/mozilla-mobile/android-components/issues/1369),
                // but it's not clear yet this delay is completely safe to remove.
                SYNC_STARTUP_DELAY_MS
            } else {
                0L
            }
        // Use the 'keep' policy to minimize overhead from multiple "sync now" operations coming in
        // at the same time. However, this policy means that if we are in a "retry" state the sync
        // doesn't happen until that retry expires - which could be 5 hours!
        // So a full user-requested or first sync replaces pending work instead. We rely on the UI
        // disabling the "sync now" button to avoid multiple user-requested syncs.
        // Only a sync of every engine gets to replace a scheduled work manager sync work: a subset sync would otherwise
        // cancel a broader sync that hasn't started yet, dropping the engines it doesn't cover.
        val policy =
            if ((reason == SyncReason.User || reason == SyncReason.FirstSync) && customEngineSubset.isEmpty()) {
                ExistingWorkPolicy.REPLACE
            } else {
                ExistingWorkPolicy.KEEP
            }
        WorkManager.getInstance(context)
            .beginUniqueWork(
                SyncWorkerName.Immediate.name,
                policy,
                regularSyncWorkRequest(reason, delayMs, debounce, customEngineSubset),
            )
            .enqueue()
    }

    override fun setEngineEnabled(engine: SyncEngine, enabled: Boolean) {
        SyncEnginesStorage(context).setStatus(engine, enabled)
        syncNow(SyncReason.EngineChange, debounce = false)
    }

    override fun close() {
        coroutineScope.cancel()
        unregisterObservers()
        stopPeriodicSync()
        stopImmediateSync()
    }

    /** Periodic background syncing is mainly intended to reduce workload when we sync during application startup. */
    override fun startPeriodicSync(unit: TimeUnit, period: Long, initialDelay: Long) {
        logger.debug("Starting periodic syncing, period = $period, time unit = $unit")
        // Use the 'update' policy as a simple way to upgrade periodic worker configurations across
        // application versions. We do this instead of versioning workers.
        WorkManager.getInstance(context)
            .enqueueUniquePeriodicWork(
                SyncWorkerName.Periodic.name,
                ExistingPeriodicWorkPolicy.UPDATE,
                periodicSyncWorkRequest(unit, period, initialDelay),
            )
    }

    /**
     * Disables periodic syncing in the background. Currently running syncs may continue until completion. Safe to call
     * this even if periodic syncing isn't currently enabled.
     */
    override fun stopPeriodicSync() {
        logger.debug("Cancelling periodic syncing")
        WorkManager.getInstance(context).cancelUniqueWork(SyncWorkerName.Periodic.name)
    }

    /** Disables any immediate sync jobs running. */
    private fun stopImmediateSync() {
        logger.debug("Cancelling immediate syncing")
        WorkManager.getInstance(context).cancelUniqueWork(SyncWorkerName.Immediate.name)
    }

    private fun periodicSyncWorkRequest(unit: TimeUnit, period: Long, initialDelay: Long): PeriodicWorkRequest {
        val data = getWorkerData(reason = SyncReason.Scheduled, supportedEngines = supportedEngines)
        // Periodic interval must be at least PeriodicWorkRequest.MIN_PERIODIC_INTERVAL_MILLIS,
        // e.g. not more frequently than 15 minutes.
        return PeriodicWorkRequestBuilder<WorkManagerSyncWorker>(period, unit, initialDelay, unit)
            .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
            .setInputData(data)
            .addTag(SyncWorkerTag.Common.name)
            .addTag(SyncWorkerTag.Debounce.name)
            .setBackoffCriteria(
                BackoffPolicy.LINEAR,
                SYNC_WORKER_BACKOFF_DELAY_MINUTES,
                TimeUnit.MINUTES,
            )
            .build()
    }

    private fun regularSyncWorkRequest(
        reason: SyncReason,
        delayMs: Long = 0L,
        debounce: Boolean = false,
        customEngineSubset: List<SyncEngine> = listOf(),
    ): OneTimeWorkRequest {
        val data =
            getWorkerData(
                reason = reason,
                supportedEngines = supportedEngines,
                customEngineSubset = customEngineSubset,
            )
        return OneTimeWorkRequestBuilder<WorkManagerSyncWorker>()
            .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
            .setInputData(data)
            .addTag(SyncWorkerTag.Common.name)
            .addTag(if (debounce) SyncWorkerTag.Debounce.name else SyncWorkerTag.Immediate.name)
            .setInitialDelay(delayMs, TimeUnit.MILLISECONDS)
            .setBackoffCriteria(
                BackoffPolicy.LINEAR,
                SYNC_WORKER_BACKOFF_DELAY_MINUTES,
                TimeUnit.MINUTES,
            )
            .build()
    }

    companion object {

        /**
         * Creates the worker data for [WorkManagerSyncWorker]
         *
         * @param reason The [SyncReason] for the sync
         * @param supportedEngines The set of supported [SyncEngine] defined by [SyncConfig]
         * @param customEngineSubset The specific set of [SyncEngine]s which are to be synced. The collection must be a
         *   subset of [supportedEngines]. An empty collection implies that we want to sync all of the user's active
         *   engines (full sync) and a non-empty collection implies that these are the specific engines that we want to
         *   sync.
         */
        internal fun getWorkerData(
            reason: SyncReason,
            supportedEngines: Set<SyncEngine>,
            customEngineSubset: List<SyncEngine> = listOf(),
        ): Data {
            // an empty list of engine subset implies "do a full sync of all the user's enabled engines"
            val isFullSync = customEngineSubset.isEmpty()
            val enginesToSync = customEngineSubset.takeIf { it.isNotEmpty() } ?: supportedEngines
            return Data.Builder()
                .putStringArray(KEY_DATA_STORES, enginesToSync.map { it.nativeName }.toTypedArray())
                .putBoolean(KEY_IS_FULL_SYNC, isFullSync)
                .putString(KEY_REASON, reason.asString())
                .build()
        }
    }
}

internal class WorkManagerSyncWorker(
    private val context: Context,
    private val params: WorkerParameters,
) : CoroutineWorker(context, params) {
    private val logger = Logger("SyncWorker")

    private val accountManager: FxaAccountManager
        get() = GlobalAccountManager.requireAccountManager()

    private val rustSyncManager: RustSyncManager
        get() = GlobalAccountManager.requireRustSyncManager()

    private val syncStateStorageProvider: SyncStateStorage.Provider
        get() = GlobalAccountManager.requireSyncStateStorageProvider()

    private val clock by lazy {
        GlobalAccountManager.systemClock
    }

    /**
     * Whether or not this sync is a full sync. See [WorkManagerSyncDispatcher.Companion.getWorkerData] for how
     * [KEY_IS_FULL_SYNC] input is determined.
     *
     * We choose to default to true in this case to keep existing behaviours on jobs that have been scheduled prior to
     * this update. Once the app is opened and we schedule new sync, this flag will always be present.
     */
    private val isFullSync by lazy { params.inputData.getBoolean(KEY_IS_FULL_SYNC, true) }

    @VisibleForTesting
    internal fun isDebounced(): Boolean {
        return params.tags.contains(SyncWorkerTag.Debounce.name)
    }

    @VisibleForTesting
    internal fun lastSyncedWithinStaggerBuffer(engine: String): Boolean {
        if (!isDebounced()) {
            return false
        }

        return engineSyncTimestamp[engine]?.let { isWithinStaggerBuffer(it) } ?: false
    }

    override suspend fun doWork(): Result =
        withContext(GlobalAccountManager.syncIoDispatcher) {
            logger.debug("Starting sync... Tagged as: ${params.tags}")

            // We will need a list of SyncableStores.
            val syncableStores =
                params.inputData
                    .getNullableStringArray(KEY_DATA_STORES)
                    ?.filterNotNull()
                    ?.filter {
                        !lastSyncedWithinStaggerBuffer(it)
                    }
                    ?.associate {
                        // Convert from a string back to our SyncEngine type.
                        val engine =
                            when (it) {
                                SyncEngine.History.nativeName -> SyncEngine.History
                                SyncEngine.Bookmarks.nativeName -> SyncEngine.Bookmarks
                                SyncEngine.Passwords.nativeName -> SyncEngine.Passwords
                                SyncEngine.Tabs.nativeName -> SyncEngine.Tabs
                                SyncEngine.CreditCards.nativeName -> SyncEngine.CreditCards
                                SyncEngine.Addresses.nativeName -> SyncEngine.Addresses
                                else -> throw IllegalStateException("Invalid syncable store: $it")
                            }

                        recordEngineSyncedTime(engine.nativeName)
                        engine to
                            checkNotNull(GlobalSyncableStoreProvider.getLazyStoreWithKey(engine)) {
                                "SyncableStore missing from GlobalSyncableStoreProvider: ${engine.nativeName}"
                            }
                    }

            if (syncableStores.isNullOrEmpty()) {
                // Short-circuit if there are no configured stores.
                // Don't update the "last-synced" timestamp because we haven't actually synced anything.
                Result.success()
            } else {
                SyncOperationTracker.executeSyncOperation {
                    doSync(syncableStores)
                }
            }
        }

    @Suppress("LongMethod", "CyclomaticComplexMethod")
    private suspend fun doSync(syncableStores: Map<SyncEngine, LazyStoreWithKey>): Result {
        val engineKeyProviders = mutableMapOf<SyncEngine, KeyProvider>()
        val syncStateStorage = syncStateStorageProvider.get()

        // We need to tell RustSyncManager which engines to sync.
        val enginesToSync = SyncEngineSelection.Some(syncableStores.map { it.key.nativeName })

        // We need to tell RustSyncManager about instances of supported stores ('places' and 'logins').
        // NOTE: This need only be done once, not each sync - but the only impact is that
        // it's slightly less efficient so refactoring might not be worthwhile.
        syncableStores.entries.forEach {
            // We're assuming all syncable stores live in Rust.
            // Currently `RustSyncManager` doesn't support non-Rust sync engines.
            when (it.key) {
                SyncEngine.History -> it.value.lazyStore.value.registerWithSyncManager()
                SyncEngine.Bookmarks -> it.value.lazyStore.value.registerWithSyncManager()
                SyncEngine.CreditCards -> {
                    it.value.lazyStore.value.registerWithSyncManager()
                }
                SyncEngine.Addresses -> {
                    it.value.lazyStore.value.registerWithSyncManager()
                }
                SyncEngine.Passwords -> {
                    it.value.lazyStore.value.registerWithSyncManager()
                }
                SyncEngine.Tabs -> {
                    it.value.lazyStore.value.registerWithSyncManager()
                }
                else -> throw NotImplementedError("Unsupported engine: ${it.key}")
            }
        }

        // We need to know the reason we're syncing.
        val reason = params.inputData.getString(KEY_REASON)!!.toSyncReason()

        // We need a "sync auth info" object.
        val syncAuthInfo = getSyncAuthInfo() ?: return Result.failure()

        // We need any persisted state that we received from RustSyncManager in the past.
        // We should be able to pass a `null` value, but currently the library will crash.
        // See https://github.com/mozilla/application-services/issues/1829
        val currentSyncState = syncStateStorage.persistedSyncState ?: ""

        // We need to tell RustSyncManager about our local "which engines are enabled/disabled" state.
        // This is a global state, shared by every sync client for this account. State that we will
        // pass here will overwrite current global state and may be propagated to every sync client.
        // This should be empty if there have been no changes to this state.
        // We pass this state if user changed it (EngineChange) or if we're in a first sync situation.
        // A "first sync" will happen after signing up or signing in.
        // In both cases, user may have been asked which engines they'd like to sync.
        val enabledChanges =
            when (reason) {
                SyncReason.EngineChange,
                SyncReason.FirstSync -> {
                    val engineMap = SyncEnginesStorage(context).getStatus().toMutableMap()
                    // For historical reasons, a "history engine" really means two sync collections: history and forms.
                    // Underlying library doesn't manage this for us, and other clients will get confused
                    // if we modify just "history" without also modifying "forms", and vice versa.
                    // So: whenever a change to the "history" engine happens locally, inject the same "forms" change.
                    // This should be handled by RustSyncManager. See
                    // https://github.com/mozilla/application-services/issues/1832
                    engineMap[SyncEngine.History]?.let {
                        engineMap[SyncEngine.Forms] = it
                    }
                    engineMap.mapKeys { it.key.nativeName }
                }
                else -> emptyMap()
            }

        // We need to tell RustSyncManager about our current FxA device. It needs that information
        // in order to sync the 'clients' collection.
        // We're depending on cache being populated. An alternative to using a "cache" is to
        // configure the worker with the values stored in it: device name, type and fxaDeviceID.
        // While device type and fxaDeviceID are stable, users are free to change device name whenever.
        // We need to reflect these changes during a sync. Our options are then: provide a global cache,
        // or re-configure our workers every time a change is made to the device name.
        // We have the same basic story already with syncAuthInfo cache, and a cache is much easier
        // to implement/reason about than worker reconfiguration.
        val deviceSettings = FxaDeviceSettingsCache(context).getCached()!!

        // Obtain encryption keys for stores that came along with KeyProviders.
        // This can take a bit of time!
        val localEncryptionKeys =
            engineKeyProviders
                .mapKeys {
                    it.key.nativeName
                }
                .mapValues {
                    it.value.getOrGenerateKey().key
                }

        // We're now ready to sync.
        val syncParams =
            SyncParams(
                reason = reason.toRustSyncReason(),
                engines = enginesToSync,
                authInfo = syncAuthInfo,
                engineChoicesModel = EngineChoicesModel.Account(enabledChanges = enabledChanges),
                persistedState = currentSyncState,
                deviceSettings = deviceSettings,
                localEncryptionKeys = localEncryptionKeys,
            )

        logger.debug("Calling into rust sync manager")
        val syncResult = rustSyncManager.sync(syncParams)

        // Persist the sync state; it may have changed during a sync, and RustSyncManager relies on us
        // to store it.
        syncStateStorage.persistedSyncState = syncResult.persistedState

        // Log the results.
        syncResult.failures.entries.forEach {
            logger.error("Failed to sync ${it.key}, reason: ${it.value}")
        }
        syncResult.successful.forEach {
            logger.info("Successfully synced $it")
        }

        // Process any changes to the list of declined/accepted engines.
        // NB: We may have received engine state information about an engine we're unfamiliar with.
        // `SyncEngine.Other(string)` stands in for unknown engines.
        val declinedEngines = syncResult.declined?.map { it.toSyncEngine() }?.toSet() ?: emptySet()
        // We synthesize the 'accepted' list ourselves: engines we know about - declined engines.
        // This assumes that "engines we know about" is a subset of engines RustSyncManager knows about.
        // RustSyncManager will handle this, eventually.
        // See: https://github.com/mozilla/application-services/issues/1685
        val acceptedEngines = syncableStores.keys.filter { !declinedEngines.contains(it) }

        // Persist engine state changes.
        with(SyncEnginesStorage(context)) {
            declinedEngines.forEach { setStatus(it, status = false) }
            acceptedEngines.forEach { setStatus(it, status = true) }
        }

        // Process telemetry.
        syncResult.telemetryJson?.let { SyncTelemetry.processSyncTelemetry(SyncTelemetryPing.fromJSONString(it)) }

        // Finally, declare success, failure or request a retry based on 'sync status'.
        return when (syncResult.status) {
            // Happy case.
            ServiceStatus.OK -> {
                // Worker should set the "last-synced" timestamp, and since we have a single timestamp,
                // it's not clear if a single failure should prevent its update. That's the current behaviour
                // in Fennec, but for very specific reasons that aren't relevant here. We could have
                // a timestamp per store, or whatever we want here really.
                // For now, we just update it every time we successfully do a full sync
                if (isFullSync) {
                    syncStateStorage.lastSynced = clock.now()
                }
                Result.success()
            }

            // Retry cases.
            // NB: retry doesn't mean "immediate retry". It means "retry, but respecting this worker's
            // backoff policy, as configured during worker's creation.
            // TODO FOR ALL retries: look at workerParams.mRunAttemptCount, don't retry after a certain number.
            ServiceStatus.NETWORK_ERROR -> {
                logger.error("Network error")
                Result.retry()
            }
            ServiceStatus.BACKED_OFF -> {
                logger.error("Backed-off error")
                // As part of `syncResult`, we get back `nextSyncAllowedAt`. Ideally, we should not retry
                // before that passes. However, we can not reconfigure back-off policy for an already
                // created Worker. So, we just rely on a sensible default. `RustSyncManager` will fail
                // to sync with a BACKED_OFF error without hitting the server if we don't respect
                // `nextSyncAllowedAt`, so we should be good either way.
                Result.retry()
            }
            // Failure cases.
            ServiceStatus.AUTH_ERROR -> {
                logger.error("Auth error")
                GlobalAccountManager.authError("RustSyncManager.sync", forSync = true)
                Result.failure()
            }
            ServiceStatus.SERVICE_ERROR -> {
                logger.error("Service error")
                Result.failure()
            }
            ServiceStatus.OTHER_ERROR -> {
                logger.error("'Other' error :(")
                Result.failure()
            }
        }
    }

    /** Get sync auth info and notify the sync observers of any error */
    private suspend fun getSyncAuthInfo(): SyncAuthInfo? {
        return try {
            logger.debug("Fetching account & sync token for auth info")
            val account = accountManager.connectedAccount() ?: error("No connected account")
            val token = account.getAccessToken(SCOPE_SYNC) ?: error("Unable to retrieve the sync access token")
            val tokenServerUrl = account.getTokenServerEndpointURL() ?: error("Unable to retrieve the token server url")

            val authKey = token.key ?: error("Access token unexpectedly without key")

            SyncAuthInfo(
                kid = authKey.kid,
                fxaAccessToken = token.token,
                syncKey = authKey.k,
                tokenserverUrl = tokenServerUrl,
            )
        } catch (exception: FxaException.Forbidden) {
            logger.error("Account is not authorized for the Sync scope", exception)
            null
        } catch (exception: IllegalStateException) {
            logger.error("Error getting sync auth info", exception)
            null
        }
    }

    companion object {
        @VisibleForTesting internal const val SYNC_STAGGER_BUFFER_MS = 5 * 1000L // 5 seconds.

        @VisibleForTesting internal val engineSyncTimestamp = ConcurrentHashMap<String, Long>()
    }
}

private const val SYNC_STATE_PREFS_KEY = "syncPrefs"
private const val SYNC_LAST_SYNCED_KEY = "lastSynced"

private const val SYNC_STARTUP_DELAY_MS = 5 * 1000L // 5 seconds.

@VisibleForTesting
internal fun isWithinStaggerBuffer(lastSyncedMs: Long, now: Long = System.currentTimeMillis()): Boolean =
    (now - lastSyncedMs) < WorkManagerSyncWorker.SYNC_STAGGER_BUFFER_MS

private fun recordEngineSyncedTime(engine: String, now: Long = System.currentTimeMillis()) {
    WorkManagerSyncWorker.engineSyncTimestamp[engine] = now
}

/**
 * This API is currently compatible with [SharedPrefsSyncStateStorage] because they have the underlying shared
 * preferences, but this API will be completely removed in bug https://bugzilla.mozilla.org/show_bug.cgi?id=2067060
 */
@Deprecated(
    message =
        "This API will be removed in the future. The last synced time should be accessed through the standard way" +
            " of accessing sync state"
)
fun getLastSynced(context: Context): Long {
    return context.getSharedPreferences(SYNC_STATE_PREFS_KEY, Context.MODE_PRIVATE).getLong(SYNC_LAST_SYNCED_KEY, 0)
}

/**
 * Saves the lastSyncedTime to the shared preferences. Will be removed in
 * https://bugzilla.mozilla.org/show_bug.cgi?id=2067060
 *
 * @param context the context
 * @param lastSyncedTime - the last synced time in milliseconds. Defaults to the current time.
 * @return the time that was stored.
 */
@Deprecated(message = "This API will be removed in bug 2067060. This should not be set externally")
fun setLastSynced(context: Context, lastSyncedTime: Long = System.currentTimeMillis()): Long {
    context.getSharedPreferences(SYNC_STATE_PREFS_KEY, Context.MODE_PRIVATE).edit {
        putLong(SYNC_LAST_SYNCED_KEY, lastSyncedTime)
    }
    return lastSyncedTime
}
