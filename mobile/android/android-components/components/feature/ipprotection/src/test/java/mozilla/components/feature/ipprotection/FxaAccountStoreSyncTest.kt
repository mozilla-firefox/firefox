/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.sync.AuthFlowError
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow.Companion.SCOPE_IPPROTECTION
import mozilla.components.feature.ipprotection.storage.FxaAccountStoreSync
import mozilla.components.feature.ipprotection.storage.buildIPProtectionState
import mozilla.components.feature.ipprotection.storage.buildStore
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.service.fxa.manager.AccountState
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.store.SyncAction
import mozilla.components.service.fxa.store.SyncState
import mozilla.components.service.fxa.store.SyncStatus
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.support.test.coMock
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class FxaAccountStoreSyncTest {

    lateinit var syncStore: SyncStore
    lateinit var accountManager: FxaAccountManager

    @Before
    fun setup() {
        syncStore = SyncStore()
        accountManager = mock<FxaAccountManager>()
    }

    @Test
    fun `WHEN initialized THEN the initial Unknown account state is forwarded as Uninitialized`() = runTest {
        val (ipProtectionStore, captureMiddleware) = buildStore()

        FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher(testScheduler))
            .initialize()

        testScheduler.advanceUntilIdle()

        captureMiddleware.assertFirstAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
            assertEquals(AccountStatus.Uninitialized, it.status)
        }
        assertEquals(AccountStatus.Uninitialized, ipProtectionStore.state.accountState.status)
    }

    @Test
    fun `WHEN the account state changes THEN the mapped status is forwarded`() = runTest {
        // All these cases are the same mappings - and we do not want them to change.
        val cases =
            listOf(
                AccountState.AuthenticationProblem to AccountStatus.NeedsAuthentication,
                AccountState.NotAuthenticated to AccountStatus.NoAccount,
            )

        cases.forEach { (accountState, expectedStatus) ->
            val (ipProtectionStore, captureMiddleware) = buildStore()
            val syncStore = SyncStore()

            FxaAccountStoreSync(
                    syncStore,
                    ipProtectionStore,
                    lazyOf(accountManager),
                    StandardTestDispatcher(testScheduler),
                )
                .initialize()

            syncStore.dispatch(SyncAction.UpdateAccountState(accountState))

            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
                assertEquals("Unexpected forwarded status for $accountState", expectedStatus, it.status)
            }
            assertEquals(
                "Unexpected store state for $accountState",
                expectedStatus,
                ipProtectionStore.state.accountState.status,
            )
        }
    }

    @Test
    fun `WHEN authenticated AND the IP protection scope is granted THEN Authenticated is forwarded`() = runTest {
        val (ipProtectionStore, captureMiddleware) = buildStore()
        val accountManager: FxaAccountManager = coMock {
            whenever(containsScope(SCOPE_IPPROTECTION)).thenReturn(true)
        }

        FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher(testScheduler))
            .initialize()

        syncStore.dispatch(SyncAction.UpdateAccountState(AccountState.Authenticated))

        testScheduler.advanceUntilIdle()

        captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
            assertEquals(AccountStatus.Authenticated, it.status)
        }
        assertEquals(AccountStatus.Authenticated, ipProtectionStore.state.accountState.status)
    }

    @Test
    fun `WHEN authenticated AND the IP protection scope is not granted THEN NeedsAuthorization is forwarded`() =
        runTest {
            val (ipProtectionStore, captureMiddleware) = buildStore()

            whenever(accountManager.containsScope(SCOPE_IPPROTECTION)).thenReturn(false)

            FxaAccountStoreSync(
                    syncStore,
                    ipProtectionStore,
                    lazyOf(accountManager),
                    StandardTestDispatcher(testScheduler),
                )
                .initialize()

            syncStore.dispatch(SyncAction.UpdateAccountState(AccountState.Authenticated))

            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
                assertEquals(AccountStatus.NeedsAuthorization, it.status)
            }
            assertEquals(AccountStatus.NeedsAuthorization, ipProtectionStore.state.accountState.status)
        }

    @Test
    fun `WHEN the account state is unchanged THEN nothing new is forwarded`() = runTest {
        val (ipProtectionStore, captureMiddleware) = buildStore()
        whenever(accountManager.containsScope(SCOPE_IPPROTECTION)).thenReturn(true)

        FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher(testScheduler))
            .initialize()
        syncStore.dispatch(SyncAction.UpdateAccountState(AccountState.Authenticated))

        captureMiddleware.reset()

        syncStore.dispatch(SyncAction.UpdateSyncStatus(SyncStatus.Idle))

        assertEquals(SyncState(accountState = AccountState.Authenticated, status = SyncStatus.Idle), syncStore.state)
        captureMiddleware.assertNotDispatched(IPProtectionAction.AccountAction.AccountManagerStateChanged::class)
    }

    @Test
    fun `WHEN onFlowError is called THEN AuthFailed is forwarded`() {
        val (ipProtectionStore, captureMiddleware) = buildStore()

        val sync = FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher())

        sync.onFlowError(AuthFlowError.FailedToCompleteAuth)

        captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
            assertEquals(AccountStatus.AuthFailed, it.status)
        }
        // The reducer moves AuthFailed into NeedsAuthentication.
        assertEquals(AccountStatus.NeedsAuthentication, ipProtectionStore.state.accountState.status)
    }

    @Test
    fun `WHEN onReady is called without an account THEN NoAccount is forwarded`() {
        val (ipProtectionStore, captureMiddleware) = buildStore()

        val sync = FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher())

        sync.onReady(authenticatedAccount = null)

        captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
            assertEquals(AccountStatus.NoAccount, it.status)
        }
        assertEquals(AccountStatus.NoAccount, ipProtectionStore.state.accountState.status)
    }

    @Test
    fun `WHEN onReady is called with an account THEN WarmingUp is forwarded`() {
        val (ipProtectionStore, captureMiddleware) =
            buildStore(buildIPProtectionState(accountStatus = AccountStatus.Uninitialized))

        val sync = FxaAccountStoreSync(syncStore, ipProtectionStore, lazyOf(accountManager), StandardTestDispatcher())

        sync.onReady(authenticatedAccount = mock<OAuthAccount>())

        captureMiddleware.assertLastAction(IPProtectionAction.AccountAction.AccountManagerStateChanged::class) {
            assertEquals(AccountStatus.WarmingUp, it.status)
        }
        assertEquals(AccountStatus.WarmingUp, ipProtectionStore.state.accountState.status)
    }
}
