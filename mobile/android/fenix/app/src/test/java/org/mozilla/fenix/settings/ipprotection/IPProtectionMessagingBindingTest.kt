/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class, ExperimentalCoroutinesApi::class)

package org.mozilla.fenix.settings.ipprotection

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction.ServiceAction.ServiceStateChanged
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.MessageSurfaceId
import mozilla.components.service.nimbus.messaging.StyleData
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.Evaluate
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.UpdateMessageToShow
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.nimbus.TestNimbusMessagingController

@RunWith(AndroidJUnit4::class)
class IPProtectionMessagingBindingTest {

    private val testDispatcher = StandardTestDispatcher()
    private val captureActions = CaptureActionsMiddleware<AppState, AppAction>()
    private val vpnMessage = createMessage(FenixMessageSurfaceId.SETTINGS_VPN)

    @Test
    fun `GIVEN binding is started, WHEN user is using GPI, service is Ready and there is no account, THEN the surface is evaluated`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Ready,
                    authProvider = CurrentAuthProvider.Gpi,
                    accountStatus = AccountStatus.NoAccount,
                )
            val controller = createBinding(appStore, ipProtectionStore)

            controller.start()
            runCurrent()

            captureActions.assertFirstAction(Evaluate::class) { action ->
                assertEquals(FenixMessageSurfaceId.SETTINGS_VPN, action.surface)
            }
        }

    @Test
    fun `GIVEN binding is started, WHEN there is an account on device, THEN the surface is not evaluated`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Ready,
                    authProvider = CurrentAuthProvider.Gpi,
                    accountStatus = AccountStatus.Authenticated,
                )
            val controller = createBinding(appStore, ipProtectionStore)

            controller.start()
            runCurrent()

            captureActions.assertNotDispatched(Evaluate::class)
        }

    @Test
    fun `GIVEN binding is started, WHEN the user is using FxA, THEN the surface is not evaluated`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Ready,
                    authProvider = CurrentAuthProvider.Fxa,
                    accountStatus = AccountStatus.NoAccount,
                )
            val controller = createBinding(appStore, ipProtectionStore)

            controller.start()
            runCurrent()

            captureActions.assertNotDispatched(Evaluate::class)
        }

    @Test
    fun `GIVEN binding is started, WHEN the auth provider is unknown, THEN the surface is not evaluated`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Ready,
                    authProvider = CurrentAuthProvider.Unknown,
                    accountStatus = AccountStatus.NoAccount,
                )
            val controller = createBinding(appStore, ipProtectionStore)

            controller.start()
            runCurrent()

            captureActions.assertNotDispatched(Evaluate::class)
        }

    @Test
    fun `GIVEN binding is started, WHEN service is not ready, THEN the surface is not evaluated`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Uninitialized,
                    authProvider = CurrentAuthProvider.Gpi,
                    accountStatus = AccountStatus.NoAccount,
                )
            val controller = createBinding(appStore, ipProtectionStore)

            controller.start()
            runCurrent()

            captureActions.assertNotDispatched(Evaluate::class)
        }

    @Test
    fun `GIVEN binding is started, WHEN a new message for the VPN surface is available, THEN the message is emitted`() =
        runTest(testDispatcher) {
            val appStore = AppStore()
            val controller = createBinding(appStore)
            controller.start()
            testScheduler.advanceUntilIdle()

            appStore.dispatch(UpdateMessageToShow(vpnMessage))
            testScheduler.advanceUntilIdle()

            assertEquals(vpnMessage, controller.message.value)
        }

    @Test
    fun `GIVEN binding is started, WHEN a new message for other screens are available, THEN they are ignored`() =
        runTest(testDispatcher) {
            val appStore = AppStore()
            val controller = createBinding(appStore)
            val homescreenMessage = createMessage(FenixMessageSurfaceId.HOMESCREEN)
            val notificationMessage = createMessage(FenixMessageSurfaceId.NOTIFICATION)
            controller.start()
            runCurrent()

            appStore.dispatch(UpdateMessageToShow(homescreenMessage))
            runCurrent()
            assertNull(controller.message.value)

            appStore.dispatch(UpdateMessageToShow(notificationMessage))
            runCurrent()
            assertNull(controller.message.value)
        }

    @Test
    fun `GIVEN binding is stopped, WHEN the app store emits new messages for the VPN surface, THEN they are ignored`() =
        runTest(testDispatcher) {
            val appStore = AppStore()
            val controller = createBinding(appStore)
            val replacementMessage = createMessage(FenixMessageSurfaceId.SETTINGS_VPN, id = "replacement")
            controller.start()
            runCurrent()
            appStore.dispatch(UpdateMessageToShow(vpnMessage))
            runCurrent()
            assertEquals(vpnMessage, controller.message.value)

            controller.stop()
            appStore.dispatch(UpdateMessageToShow(replacementMessage))
            runCurrent()

            assertEquals(vpnMessage, controller.message.value)
        }

    @Test
    fun `GIVEN binding is stopped, WHEN the ip protection store receives valid state, THEN they are ignored`() =
        runTest(testDispatcher) {
            val appStore = AppStore(middlewares = listOf(captureActions))
            val ipProtectionStore =
                buildIpProtectionStore(
                    serviceStatus = ServiceState.Uninitialized,
                    authProvider = CurrentAuthProvider.Gpi,
                    accountStatus = AccountStatus.NoAccount,
                )
            val controller = createBinding(appStore, ipProtectionStore)
            controller.start()
            runCurrent()

            controller.stop()
            ipProtectionStore.dispatch(ServiceStateChanged(ServiceState.Ready))
            runCurrent()

            captureActions.assertNotDispatched(Evaluate::class)
        }

    private fun buildIpProtectionStore(
        serviceStatus: ServiceState,
        authProvider: CurrentAuthProvider,
        accountStatus: AccountStatus,
    ): IPProtectionStore =
        IPProtectionStore(
            initialState =
                IPProtectionState(
                    serviceStatus = serviceStatus,
                    currentAuthProvider = authProvider,
                    accountState = AccountState(status = accountStatus),
                )
        )

    private fun createBinding(
        appStore: AppStore,
        ipProtectionStore: IPProtectionStore = IPProtectionStore(),
        expectedMessages: List<Message> = emptyList(),
        expectedIntent: Intent = Intent(),
    ): IPProtectionMessagingBinding =
        IPProtectionMessagingBinding(
            appStore = appStore,
            nimbusController = TestNimbusMessagingController(expectedMessages, expectedIntent),
            iPProtectionStore = ipProtectionStore,
            mainDispatcher = testDispatcher,
        )

    private fun createMessage(surface: MessageSurfaceId, id: String = surface): Message =
        Message(
            id = id,
            data = MessageData(surface = surface),
            action = "action",
            style = StyleData(maxDisplayCount = 1),
            triggerIfAll = emptyList(),
            metadata = Message.Metadata(id = id),
        )
}
