/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxrelay.eligibility

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.sync.AccessTokenInfo
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AttachedClient
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.DeviceConstellation
import mozilla.components.concept.sync.DeviceType
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.Profile
import mozilla.components.concept.sync.StatePersistenceCallback
import mozilla.components.service.fxrelay.EmailMask
import mozilla.components.service.fxrelay.FxRelay
import mozilla.components.service.fxrelay.MaskSource
import mozilla.components.service.fxrelay.RelayAccountDetails
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

class RelayFeatureTest {

    private val testDispatcher = StandardTestDispatcher()
    private lateinit var store: RelayEligibilityStore
    private lateinit var accountManager: FakeAccountManagerDelegate
    private lateinit var fakeFxRelay: FakeFxRelay

    @Before
    fun setup() {
        store = RelayEligibilityStore()
        accountManager = FakeAccountManagerDelegate()
        fakeFxRelay = FakeFxRelay()
    }

    // start() tests
    @Test
    fun `GIVEN not authenticated WHEN start THEN store has not logged in state`() =
        runTest(testDispatcher) {
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Ineligible.FirefoxAccountNotLoggedIn, store.state.eligibilityState)
        }

    @Test
    fun `GIVEN authenticated account without relay service WHEN start THEN store has no relay state`() =
        runTest(testDispatcher) {
            accountManager.account = FakeOAuthAccount()
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Ineligible.NoRelay, store.state.eligibilityState)
        }

    @Test
    fun `GIVEN started WHEN start called again THEN observer is only registered once`() =
        runTest(testDispatcher) {
            val feature = createFeature()

            feature.start()
            feature.start()

            assertEquals(1, accountManager.registerCallCount)
        }

    // stop() tests
    @Test
    fun `GIVEN started WHEN stop THEN observer is unregistered`() = runTest(testDispatcher) {
        val feature = createFeature()
        feature.start()

        feature.stop()

        assertEquals(1, accountManager.unregisterCallCount)
    }

    @Test
    fun `GIVEN not started WHEN stop THEN observer is not unregistered`() {
        val feature = createFeature()

        feature.stop()

        assertEquals(0, accountManager.unregisterCallCount)
    }

    @Test
    fun `GIVEN started and stopped WHEN started again THEN observer registered again`() =
        runTest(testDispatcher) {
            val feature = createFeature()
            feature.start()
            feature.stop()

            feature.start()

            assertEquals(2, accountManager.registerCallCount)
        }

    // Account observer tests
    @Test
    fun `GIVEN started WHEN onAuthenticated THEN store has no relay state`() =
        runTest(testDispatcher) {
            val feature = createFeature()
            feature.start()

            accountManager.observers.first().onAuthenticated(FakeOAuthAccount(), AuthType.Signin)

            assertEquals(Ineligible.NoRelay, store.state.eligibilityState)
        }

    @Test
    fun `GIVEN started WHEN onProfileUpdated THEN eligibility state unchanged`() =
        runTest(testDispatcher) {
            val feature = createFeature()
            feature.start()

            val fakeProfile = Profile(
                uid = "testUID",
                email = "test@example.com",
                avatar = null,
                displayName = null,
            )
            accountManager.observers.first().onProfileUpdated(fakeProfile)

            assertEquals(Ineligible.FirefoxAccountNotLoggedIn, store.state.eligibilityState)
        }

    @Test
    fun `GIVEN started and logged in WHEN onLoggedOut THEN store has not logged in state`() =
        runTest(testDispatcher) {
            accountManager.account = FakeOAuthAccount()
            val feature = createFeature()
            feature.start()

            accountManager.observers.first().onLoggedOut()

            assertEquals(Ineligible.FirefoxAccountNotLoggedIn, store.state.eligibilityState)
        }

    // fetchEmailMasks tests
    @Test
    fun `GIVEN authenticated with relay service WHEN fetchEmailMasks THEN returns masks from relay`() =
        runTest(testDispatcher) {
            val masks = listOf(EmailMask("test@relay.firefox.com", MaskSource.GENERATED))
            fakeFxRelay.emailMasks = masks
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val feature = createFeature()
            feature.start()

            assertEquals(masks, feature.fetchEmailMasks())
        }

    @Test
    fun `GIVEN not authenticated WHEN fetchEmailMasks THEN returns null`() =
        runTest(testDispatcher) {
            val feature = createFeature()
            feature.start()

            assertNull(feature.fetchEmailMasks())
        }

    // getOrCreateNewMask tests
    @Test
    fun `GIVEN authenticated with relay service WHEN getOrCreateNewMask THEN returns mask and updates store`() =
        runTest(testDispatcher) {
            val mask = EmailMask("test@relay.firefox.com", MaskSource.GENERATED)
            fakeFxRelay.createdMask = mask
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val feature = createFeature()
            feature.start()

            val result = feature.getOrCreateNewMask("example.com", "Test")

            assertEquals(mask, result)
            assertEquals(mask, store.state.lastUsed)
        }

    @Test
    fun `GIVEN not authenticated WHEN getOrCreateNewMask THEN returns null and dispatches null lastUsed`() =
        runTest(testDispatcher) {
            val feature = createFeature()
            feature.start()

            val result = feature.getOrCreateNewMask("example.com", "Test")

            assertNull(result)
            assertNull(store.state.lastUsed)
        }

    // checkRelayStatus tests
    @Test
    fun `GIVEN logged in with relay service and free tier WHEN status checked THEN store updated with free tier`() =
        runTest(testDispatcher) {
            fakeFxRelay.accountDetails = RelayAccountDetails(RelayPlanTier.FREE, 3)
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Eligible.Free(3), store.state.eligibilityState)
            assertNotEquals(NO_ENTITLEMENT_CHECK_YET_MS, store.state.lastEntitlementCheckMs)
        }

    @Test
    fun `GIVEN logged in with relay service and premium tier WHEN status checked THEN store updated with premium tier`() =
        runTest(testDispatcher) {
            fakeFxRelay.accountDetails = RelayAccountDetails(RelayPlanTier.PREMIUM, 0)
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Eligible.Premium, store.state.eligibilityState)
        }

    @Test
    fun `GIVEN logged in without relay service WHEN start THEN entitlement check not performed`() =
        runTest(testDispatcher) {
            accountManager.account = FakeOAuthAccount()
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Ineligible.NoRelay, store.state.eligibilityState)
            assertEquals(NO_ENTITLEMENT_CHECK_YET_MS, store.state.lastEntitlementCheckMs)
        }

    @Test
    fun `GIVEN logged in with relay service but fetch fails WHEN status checked THEN entitlement check timestamp is updated`() =
        runTest(testDispatcher) {
            fakeFxRelay.accountDetails = null
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val feature = createFeature()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Ineligible.NoRelay, store.state.eligibilityState)
            assertNotEquals(NO_ENTITLEMENT_CHECK_YET_MS, store.state.lastEntitlementCheckMs)
        }

    @Test
    fun `GIVEN recently checked WHEN state changes THEN entitlement check is not re-triggered`() =
        runTest(testDispatcher) {
            fakeFxRelay.accountDetails = RelayAccountDetails(RelayPlanTier.FREE, 1)
            accountManager.account = FakeOAuthAccount(listOf(createRelayAttachedClient()))
            val recentCheckTimeStamp = System.currentTimeMillis()
            val localStore = RelayEligibilityStore(
                initialState = RelayState(
                    eligibilityState = Ineligible.NoRelay,
                    lastEntitlementCheckMs = recentCheckTimeStamp,
                ),
            )
            val feature = RelayFeature(
                accountManager = accountManager,
                store = localStore,
                fetchTimeoutMs = FETCH_TIMEOUT_MS,
                mainDispatcher = testDispatcher,
                fxRelayFactory = { fakeFxRelay },
            )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(recentCheckTimeStamp, localStore.state.lastEntitlementCheckMs)
        }

    private fun createFeature() = RelayFeature(
        accountManager = accountManager,
        store = store,
        mainDispatcher = testDispatcher,
        fxRelayFactory = { fakeFxRelay },
        extractHostUrl = { it },
    )

    private fun createRelayAttachedClient() = AttachedClient(
        clientId = ServiceClientId.Production.id,
        deviceId = "device-id",
        deviceType = DeviceType.MOBILE,
        isCurrentSession = false,
        name = "Relay",
        createdTime = null,
        lastAccessTime = null,
        scope = null,
    )

    private class FakeAccountManagerDelegate(
        var account: OAuthAccount? = null,
    ) : FxaAccountManagerDelegate {
        val observers = mutableListOf<AccountObserver>()
        var registerCallCount = 0
        var unregisterCallCount = 0

        override fun register(observer: AccountObserver) {
            registerCallCount++
            observers.add(observer)
        }

        override fun unregister(observer: AccountObserver) {
            unregisterCallCount++
            observers.remove(observer)
        }

        override fun authenticatedAccount() = account
        override fun connectedAccount() = account
    }

    private class FakeOAuthAccount(
        private val attachedClients: List<AttachedClient> = emptyList(),
    ) : OAuthAccount {
        override suspend fun getAttachedClient() = attachedClients
        override fun getCurrentDeviceId(): String? = null
        override suspend fun handleWebChannelLogin(jsonPayload: String) = Unit
        override fun getSignedInUserForWebChannel(): String? = null
        override fun hasScope(scopes: String) = false
        override suspend fun getProfile(ignoreCache: Boolean): Profile? = null
        override suspend fun getAccessToken(singleScope: String): AccessTokenInfo? = null
        override fun authErrorDetected() = Unit
        override suspend fun checkAuthorizationStatus(singleScope: String): Boolean? = null
        override suspend fun getTokenServerEndpointURL(): String? = null
        override suspend fun getManageAccountURL(entryPoint: FxAEntryPoint): String? = null
        override fun getPairingAuthorityURL() = ""
        override fun registerPersistenceCallback(callback: StatePersistenceCallback) = Unit
        override fun deviceConstellation(): DeviceConstellation =
            throw UnsupportedOperationException()

        override suspend fun disconnect() = false
        override fun toJSONString() = ""
        override fun close() = Unit
    }

    private class FakeFxRelay(
        var emailMasks: List<EmailMask>? = emptyList(),
        var accountDetails: RelayAccountDetails? = null,
        var createdMask: EmailMask? = null,
    ) : FxRelay {
        override suspend fun fetchEmailMasks() = emailMasks
        override suspend fun fetchAccountDetails() = accountDetails
        override suspend fun createEmailMask(generatedForHostUrl: String, descriptionHostUrl: String) =
            createdMask
    }
}
