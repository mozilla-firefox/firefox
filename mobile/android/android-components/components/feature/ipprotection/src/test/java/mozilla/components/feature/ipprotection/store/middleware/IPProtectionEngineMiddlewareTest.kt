/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store.middleware

import kotlin.concurrent.thread
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.FakeIPProtectionHandler
import mozilla.components.feature.ipprotection.storage.IPProtectionTestMiddleware
import mozilla.components.feature.ipprotection.storage.buildIPProtectionState
import mozilla.components.feature.ipprotection.store.ActivationOperation
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyRequest
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class IPProtectionEngineMiddlewareTest {

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `WHEN the user toggles THEN an idle proxy is turned on, an activating, active or failing one is turned off, and any other is left alone`() =
        runTest(testDispatcher) {
            val cases =
                listOf(
                    Triple(ServiceState.Ready, Authorized.Idle, listOf(FakeIPProtectionHandler.Call.Activate(null))),
                    Triple(ServiceState.Ready, Authorized.Active, listOf(FakeIPProtectionHandler.Call.Deactivate)),
                    Triple(
                        ServiceState.Ready,
                        Authorized.ConnectionError,
                        listOf(FakeIPProtectionHandler.Call.Deactivate),
                    ),
                    Triple(ServiceState.Ready, Authorized.Activating, listOf(FakeIPProtectionHandler.Call.Deactivate)),
                    Triple(ServiceState.Ready, Authorized.DataLimitReached, emptyList()),
                    Triple(ServiceState.Ready, Uninitialized, emptyList()),
                    Triple(ServiceState.Unauthenticated, Authorized.Idle, emptyList()),
                )

            cases.forEach { (serviceStatus, proxyStatus, expected) ->
                val handler = FakeIPProtectionHandler()
                val (store, _) = buildStore(readyState(proxyStatus).copy(serviceStatus = serviceStatus), handler)

                store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                testScheduler.advanceUntilIdle()

                assertEquals("$serviceStatus service, $proxyStatus proxy", expected, handler.proxyCalls())
                assertEquals(null, store.state.proxyRequest)
            }
        }

    @Test
    fun `GIVEN a deactivation in progress WHEN the user toggles or picks a location THEN no other request is sent`() =
        runTest(testDispatcher) {
            listOf(
                    IPProtectionAction.ActivationAction.Toggle,
                    IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true),
                )
                .forEach { action ->
                    val handler = FakeIPProtectionHandler(holdResults = true)
                    val (store, _) = buildStore(readyState(Authorized.Active), handler)

                    store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                    testScheduler.advanceUntilIdle()
                    store.dispatch(action)
                    testScheduler.advanceUntilIdle()
                    handler.releaseHeldResults()
                    testScheduler.advanceUntilIdle()

                    assertEquals("$action", listOf(FakeIPProtectionHandler.Call.Deactivate), handler.proxyCalls())
                }
        }

    @Test
    fun `GIVEN an activation in progress WHEN the user picks a location THEN the proxy is moved there once the activation succeeds AND then to a location picked during that move`() =
        runTest(testDispatcher) {
            val activated = listOf(FakeIPProtectionHandler.Call.Activate(null))
            val moved =
                activated + FakeIPProtectionHandler.Call.Activate("JP") + FakeIPProtectionHandler.Call.Activate("DE")

            listOf(ProxyState.ACTIVATING, ProxyState.ACTIVE).forEach { proxyState ->
                val handler = FakeIPProtectionHandler(holdResults = true)
                val (store, _) = buildStore(readyState(Authorized.Idle), handler)

                store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                testScheduler.advanceUntilIdle()
                store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(proxyState))
                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
                testScheduler.advanceUntilIdle()

                assertEquals("$proxyState proxy before the activation returns", activated, handler.proxyCalls())

                handler.releaseHeldResults()
                testScheduler.advanceUntilIdle()
                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(GERMANY, userAction = true))
                handler.holdResults = false
                handler.releaseHeldResults()
                testScheduler.advanceUntilIdle()

                assertEquals("$proxyState proxy", moved, handler.proxyCalls())
                assertEquals(null, store.state.proxyRequest)
            }
        }

    @Test
    fun `GIVEN an activation in progress WHEN the user picks a location AND the activation is rejected THEN the proxy is not moved`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(activateResult = RuntimeException("rejected"), holdResults = true)
            val (store, _) = buildStore(readyState(Authorized.Idle), handler)

            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVATING))
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            handler.holdResults = false
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(listOf(FakeIPProtectionHandler.Call.Activate(null)), handler.proxyCalls())
            assertEquals(null, store.state.proxyRequest)
        }

    @Test
    fun `WHEN a request fails THEN the failure is reported for the request that failed`() =
        runTest(testDispatcher) {
            val failure = RuntimeException("rejected")
            val handler = FakeIPProtectionHandler(activateResult = failure, deactivateResult = failure)

            val (activation, activationActions) = buildStore(readyState(Authorized.Idle), handler)
            activation.dispatch(IPProtectionAction.ActivationAction.Toggle)
            val (switch, switchActions) = buildStore(readyState(Authorized.Active), handler)
            switch.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            val (deactivation, deactivationActions) = buildStore(readyState(Authorized.Active), handler)
            deactivation.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()

            activationActions.assertLastAction(IPProtectionAction.ActivationAction.ToggleFailed::class) {
                assertEquals(ActivationOperation.Activate, it.operation)
            }
            switchActions.assertLastAction(IPProtectionAction.LocationAction.LocationSwitchFailed::class)
            switchActions.assertNotDispatched(IPProtectionAction.ActivationAction.ToggleFailed::class)
            deactivationActions.assertLastAction(IPProtectionAction.ActivationAction.ToggleFailed::class) {
                assertEquals(ActivationOperation.Deactivate, it.operation)
            }
        }

    @Test
    fun `WHEN the user picks a location THEN an active proxy is moved there`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler()
            val (store, _) = buildStore(readyState(Authorized.Active), handler)

            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            testScheduler.advanceUntilIdle()

            assertEquals(listOf(FakeIPProtectionHandler.Call.Activate("JP")), handler.proxyCalls())
        }

    @Test
    fun `WHEN enrollment finishes THEN the proxy is turned on only if enrollment succeeded, even when the service reports Ready straight after`() =
        runTest(testDispatcher) {
            listOf(true to listOf(FakeIPProtectionHandler.Call.Activate(null)), false to emptyList()).forEach {
                (success, expected) ->
                val handler = FakeIPProtectionHandler()
                val (store, _) =
                    buildStore(
                        readyState(Authorized.Idle).copy(serviceStatus = ServiceState.Unauthenticated),
                        handler,
                    )

                store.dispatch(IPProtectionAction.AccountAction.FinishingEnrollment(success))
                store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready))
                testScheduler.advanceUntilIdle()

                assertEquals("successful enrollment: $success", expected, handler.proxyCalls())
            }
        }

    @Test
    fun `GIVEN an activation in progress WHEN the user toggles or signs out THEN the proxy is turned off AND the cancelled activation reports no failure`() =
        runTest(testDispatcher) {
            listOf(
                    IPProtectionAction.ActivationAction.Toggle,
                    IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.NoAccount),
                )
                .forEach { action ->
                    val handler =
                        FakeIPProtectionHandler(
                            activateResult = RuntimeException("activation-canceled"),
                            holdResults = true,
                        )
                    val (store, actions) = buildStore(readyState(Authorized.Idle), handler)

                    store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                    testScheduler.advanceUntilIdle()
                    store.dispatch(action)
                    testScheduler.advanceUntilIdle()
                    handler.releaseHeldResults()
                    testScheduler.advanceUntilIdle()

                    assertEquals(
                        "$action",
                        listOf(FakeIPProtectionHandler.Call.Activate(null), FakeIPProtectionHandler.Call.Deactivate),
                        handler.proxyCalls(),
                    )
                    actions.assertNotDispatched(IPProtectionAction.ActivationAction.ToggleFailed::class)
                    assertEquals(null, store.state.proxyRequest)
                }
        }

    @Test
    fun `GIVEN the service cannot run WHEN the service or the proxy reports THEN the proxy is turned off once, and only if it was running or waiting to start before the report`() =
        runTest(testDispatcher) {
            val serviceStops = IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated)
            val proxyStops = IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY)
            val proxyStarts = IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE)
            val unauthenticated = { proxyStatus: ProxyStatus ->
                readyState(proxyStatus).copy(serviceStatus = ServiceState.Unauthenticated)
            }
            val cases =
                listOf(
                    Triple(readyState(Authorized.Active), listOf(listOf(serviceStops)), 1),
                    Triple(readyState(Authorized.Active), listOf(listOf(serviceStops), listOf(proxyStops)), 1),
                    Triple(readyState(Authorized.Idle), listOf(listOf(serviceStops)), 0),
                    Triple(
                        readyState(Authorized.Idle),
                        listOf(listOf(IPProtectionAction.ActivationAction.Toggle, serviceStops)),
                        1,
                    ),
                    Triple(unauthenticated(Authorized.Active), listOf(listOf(proxyStops)), 1),
                    Triple(unauthenticated(Authorized.Idle), listOf(listOf(proxyStarts)), 0),
                )

            cases.forEach { (state, steps, expected) ->
                val handler = FakeIPProtectionHandler()
                val (store, _) = buildStore(state, handler)

                steps.forEach { step ->
                    step.forEach { store.dispatch(it) }
                    testScheduler.advanceUntilIdle()
                }

                assertEquals(
                    "${state.proxyStatus} proxy after $steps",
                    List(expected) { FakeIPProtectionHandler.Call.Deactivate },
                    handler.proxyCalls(),
                )
            }
        }

    @Test
    fun `GIVEN the user's turn-off failed WHEN the service stops THEN the proxy is turned off again`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(deactivateResult = RuntimeException("rejected"))
            val (store, _) = buildStore(readyState(Authorized.Active), handler)

            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated))
            testScheduler.advanceUntilIdle()

            assertEquals(List(2) { FakeIPProtectionHandler.Call.Deactivate }, handler.proxyCalls())
        }

    @Test
    fun `GIVEN an activation is waiting behind a turn-off that fails WHEN the user toggles THEN the activation is cancelled instead of a second one being sent`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(deactivateResult = RuntimeException("rejected"), holdResults = true)
            val (store, _) = buildStore(readyState(Authorized.Idle), handler)

            store.dispatch(IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.NoAccount))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.AccountAction.FinishingEnrollment(success = true))
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            handler.holdResults = false
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(
                    FakeIPProtectionHandler.Call.Deactivate,
                    FakeIPProtectionHandler.Call.Activate(null),
                    FakeIPProtectionHandler.Call.Deactivate,
                ),
                handler.proxyCalls(),
            )
        }

    @Test
    fun `GIVEN locations picked while a switch is running WHEN the switch finishes THEN the proxy is moved straight to the latest one AND only the locations it is moved to are shown as running`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(holdResults = true)
            val (store, _) = buildStore(readyState(Authorized.Active), handler)
            val shownRequests = mutableListOf<ProxyRequest?>()
            store.observeManually { shownRequests += it.proxyRequest }.resume()

            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(GERMANY, userAction = true))
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(FRANCE, userAction = true))
            testScheduler.advanceUntilIdle()

            listOf(ProxyRequest.SwitchLocation("JP"), ProxyRequest.SwitchLocation("FR"), null).forEach { expected ->
                assertEquals(expected, store.state.proxyRequest)
                handler.releaseHeldResults()
                testScheduler.advanceUntilIdle()
            }

            assertEquals(
                listOf(FakeIPProtectionHandler.Call.Activate("JP"), FakeIPProtectionHandler.Call.Activate("FR")),
                handler.proxyCalls(),
            )
            assertEquals(
                listOf(ProxyRequest.SwitchLocation("JP"), ProxyRequest.SwitchLocation("FR")),
                shownRequests.filterNotNull().distinct(),
            )
        }

    @Test
    fun `GIVEN a location switch is waiting WHEN the proxy stops before the switch starts THEN the switch is not sent`() =
        runTest(testDispatcher) {
            listOf(ProxyState.ERROR, ProxyState.READY).forEach { proxyState ->
                val handler = FakeIPProtectionHandler()
                val (store, _) = buildStore(readyState(Authorized.Idle), handler)

                store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                testScheduler.advanceUntilIdle()
                store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))
                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
                store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(proxyState))
                testScheduler.advanceUntilIdle()

                assertEquals(
                    "$proxyState proxy",
                    listOf(FakeIPProtectionHandler.Call.Activate(null)),
                    handler.proxyCalls(),
                )
            }
        }

    @Test
    fun `GIVEN a location switch is running WHEN the user toggles THEN the proxy is turned off after the switch finishes`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(holdResults = true)
            val (store, _) = buildStore(readyState(Authorized.Active), handler)

            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()

            assertEquals(listOf(FakeIPProtectionHandler.Call.Activate("JP")), handler.proxyCalls())

            handler.holdResults = false
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(FakeIPProtectionHandler.Call.Activate("JP"), FakeIPProtectionHandler.Call.Deactivate),
                handler.proxyCalls(),
            )
        }

    @Test
    fun `GIVEN a location switch fails while others are waiting WHEN the next switch runs THEN it goes to the latest location`() =
        runTest(testDispatcher) {
            val handler =
                FakeIPProtectionHandler(activateResult = RuntimeException("server-not-found"), holdResults = true)
            val (store, _) = buildStore(readyState(Authorized.Active), handler)

            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(GERMANY, userAction = true))
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(FRANCE, userAction = true))
            handler.activateResult = null
            handler.holdResults = false
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(FakeIPProtectionHandler.Call.Activate("JP"), FakeIPProtectionHandler.Call.Activate("FR")),
                handler.proxyCalls(),
            )
        }

    @Test
    fun `GIVEN a location switch failed WHEN a later turn-off also fails THEN no switch is sent`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(deactivateResult = RuntimeException("rejected"))
            val (store, _) = buildStore(readyState(Authorized.Idle), handler)

            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))
            handler.activateResult = RuntimeException("server-not-found")
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(
                    FakeIPProtectionHandler.Call.Activate(null),
                    FakeIPProtectionHandler.Call.Activate("JP"),
                    FakeIPProtectionHandler.Call.Deactivate,
                ),
                handler.proxyCalls(),
            )
        }

    @Test
    fun `GIVEN a location was picked during a turn-off WHEN the turn-off fails THEN the proxy is moved to that location`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(deactivateResult = RuntimeException("rejected"))
            val (store, _) = buildStore(readyState(Authorized.Idle), handler)

            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))
            handler.holdResults = true
            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(GERMANY, userAction = true))
            handler.holdResults = false
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(
                    FakeIPProtectionHandler.Call.Activate(null),
                    FakeIPProtectionHandler.Call.Deactivate,
                    FakeIPProtectionHandler.Call.Activate("DE"),
                ),
                handler.proxyCalls(),
            )
        }

    @Test
    fun `GIVEN sign-out is reported from a background thread WHEN the proxy is turned off THEN the engine is called on the middleware's thread`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler()
            val (store, _) = buildStore(readyState(Authorized.Active), handler)

            thread {
                store.dispatch(IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.NoAccount))
            }
                .join()
            testScheduler.advanceUntilIdle()

            assertEquals(listOf(FakeIPProtectionHandler.Call.Deactivate), handler.proxyCalls())
            assertEquals(Thread.currentThread(), handler.lastProxyCallThread)
        }

    @Test
    fun `GIVEN the engine was reset or the proxy failed after the VPN was turned on WHEN a later turn-off fails THEN no switch is sent`() =
        runTest(testDispatcher) {
            val engineReset =
                listOf(
                    IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Uninitialized),
                    IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready),
                )
            val proxyFailed = listOf(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ERROR))

            listOf(engineReset, proxyFailed).forEach { reports ->
                val handler = FakeIPProtectionHandler(deactivateResult = RuntimeException("rejected"))
                val (store, _) = buildStore(readyState(Authorized.Idle), handler)

                store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                testScheduler.advanceUntilIdle()
                store.dispatch(IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))
                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(JAPAN, userAction = true))
                testScheduler.advanceUntilIdle()
                reports.forEach { store.dispatch(it) }
                handler.holdResults = true
                store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                testScheduler.advanceUntilIdle()
                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(GERMANY, userAction = true))
                handler.holdResults = false
                handler.releaseHeldResults()
                testScheduler.advanceUntilIdle()

                assertEquals(
                    "after $reports",
                    listOf(
                        FakeIPProtectionHandler.Call.Activate(null),
                        FakeIPProtectionHandler.Call.Activate("JP"),
                        FakeIPProtectionHandler.Call.Deactivate,
                    ),
                    handler.proxyCalls(),
                )
            }
        }

    @Test
    fun `GIVEN a request in progress WHEN the engine resets THEN the request is dropped without a failure`() =
        runTest(testDispatcher) {
            val handler = FakeIPProtectionHandler(activateResult = RuntimeException("rejected"), holdResults = true)
            val (store, actions) = buildStore(readyState(Authorized.Idle), handler)

            store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            testScheduler.advanceUntilIdle()
            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Uninitialized))
            testScheduler.advanceUntilIdle()
            handler.releaseHeldResults()
            testScheduler.advanceUntilIdle()

            assertEquals(null, store.state.proxyRequest)
            actions.assertNotDispatched(IPProtectionAction.ActivationAction.ToggleFailed::class)
        }

    private fun readyState(proxyStatus: ProxyStatus): IPProtectionState =
        buildIPProtectionState(
            eligibilityStatus = EligibilityStatus.Eligible,
            serviceStatus = ServiceState.Ready,
            proxyStatus = proxyStatus,
            accountStatus = AccountStatus.EnrolledAndEntitled,
        )

    private fun TestScope.buildStore(
        state: IPProtectionState,
        handler: FakeIPProtectionHandler,
    ): Pair<IPProtectionStore, IPProtectionTestMiddleware> {
        val actions = IPProtectionTestMiddleware()
        val store =
            IPProtectionStore(
                initialState = state,
                middleware = listOf(actions, IPProtectionEngineMiddleware(scope = this).also { it.handler = handler }),
            )
        return store to actions
    }

    private fun FakeIPProtectionHandler.proxyCalls() = calls.filter {
        it is FakeIPProtectionHandler.Call.Activate || it is FakeIPProtectionHandler.Call.Deactivate
    }

    private companion object {
        val JAPAN = Country("JP", available = true)
        val GERMANY = Country("DE", available = true)
        val FRANCE = Country("FR", available = true)
    }
}
