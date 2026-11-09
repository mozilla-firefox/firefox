/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.storage.buildIPProtectionState
import mozilla.components.feature.ipprotection.storage.buildStore
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.middleware.IPProtectionEngineMiddleware
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class IPProtectionFeatureTest {

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `WHEN the account is awaiting enrollment THEN the sign-in is announced before enrolling`() =
        runTest(testDispatcher) {
            val (store, _) =
                buildStore(
                    buildIPProtectionState(
                        accountStatus = AccountStatus.AwaitingAuthentication,
                        eligibilityStatus = EligibilityStatus.Eligible,
                    )
                )
            val handler = startFeature(store)

            store.dispatch(IPProtectionAction.AccountAction.AccountReadyForEnrollment)
            testScheduler.advanceUntilIdle()

            // Enrollment is only usable for an account the service already knows is signed in.
            assertEquals(
                listOf(FakeIPProtectionHandler.Call.NotifyAccountStatus(true), FakeIPProtectionHandler.Call.Enroll),
                handler.calls.filter {
                    it is FakeIPProtectionHandler.Call.NotifyAccountStatus || it is FakeIPProtectionHandler.Call.Enroll
                },
            )
        }

    @Test
    fun `GIVEN the account manager reports Authenticated before WarmingUp WHEN the service starts unauthenticated THEN it is told the user is signed in`() =
        runTest(testDispatcher) {
            val (store, _) = buildStore(buildIPProtectionState(accountStatus = AccountStatus.Uninitialized))
            val handler = startFeature(store)

            store.dispatch(IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.Authenticated))
            store.dispatch(IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.WarmingUp))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated))
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(FakeIPProtectionHandler.Call.NotifyAccountStatus(true)),
                handler.calls.filterIsInstance<FakeIPProtectionHandler.Call.NotifyAccountStatus>(),
            )
        }

    @Test
    fun `GIVEN the feature has registered with the engine WHEN the user turns the VPN on THEN the request reaches the engine`() =
        runTest(testDispatcher) {
            val engineMiddleware = IPProtectionEngineMiddleware(scope = this)
            val store =
                IPProtectionStore(
                    initialState =
                        buildIPProtectionState(
                            accountStatus = AccountStatus.EnrolledAndEntitled,
                            eligibilityStatus = EligibilityStatus.Eligible,
                        ),
                    middleware = listOf(engineMiddleware),
                )
            val handler = startFeature(store, engineMiddleware)

            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready))
            store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY))
            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(FakeIPProtectionHandler.Call.Activate(null)),
                handler.calls.filterIsInstance<FakeIPProtectionHandler.Call.Activate>(),
            )
        }

    private fun TestScope.startFeature(
        store: IPProtectionStore,
        engineMiddleware: IPProtectionEngineMiddleware = IPProtectionEngineMiddleware(scope = this),
    ): FakeIPProtectionHandler {
        val handler = FakeIPProtectionHandler()
        val engine: Engine = mock { whenever(registerIPProtectionDelegate(any())).thenReturn(handler) }

        IPProtectionFeature(
                store = store,
                engine = engine,
                accountManager = mock<FxaAccountManager>(),
                engineMiddleware = engineMiddleware,
                mainDispatcher = testDispatcher,
            )
            .initialize()
        testScheduler.advanceUntilIdle()

        return handler
    }
}
