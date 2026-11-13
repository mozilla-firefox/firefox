/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa.manager

import android.content.Context
import androidx.annotation.GuardedBy
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import java.io.Closeable
import java.util.concurrent.Executors
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.fxaclient.DeviceConfig as ASDeviceConfig
import mozilla.appservices.fxaclient.FxaEvent
import mozilla.appservices.fxaclient.FxaException
import mozilla.appservices.fxaclient.FxaRustAuthState
import mozilla.appservices.fxaclient.FxaState
import mozilla.appservices.syncmanager.DeviceSettings
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.sync.AccountEvent
import mozilla.components.concept.sync.AccountEventsObserver
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthFlowError
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.DeviceConfig
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.Profile
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.fxa.AccountManagerException
import mozilla.components.service.fxa.AccountStorage
import mozilla.components.service.fxa.FxaAuthData
import mozilla.components.service.fxa.FxaDeviceSettingsCache
import mozilla.components.service.fxa.SecureAbove22AccountStorage
import mozilla.components.service.fxa.ServerConfig
import mozilla.components.service.fxa.SharedPrefAccountStorage
import mozilla.components.service.fxa.StorageWrapper
import mozilla.components.service.fxa.emitSyncFailedFact
import mozilla.components.service.fxa.into
import mozilla.components.service.fxa.sync.SharedPrefsSyncStateStorage
import mozilla.components.service.fxa.sync.SyncConnectionState
import mozilla.components.service.fxa.sync.SyncManager
import mozilla.components.service.fxa.sync.SyncReason
import mozilla.components.service.fxa.sync.SyncStateStorage
import mozilla.components.service.fxa.sync.SyncStatusObserver
import mozilla.components.service.fxa.sync.WorkManagerSyncManager
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry
import mozilla.components.support.base.utils.NamedThreadFactory

// Necessary to fetch a profile.
const val SCOPE_PROFILE = "profile"

// Necessary to obtain sync keys.
const val SCOPE_SYNC = "https://identity.mozilla.com/apps/oldsync"

// Necessary to obtain a sessionToken, which gives full access to the account.
const val SCOPE_SESSION = "https://identity.mozilla.com/tokens/session"

// If we see more than AUTH_CHECK_CIRCUIT_BREAKER_COUNT checks, and each is less than
// AUTH_CHECK_CIRCUIT_BREAKER_RESET_MS since the last check, then we'll trigger a "circuit breaker".
const val AUTH_CHECK_CIRCUIT_BREAKER_RESET_MS = 1000L * 10
const val AUTH_CHECK_CIRCUIT_BREAKER_COUNT = 10
// This logic is in place to protect ourselves from endless auth recovery loops, while at the same
// time allowing for a few 401s to hit the state machine in a quick succession.
// For example, initializing the account state machine & syncing after letting our access tokens expire
// due to long period of inactivity will trigger a few 401s, and that shouldn't be a cause for concern.

const val MAX_NETWORK_RETRIES = 3

/**
 * An account manager which encapsulates various internal details of an account lifecycle and provides an observer
 * interface along with a public API for interacting with an account. The internal state machine abstracts over state
 * space as exposed by the fxaclient library, not the internal states experienced by lower-level representation of a
 * Firefox Account; those are opaque to us.
 *
 * Class is 'open' to facilitate testing.
 *
 * @param context A [Context] instance that's used for internal messaging and interacting with local storage.
 * @param serverConfig A [ServerConfig] used for account initialization.
 * @param deviceConfig A description of the current device (name, type, capabilities).
 * @param syncConfig Optional, initial sync behaviour configuration. Sync will be disabled if this is `null`.
 * @param applicationScopes A set of scopes which will be requested during account authentication.
 */
@Suppress("TooManyFunctions", "LargeClass")
open class FxaAccountManager(
    private val context: Context,
    val serverConfig: ServerConfig,
    private val deviceConfig: DeviceConfig,
    private val syncConfig: SyncConfig?,
    private val applicationScopes: Set<String> = emptySet(),
    private val crashReporter: CrashReporting? = null,
    // We want a single-threaded execution model for our account-related "actions" (state machine side-effects).
    // That is, we want to ensure a sequential execution flow, but on a background thread.
    private val coroutineContext: CoroutineContext =
        Executors.newSingleThreadExecutor(NamedThreadFactory("FxaAccountManager")).asCoroutineDispatcher() +
            SupervisorJob(),
) : Closeable, Observable<AccountObserver> by ObserverRegistry() {
    private val logger = Logger("FirefoxAccountStateMachine")

    init {
        GlobalAccountManager.setInstance(this)
    }

    private val accountOnDisk by lazy { getStorageWrapper().account() }
    private val account by lazy { accountOnDisk.account() }
    private val accountStateEventsObserver = AccountStateEventsObserver(this::queueEvent)

    // Note on threading: we use a single-threaded executor, so there's no concurrent access possible.
    // However, that executor doesn't guarantee that it'll always use the same thread, and so vars
    // are marked as volatile for across-thread visibility. Similarly, event queue uses a concurrent
    // list, although that's probably an overkill.
    @Volatile private var profile: Profile? = null

    @Volatile private var state: FxaState = FxaState.Uninitialized

    @Volatile private var isAccountManagerReady: Boolean = false

    @VisibleForTesting val accountEventObserverRegistry = ObserverRegistry<AccountEventsObserver>()

    @VisibleForTesting open val syncStatusObserverRegistry = ObserverRegistry<SyncStatusObserver>()

    // We always obtain a "profile" scope, as that's assumed to be needed for any application integration.
    // We obtain a sync scope only if this was requested by the application via SyncConfig.
    // Additionally, we obtain any scopes that the application requested explicitly.
    // XXX - We need to rethink this - with "sync decoupling" and VPN etc, there aren't really any "default scopes",
    // other than maybe "profile".
    private val scopes: Set<String>
        get() =
            if (syncConfig != null) {
                    setOf(SCOPE_PROFILE, SCOPE_SYNC)
                } else {
                    setOf(SCOPE_PROFILE)
                }
                .plus(applicationScopes)

    // Internal backing field for the syncManager. This will be `null` if passed in SyncConfig (either
    // via the constructor, or via [setSyncConfig]) is also `null` - that is, sync will be disabled.
    // Note that trying to perform a sync while account isn't authenticated will not succeed.
    @GuardedBy("this") private var syncManager: SyncManager? = null

    /** Emits whether sync is connected on this device. */
    val syncConnectionState: StateFlow<SyncConnectionState> by lazy {
        syncManager?.syncConnectionState ?: MutableStateFlow(SyncConnectionState.Uninitialized)
    }

    /** Provider for retrieving a [SyncStateStorage] instance. */
    private val syncStateStorageProvider by lazy {
        SharedPrefsSyncStateStorage.SingletonProvider(context, coroutineContext)
    }

    init {
        registerForAccountEvents(accountStateEventsObserver, ProcessLifecycleOwner.get(), false)

        GlobalAccountManager.setSyncStateStorageProvider(syncStateStorageProvider)
        syncConfig?.let {
            // Initialize sync manager with the passed-in config.
            require(syncConfig.supportedEngines.isNotEmpty()) {
                "Set of supported engines can't be empty"
            }

            syncManager =
                createSyncManager(syncConfig).also {
                    // Observe account state changes.
                    this.register(AccountsToSyncIntegration(it))

                    // Observe sync status changes.
                    it.registerSyncStatusObserver(SyncToAccountsIntegration(this))
                }
        }

        if (syncManager == null) {
            logger.info("Sync is disabled")
        } else {
            logger.info("Sync is enabled")
        }
    }

    /** @return A list of currently supported [SyncEngine]s. `null` if sync isn't configured. */
    fun supportedSyncEngines(): Set<SyncEngine>? {
        // Notes on why this exists:
        // Parts of the system that make up an "fxa + sync" experience need to know which engines
        // are supported by an application. For example, FxA web content UI may present a "choose what
        // to sync" dialog during account sign-up, and application needs to be able to configure that
        // dialog. A list of supported engines comes to us from the application via passed-in SyncConfig.
        // Naturally, we could let the application configure any other part of the system that needs
        // to have access to supported engines. From the implementor's point of view, this is an extra
        // hurdle - instead of configuring only the account manager, they need to configure additional
        // classes. Additionally, we currently allow updating sync config "in-flight", not just at
        // the time of initialization. Providing an API for accessing currently configured engines
        // makes re-configuring SyncConfig less error-prone, as only one class needs to be told of the
        // new config.
        // Merits of allowing applications to re-configure SyncConfig after initialization are under
        // question, however: currently, we do not use that capability.
        return syncConfig?.supportedEngines
    }

    /**
     * Request an immediate synchronization, as configured according to [syncConfig].
     *
     * @param reason A [SyncReason] indicating why this sync is being requested.
     * @param debounce Boolean flag indicating if this sync may be debounced (in case another sync executed recently).
     * @param customEngineSubset A subset of supported engines to sync. Defaults to all supported engines.
     */
    suspend fun syncNow(
        reason: SyncReason,
        debounce: Boolean = false,
        customEngineSubset: List<SyncEngine> = listOf(),
    ) =
        withContext(coroutineContext) {
            check(
                customEngineSubset.isEmpty() || syncConfig?.supportedEngines?.containsAll(customEngineSubset) == true
            ) {
                "Custom engines for sync must be a subset of supported engines."
            }
            when (val s = state) {
                // All good, request a sync.
                FxaState.Connected -> {
                    // Access to syncManager is guarded by `this`.
                    synchronized(this@FxaAccountManager) {
                        checkNotNull(syncManager == null) {
                            "Sync is not configured. Construct this class with a 'syncConfig' or use 'setSyncConfig'"
                        }
                        syncManager?.now(reason, debounce, customEngineSubset)
                    }

                    // Only an explicit full "sync now"; feature syncs (e.g. tabs) run on every tab change.
                    if (reason == SyncReason.User && customEngineSubset.isEmpty()) {
                        refreshProfile(ignoreCache = true)
                    }
                }
                else -> logger.info("Ignoring syncNow request, not in the right state: $s")
            }
        }

    /** Indicates if sync is currently running. */
    fun isSyncActive() = syncManager?.isSyncActive() ?: false

    /**
     * Sets the enabled state of a sync engine and triggers a sync with [SyncReason.EngineChange].
     *
     * @param engine The [SyncEngine] to enable or disable.
     * @param enabled Whether the engine should be enabled or disabled.
     */
    suspend fun setEngineEnabled(engine: SyncEngine, enabled: Boolean) =
        withContext(coroutineContext) {
            syncManager?.setEngineEnabled(engine, enabled)
        }

    /**
     * Checks whether the given OAuth [scope] is granted to the persisted account. Only consults the stored account when
     * it is in an accessible state; otherwise `false` is returned.
     *
     * @param scope The OAuth scope to look for.
     * @return `true` if the scope is granted, `false` otherwise.
     */
    fun containsScope(scope: String): Boolean {
        val account = authenticatedAccount() ?: return false
        return account.hasScope(scope)
    }

    /** Call this after registering your observers, and before interacting with this class. */
    suspend fun start() =
        withContext(coroutineContext) {
            syncManager?.initialize()

            processQueue(Event.Account.Start)

            if (!isAccountManagerReady) {
                notifyObservers { onReady(authenticatedAccount()) }
                isAccountManagerReady = true
            }
        }

    /**
     * Get the [OAuthAccount] instance if it's not disconnected. Returned [OAuthAccount] may need to be
     * re-authenticated; consumers are expected to check [accountNeedsReauth].
     */
    fun authenticatedAccount(): OAuthAccount? = state.let {
        when (it) {
            FxaState.Connected,
            FxaState.AuthIssues -> account
            // Bug 2030890 - we may not leave the `FxaState.Authenticating` state.
            is FxaState.Authenticating if it.initialState == FxaRustAuthState.CONNECTED -> account
            else -> null
        }
    }

    /** Get the [OAuthAccount] instance if it's connected. */
    fun connectedAccount(): OAuthAccount? = state.let {
        when (it) {
            FxaState.Connected -> account
            // While authenticating from a connected state we should return the account.
            is FxaState.Authenticating if it.initialState == FxaRustAuthState.CONNECTED -> account
            else -> null
        }
    }

    /**
     * Indicates if account needs to be re-authenticated via [beginAuthentication]. Most common reason for an account to
     * need re-authentication is a password change.
     *
     * TODO this may return a false-positive, if we're currently going through a recovery flow. Prefer to be notified of
     * auth problems via [AccountObserver], which is reliable.
     *
     * @return A boolean flag indicating if account needs to be re-authenticated.
     */
    fun accountNeedsReauth() = (state == FxaState.AuthIssues)

    /**
     * Returns a [Profile] for an account, attempting to fetch it if necessary.
     *
     * @return [Profile] if one is available and account is an authenticated state.
     */
    fun accountProfile(): Profile? =
        when (state) {
            is FxaState.Connected,
            is FxaState.Authenticating,
            is FxaState.AuthIssues -> profile
            else -> null
        }

    @VisibleForTesting
    internal suspend fun refreshProfile(ignoreCache: Boolean): Profile? {
        return authenticatedAccount()?.getProfile(ignoreCache = ignoreCache)?.let { newProfile ->
            profile = newProfile
            notifyObservers {
                onProfileUpdated(newProfile)
            }
            profile
        }
    }

    /**
     * Begins an authentication process. Should be finalized by calling [finishAuthentication] once user successfully
     * goes through the authentication at the returned url.
     *
     * @param pairingUrl Optional pairing URL in case a pairing flow is being initiated.
     * @param entrypoint an enum representing the feature entrypoint requesting the URL. the entrypoint is used in
     *   telemetry.
     * @param authScopes The OAuth scopes being requested. When sync decoupling is enabled
     *   ([SyncConfig.syncDecouplingEnabled], this parameter gets ignored. Otherwise, if none are provided we default to
     *   the scopes provided when constructing [FxaAccountManager]
     * @param service The name fo the service being enabled. This may change the UX shown by the FxA content servers.
     * @return An authentication url which is to be presented to the user.
     */
    suspend fun beginAuthentication(
        pairingUrl: String? = null,
        entrypoint: FxAEntryPoint,
        authScopes: Set<String> = scopes,
        service: String,
    ): String? =
        withContext(coroutineContext) {
            // When sync decoupling is enabled, we need to send empty scopes.
            // see https://bugzilla.mozilla.org/show_bug.cgi?id=2066516#c4
            val authScopes = authScopes.orEmptyIf { syncConfig?.syncDecouplingEnabled == true }
            val event =
                if (pairingUrl != null) {
                    Event.Account.BeginPairingFlow(pairingUrl, service, entrypoint, authScopes)
                } else {
                    Event.Account.BeginEmailFlow(service, entrypoint, authScopes)
                }

            // Process the event, then use the new state to check the result of the operation
            processQueue(event)
            when (val s = state) {
                is FxaState.Authenticating -> s.oauthUrl
                else -> null
            }.also { result ->
                if (result == null) {
                    logger.warn("beginAuthentication: error processing next state ($state)")
                }
            }
        }

    /** Stores the session token from a WebChannel login JSON payload without exposing it to the browser layer. */
    suspend fun handleWebChannelLogin(jsonPayload: String) =
        withContext(coroutineContext) {
            account.handleWebChannelLogin(jsonPayload)
        }

    /**
     * Handles the `fxaccounts:change_password` WebChannel payload after the user changes their password from the
     * manage-account flow.
     */
    suspend fun handleWebChannelPasswordChange(jsonPayload: String) =
        withContext(coroutineContext) {
            val wasInAuthIssues = state == FxaState.AuthIssues
            processQueue(Event.Account.WebChannelPasswordChange(jsonPayload))
            if (state == FxaState.Connected) {
                authenticationSideEffects()
                if (wasInAuthIssues) {
                    notifyObservers { onAuthenticated(account, AuthType.Recovered) }
                }
                refreshProfile(ignoreCache = true)
            }
        }

    /**
     * Finalize authentication that was started via [beginAuthentication].
     *
     * If authentication wasn't started via this manager we won't accept this authentication attempt, returning `false`.
     * This may happen if [WebChannelFeature] is enabled, and user is manually logging into accounts.firefox.com in a
     * regular tab.
     *
     * Guiding principle behind this is that logging into accounts.firefox.com should not affect logged-in state of the
     * browser itself, even though the two may have an established communication channel via [WebChannelFeature].
     */
    suspend fun finishAuthentication(authData: FxaAuthData) =
        withContext(coroutineContext) {
            authData.declinedEngines?.let { persistDeclinedEngines(it) }
            processQueue(Event.Progress.AuthData(authData))
            true
        }

    /** Logout of the account, if currently logged-in. */
    suspend fun logout() = withContext(coroutineContext) { processQueue(Event.Account.Logout) }

    /** Register a [AccountEventsObserver] to monitor events relevant to an account/device. */
    fun registerForAccountEvents(observer: AccountEventsObserver, owner: LifecycleOwner, autoPause: Boolean) {
        accountEventObserverRegistry.register(observer, owner, autoPause)
    }

    /** Register a [SyncStatusObserver] to monitor sync activity performed by this manager. */
    fun registerForSyncEvents(observer: SyncStatusObserver, owner: LifecycleOwner, autoPause: Boolean) {
        syncStatusObserverRegistry.register(observer, owner, autoPause)
    }

    /**
     * Unregister a [SyncStatusObserver] from being informed about "sync lifecycle" events. The method is safe to call
     * even if the provided observer was not registered before.
     */
    fun unregisterForSyncEvents(observer: SyncStatusObserver) {
        syncStatusObserverRegistry.unregister(observer)
    }

    override fun close() {
        GlobalAccountManager.close()
        coroutineContext.cancel()
        account.close()
    }

    internal suspend fun encounteredAuthError(
        operation: String,
        errorCountWithinTheTimeWindow: Int = 1,
    ) {
        return withContext(coroutineContext) {
            processQueue(Event.Account.AuthenticationError(operation, errorCountWithinTheTimeWindow))
        }
    }

    /** @return an empty Set if [condition] evaluates to true */
    private inline fun Set<String>.orEmptyIf(condition: () -> Boolean): Set<String> =
        this.takeIf { !condition() }.orEmpty()

    /** Pumps the state machine until all events are processed and their side-effects resolve. */
    private suspend fun processQueue(event: Event) {
        logger.debug("processQueue: event: '$event'")
        var fxaEvent = calcFxaEvent(event)
        if (fxaEvent == null) {
            logger.warn("processQueue: Got invalid event '$event'")
            return
        }
        logger.debug("processQueue: fxaEvent '$fxaEvent'")
        state =
            try {
                account.processEvent(fxaEvent)
            } catch (e: FxaException) {
                logger.warn("processQueue: Error in processEvent: $e")
                return
            }
        logger.debug("processQueue: new FxaState '$state'")
        try {
            accountStateSideEffects(state, event)
        } catch (_: AccountManagerException.AuthenticationSideEffectsFailed) {
            logger.debug("processQueue: handling AuthenticationSideEffectsFailed")
            account.processEvent(FxaEvent.Disconnect)
            accountStateSideEffects(FxaState.Disconnected, Event.Account.Logout)
        }
        logger.debug("processQueue: finished")
    }

    internal fun queueEvent(event: Event) {
        CoroutineScope(coroutineContext).launch {
            processQueue(event)
        }
    }

    private fun calcFxaEvent(event: Event): FxaEvent? =
        when (event) {
            Event.Account.Start ->
                FxaEvent.Initialize(
                    ASDeviceConfig(
                        name = deviceConfig.name,
                        deviceType = deviceConfig.type.into(),
                        capabilities = ArrayList(deviceConfig.capabilities.map { it.into() }),
                    )
                )
            is Event.Account.BeginEmailFlow ->
                FxaEvent.BeginOAuthFlow(
                    service = event.service,
                    scopes = ArrayList(event.scopes),
                    entrypoint = event.entrypoint.entryName,
                )
            is Event.Account.BeginPairingFlow -> {
                if (event.pairingUrl != null) {
                    FxaEvent.BeginPairingFlow(
                        pairingUrl = event.pairingUrl,
                        service = event.service,
                        scopes = ArrayList(event.scopes),
                        entrypoint = event.entrypoint.entryName,
                    )
                } else {
                    crashReporter?.recordCrashBreadcrumb(Breadcrumb("event.pairingUrl is null"))
                    null
                }
            }
            is Event.Account.AuthenticationError -> FxaEvent.CheckAuthorizationStatus
            Event.Account.AccessTokenKeyError -> FxaEvent.CheckAuthorizationStatus
            Event.Account.Logout -> FxaEvent.Disconnect
            is Event.Account.WebChannelPasswordChange ->
                FxaEvent.WebChannelPasswordChange(jsonPayload = event.jsonPayload)
            // This is the one ProgressEvent that's considered a "public event" in app-services
            is Event.Progress.AuthData -> FxaEvent.CompleteOAuthFlow(event.authData.code, event.authData.state)
        }

    /**
     * Side-effects of entering a new FxaState
     *
     * Upon entering these states, observers are typically notified. The sole exception occurs during the completion of
     * authentication, where it is necessary to populate the SyncAuthInfoCache for the background synchronization
     * worker.
     *
     * @throws [AccountManagerException.AuthenticationSideEffectsFailed] if there was a failure to run the side effects
     *   for a newly authenticated account.
     */
    private suspend fun accountStateSideEffects(
        forState: FxaState,
        via: Event,
    ) {
        when (forState) {
            FxaState.Disconnected ->
                when (via) {
                    Event.Account.Logout -> {
                        resetAccount()
                        notifyObservers { onLoggedOut() }
                    }
                    is Event.Account.BeginEmailFlow,
                    is Event.Account.BeginPairingFlow -> {
                        resetAccount()
                        notifyObservers { onFlowError(AuthFlowError.FailedToBeginAuth) }
                    }
                    is Event.Progress.AuthData -> {
                        resetAccount()
                        notifyObservers { onFlowError(AuthFlowError.FailedToCompleteAuth) }
                    }
                    else -> Unit
                }
            FxaState.Connected ->
                when (via) {
                    is Event.Account.Start -> {
                        if (authenticationSideEffects()) {
                            notifyObservers { onAuthenticated(account, AuthType.Existing) }
                            refreshProfile(ignoreCache = false)
                        } else {
                            throw AccountManagerException.AuthenticationSideEffectsFailed()
                        }
                    }
                    is Event.Progress.AuthData -> {
                        if (authenticationSideEffects()) {
                            notifyObservers { onAuthenticated(account, via.authData.authType) }
                            refreshProfile(ignoreCache = false)
                        } else {
                            throw AccountManagerException.AuthenticationSideEffectsFailed()
                        }
                    }
                    is Event.Account.AuthenticationError,
                    Event.Account.AccessTokenKeyError -> {
                        // Should we also call authenticationSideEffects here?
                        // (https://bugzilla.mozilla.org/show_bug.cgi?id=1865086)
                        notifyObservers { onAuthenticated(account, AuthType.Recovered) }
                        refreshProfile(ignoreCache = true)
                    }
                    else -> Unit
                }
            FxaState.AuthIssues -> {
                notifyObservers { onAuthenticationProblems() }
            }
            else -> Unit
        }
    }

    private suspend fun resetAccount() {
        // Clean up resources.
        profile = null
        // Delete persisted state.
        getAccountStorage().clear()
        // Even though we might not have Sync enabled, clear out sync-related storage
        // layers as well; if they're already empty (unused), nothing bad will happen
        // and extra overhead is quite small.
        SyncEnginesStorage(context).clear()
        syncStateStorageProvider.get().clear()
    }

    private fun persistDeclinedEngines(declinedEngines: Set<SyncEngine>) {
        // Sync may not be configured at all (e.g. won't run), but if we received a
        // list of declined engines, that indicates user intent, so we preserve it
        // within SyncEnginesStorage regardless. If sync becomes enabled later on,
        // we will be respecting user choice around what to sync.
        val enginesStorage = SyncEnginesStorage(context)
        declinedEngines.forEach { declinedEngine ->
            enginesStorage.setStatus(declinedEngine, false)
        }

        // Enable all engines that were not explicitly disabled. Only do this in
        // the presence of a "declinedEngines" list, since that indicates user
        // intent. Absence of that list means that user was not presented with a
        // choice during authentication, and so we can't assume an enabled state
        // for any of the engines.
        syncConfig?.supportedEngines?.forEach { supportedEngine ->
            if (!declinedEngines.contains(supportedEngine)) {
                enginesStorage.setStatus(supportedEngine, true)
            }
        }
    }

    private suspend fun finalizeDevice(authType: AuthType) =
        account
            .deviceConstellation()
            .finalizeDevice(
                authType,
                deviceConfig,
            )

    /**
     * Populates caches necessary for the sync worker (sync auth info and FxA device).
     *
     * @return 'true' on success, 'false' on failure, indicating that sync won't work.
     */
    private suspend fun authenticationSideEffects(): Boolean {
        // Sync workers also need to know about the current FxA device.
        FxaDeviceSettingsCache(context)
            .setToCache(
                DeviceSettings(
                    fxaDeviceId = account.getCurrentDeviceId()!!,
                    name = deviceConfig.name,
                    kind = deviceConfig.type.into(),
                )
            )
        return true
    }

    /** Exists strictly for testing purposes, allowing tests to specify their own implementation of [OAuthAccount]. */
    @VisibleForTesting
    open fun getStorageWrapper(): StorageWrapper {
        return StorageWrapper(this, accountEventObserverRegistry, serverConfig, crashReporter)
    }

    @VisibleForTesting
    internal open fun createSyncManager(config: SyncConfig): SyncManager {
        return WorkManagerSyncManager(
            context = context,
            syncConfig = config,
            syncStateStorageProvider = syncStateStorageProvider,
            coroutineContext = coroutineContext,
        )
    }

    internal open fun getAccountStorage(): AccountStorage {
        return if (deviceConfig.secureStateAtRest) {
            SecureAbove22AccountStorage(context, crashReporter)
        } else {
            SharedPrefAccountStorage(context, crashReporter)
        }
    }

    /** Account status events flowing into the sync manager. */
    @VisibleForTesting
    internal class AccountsToSyncIntegration(private val syncManager: SyncManager) : AccountObserver {
        override fun onLoggedOut() {
            syncManager.stop()
        }

        override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
            val reason =
                when (authType) {
                    is AuthType.OtherExternal,
                    AuthType.Signin,
                    AuthType.Signup,
                    AuthType.MigratedReuse,
                    AuthType.MigratedCopy,
                    AuthType.Pairing -> SyncReason.FirstSync
                    AuthType.Existing,
                    AuthType.Recovered -> SyncReason.Startup
                }
            syncManager.start()
            syncManager.now(reason)
        }

        override fun onProfileUpdated(profile: Profile) {
            // Sync currently doesn't care about the FxA profile.
            // In the future, we might kick-off an immediate sync here.
        }

        override fun onAuthenticationProblems() {
            emitSyncFailedFact()
            syncManager.stop()
        }
    }

    /** Sync status changes flowing into account manager. */
    private class SyncToAccountsIntegration(private val accountManager: FxaAccountManager) : SyncStatusObserver {
        override fun onStarted() {
            accountManager.syncStatusObserverRegistry.notifyObservers { onStarted() }
        }

        override fun onIdle() {
            accountManager.syncStatusObserverRegistry.notifyObservers { onIdle() }
        }

        override fun onError(error: Exception?) {
            accountManager.syncStatusObserverRegistry.notifyObservers { onError(error) }
        }
    }

    /**
     * Hook this up to the secret debug menu to simulate a network error
     *
     * Typical usage is:
     * - `adb logcat | grep fxa_client`
     * - Trigger this via the secret debug menu item.
     * - Watch the logs. You should see the client perform a call to `get_profile', see a network error, then recover.
     *     - Note: the logs will be more clear once we switch the code to using the app-services state machine.
     * - Check the UI, it should be in an authenticated state.
     */
    public fun simulateNetworkError() {
        account.simulateNetworkError()
        CoroutineScope(coroutineContext).launch {
            refreshProfile(true)
        }
    }

    /**
     * Hook this up to the secret debug menu to simulate a temporary auth error
     *
     * Typical usage is:
     * - `adb logcat | grep fxa_client`
     * - Trigger this via the secret debug menu item.
     * - Watch the logs. You should see the client perform a call to `get_profile', see an auth error, then recover.
     * - Check the UI, it should be in an authenticated state.
     */
    public fun simulateTemporaryAuthTokenIssue() {
        account.simulateTemporaryAuthTokenIssue()
        CoroutineScope(coroutineContext).launch {
            refreshProfile(true)
        }
    }

    /**
     * Hook this up to the secret debug menu to simulate an unrecoverable auth error
     *
     * Typical usage is:
     * - `adb logcat | grep fxa_client`
     * - Trigger this via the secret debug menu item.
     * - Initiaite a sync, or perform some other action that requires authentication.
     * - Watch the logs. You should see the client perform a call to `get_profile', see an auth error, then fail to
     *   recover.
     * - Check the UI, it should be in an authentication problems state.
     */
    public fun simulatePermanentAuthTokenIssue() {
        account.simulatePermanentAuthTokenIssue()
        CoroutineScope(coroutineContext).launch {
            refreshProfile(true)
        }
    }
}

internal class AccountStateEventsObserver(internal val queueEvent: (Event) -> Unit) : AccountEventsObserver {
    private val logger = Logger("FxA AccountStateEventsObserver")

    override fun onEvents(events: List<AccountEvent>) {
        for (event in events) {
            when (event) {
                AccountEvent.AccountAuthStateChanged -> {
                    logger.info("Saw AccountAuthStateChanged")
                    queueEvent(Event.Account.AuthenticationError("AccountAuthStateChanged event"))
                }
                AccountEvent.AccountDestroyed -> {
                    logger.info("Saw AccountDestroyed")
                    queueEvent(Event.Account.Logout)
                }
                else -> Unit
            }
        }
    }
}
