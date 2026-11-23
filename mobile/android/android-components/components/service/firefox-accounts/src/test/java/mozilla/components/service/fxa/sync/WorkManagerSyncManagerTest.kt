/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa.sync

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.Configuration
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.WorkQuery
import androidx.work.WorkerParameters
import androidx.work.impl.utils.taskexecutor.TaskExecutor
import androidx.work.testing.SynchronousExecutor
import androidx.work.testing.WorkManagerTestInitHelper
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNotEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.appservices.sync15.DeviceType
import mozilla.appservices.syncmanager.DeviceSettings
import mozilla.appservices.syncmanager.ServiceStatus
import mozilla.appservices.syncmanager.SyncReason as RustSyncReason
import mozilla.appservices.syncmanager.SyncResult
import mozilla.components.concept.sync.AccessTokenInfo
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthScopedKey
import mozilla.components.concept.sync.PeriodicSyncConfig
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.service.fxa.FxaDeviceSettingsCache
import mozilla.components.service.fxa.TestOAuthAccount
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.GlobalAccountManager
import mozilla.components.service.fxa.manager.SCOPE_SYNC
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.service.fxa.sync.WorkManagerSyncWorker.Companion.SYNC_STAGGER_BUFFER_MS
import mozilla.components.service.fxa.sync.WorkManagerSyncWorker.Companion.engineSyncTimestamp
import mozilla.components.service.fxa.sync.helpers.TestSyncStateStorage
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`

@OptIn(ExperimentalCoroutinesApi::class) // TestScope.runCurrent
@RunWith(AndroidJUnit4::class)
class WorkManagerSyncManagerTest {
    private lateinit var mockParam: WorkerParameters
    private lateinit var mockTags: Set<String>
    private lateinit var mockTaskExecutor: TaskExecutor

    private val defaultSyncConfig =
        SyncConfig(
            supportedEngines = setOf(SyncEngine.Tabs),
            periodicSyncConfig = null,
            syncDecouplingEnabled = false,
        )
    private val syncConfigWithDecoupling =
        SyncConfig(
            supportedEngines = setOf(SyncEngine.Tabs),
            periodicSyncConfig = PeriodicSyncConfig(),
            syncDecouplingEnabled = true,
        )
    private val testDispatcher = StandardTestDispatcher()
    private val accountManager = mock<FxaAccountManager>()

    @Before
    fun setUp() {
        mockParam = mock()
        mockTags = mock()
        mockTaskExecutor = mock()
        `when`(mockParam.taskExecutor).thenReturn(mockTaskExecutor)
        `when`(mockTaskExecutor.serialTaskExecutor).thenReturn(mock())
        `when`(mockParam.tags).thenReturn(mockTags)

        GlobalAccountManager.setInstance(accountManager)

        WorkManagerTestInitHelper.initializeTestWorkManager(
            testContext,
            Configuration.Builder().setExecutor(SynchronousExecutor()).build(),
        )
    }

    @After
    fun tearDown() {
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `GIVEN work is not set to be debounced THEN it is not considered to be synced within the buffer`() {
        `when`(mockTags.contains(SyncWorkerTag.Debounce.name)).thenReturn(false)

        engineSyncTimestamp["test"] = System.currentTimeMillis() - SYNC_STAGGER_BUFFER_MS - 100L
        engineSyncTimestamp["test2"] = System.currentTimeMillis()

        val workerManagerSyncWorker = WorkManagerSyncWorker(testContext, mockParam)

        assertFalse(workerManagerSyncWorker.isDebounced())
        assertFalse(workerManagerSyncWorker.lastSyncedWithinStaggerBuffer("test"))
        assertFalse(workerManagerSyncWorker.lastSyncedWithinStaggerBuffer("test2"))
    }

    @Test
    fun `GIVEN work is set to be debounced THEN last synced timestamp is compared to buffer`() {
        `when`(mockTags.contains(SyncWorkerTag.Debounce.name)).thenReturn(true)

        engineSyncTimestamp["test"] = System.currentTimeMillis() - SYNC_STAGGER_BUFFER_MS - 100L
        engineSyncTimestamp["test2"] = System.currentTimeMillis()

        val workerManagerSyncWorker = WorkManagerSyncWorker(testContext, mockParam)

        assert(workerManagerSyncWorker.isDebounced())
        assertFalse(workerManagerSyncWorker.lastSyncedWithinStaggerBuffer("test"))
        assert(workerManagerSyncWorker.lastSyncedWithinStaggerBuffer("test2"))
    }

    @Test
    fun `isWithinStaggerBuffer is true just inside the buffer and false at or beyond it`() {
        val now = 1_000_000L
        assertTrue(isWithinStaggerBuffer(lastSyncedMs = now - (SYNC_STAGGER_BUFFER_MS - 1), now = now))
        assertFalse(isWithinStaggerBuffer(lastSyncedMs = now - SYNC_STAGGER_BUFFER_MS, now = now))
    }

    @Test
    fun `GIVEN work is set to be debounced WHEN there is not a saved time stamp THEN work will not be debounced`() {
        `when`(mockTags.contains(SyncWorkerTag.Debounce.name)).thenReturn(true)

        val workerManagerSyncWorker = WorkManagerSyncWorker(testContext, mockParam)

        assert(workerManagerSyncWorker.isDebounced())
        assertFalse(workerManagerSyncWorker.lastSyncedWithinStaggerBuffer("test"))
    }

    @Test
    fun `WHEN workerStateChanged receives null state THEN nothing happens`() =
        runTest(testDispatcher) {
            val observer = FakeSyncStatusObserver()
            val syncManager = createSyncManager()

            syncManager.syncDispatcher?.workersStateChanged(null)

            assertTrue(observer.events.isEmpty())
            assertFalse(syncManager.isSyncActive())
        }

    @Test
    fun `WHEN workerStateChanged receives empty list THEN nothing happens`() =
        runTest(testDispatcher) {
            val observer = FakeSyncStatusObserver()
            val syncManager = createSyncManager(observer = observer)

            syncManager.syncDispatcher?.workersStateChanged(emptyList())

            assertTrue(observer.events.isEmpty())
            assertFalse(syncManager.isSyncActive())
        }

    @Test
    fun `WHEN workerStateChanged receives ENQUEUED state THEN nothing happens`() =
        runTest(testDispatcher) {
            val observer = FakeSyncStatusObserver()
            val syncManager = createSyncManager(observer = observer)

            syncManager.syncDispatcher?.workersStateChanged(listOf(WorkInfo.State.ENQUEUED))

            assertTrue(observer.events.isEmpty())
            assertFalse(syncManager.isSyncActive())
        }

    @Test
    fun `GIVEN sync decoupling is off, and an authenticated account exists, THEN connection state reports connected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())
            val syncManager = createSyncManager(syncConfig = defaultSyncConfig)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Connected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN sync decoupling is off, and no authenticated account exists, THEN connection state reports disconnected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(null)
            val syncManager = createSyncManager(syncConfig = defaultSyncConfig)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Disconnected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN sync decoupling is off, an authenticated account exists, and sync was disconnected, THEN the connection state still reports connected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())

            val syncStateStorage = TestSyncStateStorage()
            syncStateStorage.storeSyncConnected(connected = false)
            val syncManager = createSyncManager(syncConfig = defaultSyncConfig, syncStateStorage = syncStateStorage)

            syncManager.initialize()
            runCurrent()

            assertEquals(
                SyncConnectionState.Connected,
                syncManager.syncConnectionState.value,
                "Sync should be connected without sync decoupling when there is an authenticated account",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and no authenticated account exists, THEN connection state reports disconnected`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(null)

            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Disconnected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN sync decoupling is on, an authenticated account exists, and sync state was never set, THEN connection state reports connected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())
            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Connected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN sync decoupling is on, an authenticated account exists, and sync was disconnected, THEN connection state reports disconnected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())

            val syncStateStorage = TestSyncStateStorage()
            syncStateStorage.storeSyncConnected(connected = false)
            val syncManager =
                createSyncManager(syncConfig = syncConfigWithDecoupling, syncStateStorage = syncStateStorage)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Disconnected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN sync decoupling is on, an authenticated account exists, and sync was connected, THEN connection state reports connected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())

            val syncStateStorage = TestSyncStateStorage()
            syncStateStorage.storeSyncConnected(connected = true)
            val syncManager =
                createSyncManager(syncConfig = syncConfigWithDecoupling, syncStateStorage = syncStateStorage)

            syncManager.initialize()
            runCurrent()

            assertEquals(SyncConnectionState.Connected, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN the manager has not been initialized, THEN connection state reports uninitialized`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())

            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)
            runCurrent()

            assertEquals(SyncConnectionState.Uninitialized, syncManager.syncConnectionState.value)
        }

    @Test
    fun `GIVEN no connected account exists, WHEN connect is called, THEN the ConnectResult is NeedsAuthentication`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(null)

            val syncManager = createSyncManager()
            syncManager.initialize()
            runCurrent()

            val result = syncManager.connect()

            assertEquals(ConnectResult.Failure.NeedsAuthentication, result)
        }

    @Test
    fun `GIVEN sync decoupling is on, and connected account does not have sync scope, WHEN connect is called, THEN ConnectResult is NeedsSyncAuthorization`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(TestAccount(initialScopes = setOf("random_scope")))

            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)
            syncManager.initialize()
            runCurrent()

            val result = syncManager.connect()

            assertEquals(ConnectResult.Failure.NeedsSyncAuthorization, result)
        }

    @Test
    fun `GIVEN sync decoupling is on, and connected account has sync scope, WHEN connect succeeds, THEN storage reflects that sync is connected`() =
        runTest(testDispatcher) {
            val account = TestAccount(initialScopes = setOf(SCOPE_SYNC))
            whenever(accountManager.connectedAccount()).thenReturn(account)
            whenever(accountManager.authenticatedAccount()).thenReturn(account)

            val syncStateStorage = TestSyncStateStorage()
            val syncManager =
                createSyncManager(
                    syncConfig = syncConfigWithDecoupling,
                    syncStateStorage = syncStateStorage,
                )
            syncManager.initialize()
            runCurrent()

            syncManager.connect()

            val syncConnectedInStorage = syncStateStorage.syncConnected
            assertNotNull(syncConnectedInStorage, "Sync connected state in storage should not be null")
            assertTrue(syncConnectedInStorage, "Sync connected should be true in storage")
        }

    @Test
    fun `GIVEN sync decoupling is on, WHEN connect succeeds, THEN connection state reports connected ok`() =
        runTest(testDispatcher) {
            // connected account with sync scope exists to give a successful `connect()`
            val account = TestAccount(initialScopes = setOf(SCOPE_SYNC))
            whenever(accountManager.connectedAccount()).thenReturn(account)
            whenever(accountManager.authenticatedAccount()).thenReturn(account)

            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)
            syncManager.initialize()
            syncManager.connect()
            runCurrent()

            assertEquals(
                SyncConnectionState.Connected,
                syncManager.syncConnectionState.value,
                "Sync should be connected",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and sync previously disconnected, WHEN connect succeeds, THEN connection state reports connected`() =
        runTest(testDispatcher) {
            val account = TestAccount()
            whenever(accountManager.connectedAccount()).thenReturn(account)
            whenever(accountManager.authenticatedAccount()).thenReturn(account)

            val syncStateStorage = TestSyncStateStorage()
            syncStateStorage.storeSyncConnected(connected = false)

            val syncManager =
                createSyncManager(
                    syncStateStorage = syncStateStorage,
                    syncConfig = syncConfigWithDecoupling,
                )
            syncManager.initialize()
            syncManager.connect()
            runCurrent()

            assertEquals(
                SyncConnectionState.Connected,
                syncManager.syncConnectionState.value,
                "Sync should be connected",
            )
        }

    @Test
    fun `GIVEN connected account with sync scope, WHEN connect is called, THEN first sync is initiated`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(TestAccount(initialScopes = setOf(SCOPE_SYNC)))

            val testRustSyncManager = TestRustSyncManager().apply { expectSuccessfulSync() }
            val syncManager =
                createSyncManager(
                    testRustSyncManager = testRustSyncManager,
                    syncConfig = defaultSyncConfig,
                    start = false,
                )
            syncManager.initialize()
            runCurrent()

            syncManager.connect()

            val workManager = WorkManager.getInstance(testContext)
            val immediateWork =
                workManager.getWorkInfos(WorkQuery.fromUniqueWorkNames(SyncWorkerName.Immediate.name)).get()
            assertEquals(1, immediateWork.size, "Unexpected count of immediate sync work")

            runSyncWorkers(tags = listOf(SyncWorkerTag.Common.name))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(RustSyncReason.ENABLED_CHANGE, testRustSyncManager.requestedSyncParams.reason)
        }

    @Test
    fun `GIVEN pending startup sync, WHEN connect is called, THEN first sync replaces it`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(TestAccount(initialScopes = setOf(SCOPE_SYNC)))
            val testRustSyncManager = TestRustSyncManager().apply { expectSuccessfulSync() }
            val syncManager = createSyncManager(testRustSyncManager = testRustSyncManager)
            syncManager.initialize()
            runCurrent()
            syncManager.now(SyncReason.Startup)
            val workManager = WorkManager.getInstance(testContext)
            val startupWork = workManager.getWorkInfosForUniqueWork(SyncWorkerName.Immediate.name).get().single()

            syncManager.connect()

            assertNull(workManager.getWorkInfoById(startupWork.id).get())
            val firstWork = workManager.getWorkInfosForUniqueWork(SyncWorkerName.Immediate.name).get().single()
            assertNotEquals(startupWork.id, firstWork.id)

            runSyncWorkers(tags = listOf(SyncWorkerTag.Common.name))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(RustSyncReason.ENABLED_CHANGE, testRustSyncManager.requestedSyncParams.reason)
        }

    @Test
    fun `GIVEN connected account with sync scope and no periodic sync configured, WHEN connect succeeds, THEN no periodic sync is initiated`() =
        runTest(testDispatcher) {
            whenever(accountManager.connectedAccount()).thenReturn(TestAccount(initialScopes = setOf(SCOPE_SYNC)))

            val syncManager =
                createSyncManager(
                    syncConfig = syncConfigWithDecoupling.copy(periodicSyncConfig = null),
                    start = true,
                )
            syncManager.initialize()
            runCurrent()

            syncManager.connect()

            val periodicWork =
                WorkManager.getInstance(testContext)
                    .getWorkInfos(WorkQuery.fromUniqueWorkNames(SyncWorkerName.Periodic.name))
                    .get()
            assertTrue(periodicWork.isEmpty(), "Expected no periodic sync work")
        }

    @Test
    fun `GIVEN sync decoupling is on, WHEN disconnect is called, THEN rust sync manager is disconnected`() =
        runTest(testDispatcher) {
            val testRustSyncManager = TestRustSyncManager()
            val syncManager =
                createSyncManager(syncConfig = syncConfigWithDecoupling, testRustSyncManager = testRustSyncManager)
            syncManager.connect()

            assertTrue(
                testRustSyncManager.isConnected,
                "Rust sync manager should be connected before we call disconnect",
            )

            syncManager.disconnect()

            assertFalse(testRustSyncManager.isConnected, "Rust sync manager should be disconnected")
        }

    @Test
    fun `GIVEN sync decoupling is on, and sync is previously connected, WHEN disconnect is called, THEN all sync work is cancelled`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())
            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling, start = true)
            syncManager.initialize()
            syncManager.connect()
            runCurrent()

            syncManager.disconnect()
            runCurrent()

            val syncWorkInfos =
                WorkManager.getInstance(testContext)
                    .getWorkInfos(WorkQuery.Builder.fromTags(listOf(SyncWorkerTag.Common.name)).build())
                    .get()

            assertFalse(syncWorkInfos.isEmpty(), "Expected some sync work")

            val notCancelled = syncWorkInfos.filter { it.state != WorkInfo.State.CANCELLED }
            assertEquals(emptyList(), notCancelled, "Expected no uncancelled sync work")
        }

    @Test
    fun `GIVEN sync decoupling is on, and sync is previously connected, WHEN disconnect is called, THEN sync engines storage is cleared`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())

            val syncEnginesStorage = SyncEnginesStorage(testContext)
            val syncManager =
                createSyncManager(
                    syncEnginesStorage = syncEnginesStorage,
                    syncConfig = syncConfigWithDecoupling,
                    start = true,
                )
            syncManager.initialize()
            syncManager.connect()
            syncManager.setEngineEnabled(engine = SyncEngine.Tabs, enabled = true)
            runCurrent()

            syncManager.disconnect()

            val engines = syncEnginesStorage.getStatus()
            assertTrue(engines.isEmpty(), "Expected no sync engines after disconnect. Got: $engines")
        }

    @Test
    fun `GIVEN sync decoupling is on, and sync is previously connected, WHEN disconnect is called, THEN sync storage is reset`() =
        runTest(testDispatcher) {
            val account = TestAccount()
            whenever(accountManager.authenticatedAccount()).thenReturn(account)
            whenever(accountManager.connectedAccount()).thenReturn(account)

            val testRustSyncManager =
                TestRustSyncManager().apply {
                    expectSuccessfulSync()
                }
            val syncStateStorage = TestSyncStateStorage()
            val syncConfig = syncConfigWithDecoupling
            val syncManager =
                createSyncManager(
                    syncConfig = syncConfig,
                    syncStateStorage = syncStateStorage,
                    testRustSyncManager = testRustSyncManager,
                    start = false,
                )

            syncManager.connect()

            runSyncWorkers(tags = listOf(SyncWorkerTag.Common.name))

            testDispatcher.scheduler.advanceUntilIdle()

            syncManager.disconnect()

            assertNull(
                syncStateStorage.persistedSyncState,
                "Sync storage persisted sync state should be null after disconnect",
            )
            assertNull(syncStateStorage.lastSynced, "Sync storage last synced should be null after disconnect")
            assertFalse(
                syncStateStorage.syncConnected!!,
                "Sync storage sync connected should be false after disconnect",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and sync is previously connected, WHEN disconnect is called, THEN connection state reports disconnected`() =
        runTest(testDispatcher) {
            val account = TestAccount(initialScopes = setOf(SCOPE_SYNC))
            whenever(accountManager.authenticatedAccount()).thenReturn(account)
            whenever(accountManager.connectedAccount()).thenReturn(account)

            // initialize and connect sync
            val syncManager =
                createSyncManager(
                    syncConfig = syncConfigWithDecoupling,
                    start = false,
                )
            syncManager.initialize()
            runCurrent()

            // start observing after initialization
            val syncConnectionStates = mutableListOf<SyncConnectionState>()
            backgroundScope.launch {
                syncManager.syncConnectionState.collect { syncConnectionStates.add(it) }
            }

            syncManager.connect()
            runCurrent()

            syncManager.disconnect()
            runCurrent()

            assertEquals(
                listOf(
                    SyncConnectionState.Connected,
                    SyncConnectionState.Disconnected,
                ),
                syncConnectionStates,
                "Sync should first be connected, and then disconnected",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and no authenticated account exists, WHEN an account is authenticated, THEN connection state reports connected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(null)
            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)

            syncManager.initialize()
            runCurrent()

            val syncConnectionStates = mutableListOf<SyncConnectionState>()
            backgroundScope.launch {
                syncManager.syncConnectionState.collect { syncConnectionStates.add(it) }
            }

            // when the account is authenticated
            val account = TestAccount()
            whenever(accountManager.authenticatedAccount()).thenReturn(account)
            registeredAccountObserver().onAuthenticated(account, AuthType.Signin)
            runCurrent()

            assertEquals(
                listOf(SyncConnectionState.Disconnected, SyncConnectionState.Connected),
                syncConnectionStates,
                "Sync should first be disconnected, and then connected",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and an authenticated account exists, WHEN the account encounters authentication problems, THEN connection state remains connected`() =
        runTest(testDispatcher) {
            val account = TestAccount()
            whenever(accountManager.authenticatedAccount()).thenReturn(account)
            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)

            syncManager.initialize()
            runCurrent()

            // when the account encounters authentication problems
            whenever(accountManager.connectedAccount()).thenReturn(null)
            registeredAccountObserver().onAuthenticationProblems()
            runCurrent()

            assertEquals(
                SyncConnectionState.Connected,
                syncManager.syncConnectionState.value,
                "Sync should be connected",
            )
        }

    @Test
    fun `GIVEN sync decoupling is on, and an authenticated account exists, WHEN the account logs out, THEN connection state reports disconnected`() =
        runTest(testDispatcher) {
            whenever(accountManager.authenticatedAccount()).thenReturn(TestAccount())
            val syncManager = createSyncManager(syncConfig = syncConfigWithDecoupling)

            syncManager.initialize()
            runCurrent()

            assertIs<SyncConnectionState.Connected>(
                syncManager.syncConnectionState.value,
                "Sync should be connected to begin with",
            )

            // when the account logs out
            whenever(accountManager.authenticatedAccount()).thenReturn(null)
            registeredAccountObserver().onLoggedOut()
            runCurrent()

            assertEquals(SyncConnectionState.Disconnected, syncManager.syncConnectionState.value)
        }

    /** Sets up the rust sync manager to report a successful sync of [SyncEngine.Tabs]. */
    private fun TestRustSyncManager.expectSuccessfulSync() {
        expectedResult =
            SyncResult(
                status = ServiceStatus.OK,
                successful = listOf(SyncEngine.Tabs.nativeName),
                failures = emptyMap(),
                persistedState = "{\"foo\": \"bar\"}",
                declined = null,
                nextSyncAllowedAt = null,
                telemetryJson = null,
            )
    }

    /** Meets the constraints of every queued sync worker, so that the work is allowed to run. */
    private fun runSyncWorkers(tags: List<String>) {
        val driver = WorkManagerTestInitHelper.getTestDriver(testContext) ?: return
        WorkManager.getInstance(testContext).getWorkInfos(WorkQuery.Builder.fromTags(tags).build()).get().forEach {
            driver.setAllConstraintsMet(it.id)
        }
    }

    /** Returns the [AccountObserver] the manager registered while initializing. */
    private fun registeredAccountObserver(): AccountObserver {
        val observer = argumentCaptor<AccountObserver>()
        verify(accountManager).register(observer.capture())
        return observer.value
    }

    private fun createSyncManager(
        observer: FakeSyncStatusObserver = FakeSyncStatusObserver(),
        syncStateStorage: SyncStateStorage = TestSyncStateStorage(),
        syncEnginesStorage: SyncEnginesStorage = SyncEnginesStorage(testContext),
        testRustSyncManager: RustSyncManager = TestRustSyncManager(),
        syncConfig: SyncConfig = defaultSyncConfig,
        syncStateStorageProvider: SyncStateStorage.Provider = SyncStateStorage.Provider { syncStateStorage },
        start: Boolean = true,
    ): WorkManagerSyncManager {
        GlobalAccountManager.syncIoDispatcher = testDispatcher
        GlobalAccountManager.setRustSyncManager(testRustSyncManager)
        GlobalAccountManager.setSyncStateStorageProvider(syncStateStorageProvider)
        GlobalSyncableStoreProvider.registerTestSyncStores(syncConfig)

        FxaDeviceSettingsCache(testContext)
            .setToCache(
                DeviceSettings(
                    fxaDeviceId = "test-device",
                    name = "test device",
                    kind = DeviceType.MOBILE,
                )
            )

        return WorkManagerSyncManager(
                context = testContext,
                syncConfig = syncConfig,
                rustSyncManager = testRustSyncManager,
                syncStateStorageProvider = syncStateStorageProvider,
                syncEnginesStorage = syncEnginesStorage,
                coroutineContext = testDispatcher,
            )
            .apply {
                registerSyncStatusObserver(observer)
                if (start) start()
            }
    }

    private fun GlobalSyncableStoreProvider.registerTestSyncStores(syncConfig: SyncConfig) {
        syncConfig.supportedEngines.forEach { engine ->
            configureStore(
                storePair =
                    engine to
                        lazy {
                            object : SyncableStore {
                                override fun registerWithSyncManager() {}
                            }
                        }
            )
        }
    }

    private class TestAccount(initialScopes: Set<String> = setOf(SCOPE_SYNC)) : TestOAuthAccount() {
        private val grantedScopes: MutableSet<String> = initialScopes.toMutableSet()

        override fun hasScope(scope: String): Boolean = scope in grantedScopes

        override suspend fun getAccessToken(singleScope: String): AccessTokenInfo {
            return AccessTokenInfo(
                scope = SCOPE_SYNC,
                token = "token",
                key =
                    OAuthScopedKey(
                        kty = "kty",
                        scope = SCOPE_SYNC,
                        kid = "kid",
                        k = "k",
                    ),
                expiresAt = 15L,
            )
        }

        override suspend fun getTokenServerEndpointURL(): String {
            return "token server url"
        }
    }
}
