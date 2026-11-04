/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.messaging.state

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.ConsumeMessageToShow
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.Evaluate
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MessageClicked
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MessageDismissed
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MicrosurveyAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.Restore
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.UpdateMessageToShow
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.UpdateMessages
import org.mozilla.fenix.components.appstate.AppState

typealias AppStoreMiddlewareContext = Store<AppState, AppAction>

class MessagingMiddleware(
    private val controller: NimbusMessagingControllerInterface,
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Middleware<AppState, AppAction> {

    override fun invoke(
        store: AppStoreMiddlewareContext,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        when (action) {
            is Restore -> {
                coroutineScope.launch {
                    val messages = controller.getMessages()
                    store.dispatch(UpdateMessages(messages))
                }
            }

            is Evaluate -> {
                val message =
                    controller.getNextMessage(
                        action.surface,
                        store.state.messaging.messages,
                        action.triggerContext,
                    )
                if (message != null) {
                    store.dispatch(UpdateMessageToShow(message))
                    onMessagedDisplayed(message, store)
                } else {
                    store.dispatch(ConsumeMessageToShow(action.surface))
                }
            }

            is MessageClicked -> onMessageClicked(action.message, store)

            is MessageDismissed -> onMessageDismissed(store, action.message)

            is MicrosurveyAction.Shown -> onMicrosurveyShown(action.id)

            is MicrosurveyAction.OnPrivacyNoticeTapped -> onPrivacyNoticeTapped(action.id)

            is MicrosurveyAction.Dismissed -> {
                store.state.messaging.messages
                    .find { it.id == action.id }
                    ?.let { message ->
                        onMicrosurveyDismissed(store, message)
                    }
            }

            is MicrosurveyAction.Completed -> {
                store.state.messaging.messages
                    .find { it.id == action.id }
                    ?.let { message ->
                        onMicrosurveyCompleted(store, message, action.answer)
                    }
            }

            is MicrosurveyAction.SentConfirmationShown -> onMicrosurveyConfirmationShown(action.id)

            is MicrosurveyAction.Started -> onMicrosurveyStarted(action.id)

            else -> {
                // no-op
            }
        }
        next(action)
    }

    private fun onMicrosurveyCompleted(
        store: AppStoreMiddlewareContext,
        message: Message,
        answer: String,
    ) {
        val newMessages = removeMessage(store, message)
        store.dispatch(UpdateMessages(newMessages))
        consumeMessageToShowIfNeeded(store, message)
        coroutineScope.launch {
            controller.onMicrosurveyCompleted(message, answer)
        }
    }

    private fun onMicrosurveyShown(id: String) {
        coroutineScope.launch {
            controller.onMicrosurveyShown(id)
        }
    }

    private fun onMicrosurveyDismissed(
        store: AppStoreMiddlewareContext,
        message: Message,
    ) {
        val newMessages = removeMessage(store, message)
        store.dispatch(UpdateMessages(newMessages))
        consumeMessageToShowIfNeeded(store, message)
        coroutineScope.launch {
            controller.onMicrosurveyDismissed(message)
        }
    }

    private fun onMicrosurveyConfirmationShown(id: String) {
        coroutineScope.launch {
            controller.onMicrosurveySentConfirmationShown(id)
        }
    }

    private fun onPrivacyNoticeTapped(id: String) {
        coroutineScope.launch {
            controller.onMicrosurveyPrivacyNoticeTapped(id)
        }
    }

    private fun onMessagedDisplayed(
        oldMessage: Message,
        store: AppStoreMiddlewareContext,
    ) {
        coroutineScope.launch {
            val newMessage = controller.onMessageDisplayed(oldMessage)
            val newMessages =
                if (!newMessage.isExpired) {
                    updateMessage(store, oldMessage, newMessage)
                } else {
                    removeMessage(store, oldMessage)
                }
            store.dispatch(UpdateMessages(newMessages))
        }
    }

    private fun onMessageDismissed(
        store: AppStoreMiddlewareContext,
        message: Message,
    ) {
        val newMessages = removeMessage(store, message)
        store.dispatch(UpdateMessages(newMessages))
        consumeMessageToShowIfNeeded(store, message)
        coroutineScope.launch {
            controller.onMessageDismissed(message)
        }
    }

    private fun onMessageClicked(
        message: Message,
        store: AppStoreMiddlewareContext,
    ) {
        // Update Nimbus storage.
        coroutineScope.launch {
            controller.onMessageClicked(message)
        }
        // Update app state.
        val newMessages = removeMessage(store, message)
        store.dispatch(UpdateMessages(newMessages))
        consumeMessageToShowIfNeeded(store, message)
    }

    private fun onMicrosurveyStarted(id: String) {
        coroutineScope.launch {
            controller.onMicrosurveyStarted(id)
        }
    }

    private fun consumeMessageToShowIfNeeded(
        store: AppStoreMiddlewareContext,
        message: Message,
    ) {
        val current = store.state.messaging.messageToShow[message.surface]
        if (current?.id == message.id) {
            store.dispatch(ConsumeMessageToShow(message.surface))
        }
    }

    private fun removeMessage(
        store: AppStoreMiddlewareContext,
        message: Message,
    ): List<Message> {
        return store.state.messaging.messages.filter { it.id != message.id }
    }

    private fun updateMessage(
        store: AppStoreMiddlewareContext,
        oldMessage: Message,
        updatedMessage: Message,
    ): List<Message> {
        val actualMessageToShow = store.state.messaging.messageToShow[updatedMessage.surface]

        if (actualMessageToShow?.id == oldMessage.id) {
            store.dispatch(UpdateMessageToShow(updatedMessage))
        }
        val oldMessageIndex = store.state.messaging.messages.indexOfFirst { it.id == updatedMessage.id }

        return if (oldMessageIndex != -1) {
            val newList = store.state.messaging.messages.toMutableList()
            newList[oldMessageIndex] = updatedMessage
            newList
        } else {
            // No need to update the message, it was removed. This is due to a race condition, see:
            // https://bugzilla.mozilla.org/show_bug.cgi?id=1897485
            store.state.messaging.messages
        }
    }
}
