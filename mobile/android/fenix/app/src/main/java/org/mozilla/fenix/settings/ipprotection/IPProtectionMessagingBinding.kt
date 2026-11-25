/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.settings.ipprotection

import android.content.Intent
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction
import org.mozilla.fenix.messaging.FenixMessageSurfaceId

/**
 * Binds the [AppStore] Nimbus messages to [IPProtectionStore] while the VPN settings surface is available.
 *
 * @param appStore The [AppStore] handling the messages
 * @param nimbusController The controller for handling the message interactions
 * @param iPProtectionStore The IP protection store
 * @param mainDispatcher Main dispatcher on which the state is observed
 */
class IPProtectionMessagingBinding(
    private val appStore: AppStore,
    private val nimbusController: NimbusMessagingControllerInterface,
    iPProtectionStore: IPProtectionStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(iPProtectionStore, mainDispatcher) {

    /** Messages eligible to be shown on the IP Protection screen */
    val message: StateFlow<Message?>
        field = MutableStateFlow(null)

    override suspend fun onState(flow: Flow<IPProtectionState>) {
        coroutineScope {
            launch { observeIpProtectionState(flow) }
            launch { observeIpProtectionMessages() }
        }
    }

    /** Dismisses the [message] */
    fun dismissMessage(message: Message) {
        appStore.dispatch(MessagingAction.MessageDismissed(message))
    }

    /** Registers message clicks and returns an intent to handle the action. */
    fun clickMessageAction(message: Message): Intent {
        val intent = nimbusController.getIntentForMessage(message)
        appStore.dispatch(MessagingAction.MessageClicked(message))

        return intent
    }

    private suspend fun observeIpProtectionState(flow: Flow<IPProtectionState>) {
        flow
            .distinctUntilChangedBy { Triple(it.serviceStatus, it.currentAuthProvider, it.accountState) }
            .filter { it.readyForEvaluation() }
            .map { state ->
                state.toMessageContext()
            }
            .distinctUntilChanged()
            .collect { context ->
                appStore.dispatch(MessagingAction.Evaluate(FenixMessageSurfaceId.SETTINGS_VPN, context))
            }
    }

    private suspend fun observeIpProtectionMessages() {
        appStore.stateFlow
            .map { it.messaging.messageToShow[FenixMessageSurfaceId.SETTINGS_VPN] }
            .distinctUntilChangedBy { it?.id }
            .collect { vpnMessage ->
                message.value = vpnMessage
            }
    }

    /**
     * The state is ready for evaluation when the service is ready, current auth provider is GPI, and there's no account
     * signed in
     */
    private fun IPProtectionState.readyForEvaluation(): Boolean {
        val serviceReady = serviceStatus == ServiceState.Ready
        val gpiAuthProvider = currentAuthProvider == CurrentAuthProvider.Gpi
        val noAccount = accountState.status == AccountStatus.NoAccount

        return serviceReady && gpiAuthProvider && noAccount
    }
}
