/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.nimbus.messaging

import android.content.Intent
import android.net.Uri
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import mozilla.components.service.nimbus.GleanMetrics.Messaging as GleanMessaging
import mozilla.components.service.nimbus.GleanMetrics.Microsurvey

/**
 * Bookkeeping for message actions in terms of Glean messages and the messaging store.
 *
 * @param messagingStorage a NimbusMessagingStorage instance.
 * @param deepLinkScheme the deepLinkScheme for the app.
 */
open class NimbusMessagingController(
    private val messagingStorage: NimbusMessagingStorage,
    private val deepLinkScheme: String,
) : NimbusMessagingControllerInterface {
    override suspend fun onMessageDisplayed(displayedMessage: Message, bootIdentifier: String?): Message {
        sendShownMessageTelemetry(displayedMessage.id)
        val nextMessage = messagingStorage.onMessageDisplayed(displayedMessage, bootIdentifier)
        if (nextMessage.isExpired) {
            sendExpiredMessageTelemetry(nextMessage.id)
        }
        return nextMessage
    }

    override suspend fun onMessageDismissed(message: Message) {
        val messageMetadata = message.metadata
        sendDismissedMessageTelemetry(messageMetadata.id)
        val updatedMetadata = messageMetadata.copy(dismissed = true)
        messagingStorage.updateMetadata(updatedMetadata)
    }

    override suspend fun onMicrosurveyCompleted(message: Message, answer: String) {
        val messageMetadata = message.metadata
        sendMicrosurveyCompletedTelemetry(messageMetadata.id, answer)
        val updatedMetadata = messageMetadata.copy(pressed = true)
        messagingStorage.updateMetadata(updatedMetadata)
    }

    override suspend fun onMicrosurveyShown(id: String) {
        Microsurvey.shown.record(Microsurvey.ShownExtra(surveyId = id))
    }

    override suspend fun onMicrosurveyPrivacyNoticeTapped(id: String) {
        Microsurvey.privacyNoticeTapped.record(Microsurvey.PrivacyNoticeTappedExtra(surveyId = id))
    }

    /**
     * Called once the user has clicked on a message.
     *
     * This records that the message has been clicked on, but does not record a glean event. That should be done via
     * [processMessageActionToUri].
     */
    override suspend fun onMessageClicked(message: Message) {
        val messageMetadata = message.metadata
        val updatedMetadata = messageMetadata.copy(pressed = true)
        messagingStorage.updateMetadata(updatedMetadata)
    }

    override suspend fun onMicrosurveyDismissed(message: Message) {
        Microsurvey.dismissButtonTapped.record(Microsurvey.DismissButtonTappedExtra(surveyId = message.id))
        val messageMetadata = message.metadata
        val updatedMetadata = messageMetadata.copy(dismissed = true)
        messagingStorage.updateMetadata(updatedMetadata)
    }

    override suspend fun onMicrosurveySentConfirmationShown(id: String) {
        Microsurvey.confirmationShown.record(Microsurvey.ConfirmationShownExtra(surveyId = id))
    }

    override suspend fun onMicrosurveyStarted(id: String) {
        sendClickedMessageTelemetry(id, null)
    }

    override fun getIntentForMessage(message: Message) =
        Intent(
            Intent.ACTION_VIEW,
            processMessageActionToUri(message),
        )

    override suspend fun getMessage(id: String): Message? {
        return messagingStorage.getMessage(id)
    }

    /**
     * The [message] action needs to be examined for string substitutions and any `uuid` needs to be recorded in the
     * Glean event.
     *
     * We call this `process` as it has a side effect of logging a Glean event while it creates a URI string for the
     * message action.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    fun processMessageActionToUri(message: Message): Uri {
        val (uuid, action) = messagingStorage.generateUuidAndFormatMessage(message)
        sendClickedMessageTelemetry(message.id, uuid)

        return convertActionIntoDeepLinkSchemeUri(action)
    }

    private fun sendDismissedMessageTelemetry(messageId: String) {
        GleanMessaging.messageDismissed.record(GleanMessaging.MessageDismissedExtra(messageId))
    }

    private fun sendShownMessageTelemetry(messageId: String) {
        GleanMessaging.messageShown.record(GleanMessaging.MessageShownExtra(messageId))
    }

    private fun sendExpiredMessageTelemetry(messageId: String) {
        GleanMessaging.messageExpired.record(GleanMessaging.MessageExpiredExtra(messageId))
    }

    private fun sendClickedMessageTelemetry(messageId: String, uuid: String?) {
        GleanMessaging.messageClicked.record(
            GleanMessaging.MessageClickedExtra(messageKey = messageId, actionUuid = uuid)
        )
    }

    private fun sendMicrosurveyCompletedTelemetry(messageId: String, answer: String) {
        Microsurvey.submitButtonTapped.record(
            Microsurvey.SubmitButtonTappedExtra(
                surveyId = messageId,
                userSelection = answer,
            )
        )
    }

    private fun convertActionIntoDeepLinkSchemeUri(action: String): Uri =
        if (action.startsWith("://")) {
            "$deepLinkScheme$action".toUri()
        } else {
            action.toUri()
        }

    override suspend fun getMessages(): List<Message> = messagingStorage.getMessages()

    override suspend fun getNextMessage(surfaceId: MessageSurfaceId) = getNextMessage(surfaceId, getMessages())

    override fun getNextMessage(
        surfaceId: MessageSurfaceId,
        messages: List<Message>,
        triggerContext: Map<String, Any?>?,
    ) = messagingStorage.getNextMessage(surfaceId, messages, triggerContext)
}
