/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus

import android.content.Intent
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageSurfaceId
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface

internal class TestNimbusMessagingController(
    private val messages: List<Message> = emptyList(),
    private val actionIntent: Intent = Intent(),
) : NimbusMessagingControllerInterface {
    override suspend fun getMessages(): List<Message> = messages

    override suspend fun getMessage(id: String): Message? = messages.firstOrNull { it.id == id }

    override fun getNextMessage(
        surfaceId: MessageSurfaceId,
        messages: List<Message>,
        triggerContext: Map<String, Any?>?,
    ): Message? = messages.firstOrNull { it.surface == surfaceId }

    override suspend fun getNextMessage(surfaceId: MessageSurfaceId): Message? = getNextMessage(surfaceId, messages)

    override fun getIntentForMessage(message: Message): Intent = actionIntent

    override suspend fun onMessageDisplayed(displayedMessage: Message, bootIdentifier: String?): Message =
        displayedMessage

    override suspend fun onMessageDismissed(message: Message) = Unit

    override suspend fun onMessageClicked(message: Message) = Unit

    override suspend fun onMicrosurveyCompleted(message: Message, answer: String) = Unit

    override suspend fun onMicrosurveyShown(id: String) = Unit

    override suspend fun onMicrosurveyPrivacyNoticeTapped(id: String) = Unit

    override suspend fun onMicrosurveyDismissed(message: Message) = Unit

    override suspend fun onMicrosurveySentConfirmationShown(id: String) = Unit

    override suspend fun onMicrosurveyStarted(id: String) = Unit
}
