/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import io.mockk.every
import io.mockk.mockk
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.StyleData
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingMessage.Card

private const val REQUEST_NOTIFICATION_PERMISSION = "://request_notification_permission"
private const val TURN_ON_SYNC = "://turn_on_sync"

class ContinuousOnboardingMessageTest {
    @Test
    fun `WHEN the action makes the app the default browser THEN the message is a default browser role request`() {
        val message = message(action = "://make_default_browser")

        assertEquals(
            ContinuousOnboardingMessage.DefaultBrowserRoleRequest(message),
            ContinuousOnboardingMessage.createFrom(message),
        )
    }

    @Test
    fun `WHEN the action opens the IP Protection settings THEN the message is the IP Protection prompt`() {
        val message = message(action = "://settings_ip_protection")

        assertEquals(
            ContinuousOnboardingMessage.IpProtectionPrompt(message),
            ContinuousOnboardingMessage.createFrom(message),
        )
    }

    @Test
    fun `WHEN the action completes a stage THEN the message is a stage completion, whatever its text`() {
        val message =
            message(action = "://continuous_onboarding_stage_completed", text = EMPTY_CONTINUOUS_ONBOARDING_MESSAGE_BODY)

        assertEquals(ContinuousOnboardingMessage.StageCompleted(message), ContinuousOnboardingMessage.createFrom(message))
    }

    @Test
    fun `WHEN the action requests the notification permission THEN the message is the notification card`() {
        val message = message(action = REQUEST_NOTIFICATION_PERMISSION)

        val card = ContinuousOnboardingMessage.createFrom(message) as Card

        assertEquals(Card("title", "text", "button", Card.Type.REQUEST_NOTIFICATION_PERMISSION, message), card)
        assertEquals(R.drawable.nova_onboarding_notifications, card.type.imageRes)
        assertEquals(R.string.nova_onboarding_negative_button, card.type.secondaryButtonLabel)
    }

    @Test
    fun `WHEN the action turns on sync THEN the message is the sync card`() {
        val card = ContinuousOnboardingMessage.createFrom(message(action = TURN_ON_SYNC)) as Card

        assertEquals(Card.Type.TURN_ON_SYNC, card.type)
        assertEquals(R.drawable.nova_onboarding_sync, card.type.imageRes)
        assertEquals(R.string.nova_onboarding_continue_button, card.type.secondaryButtonLabel)
    }

    @Test
    fun `WHEN a card message has no title THEN the card's title is empty`() {
        val card = ContinuousOnboardingMessage.createFrom(message(title = null)) as Card

        assertEquals("", card.title)
    }

    @Test
    fun `WHEN a card message has no button label THEN there is no message`() {
        assertNull(ContinuousOnboardingMessage.createFrom(message(buttonLabel = null)))
        assertNull(ContinuousOnboardingMessage.createFrom(message(buttonLabel = " ")))
    }

    @Test
    fun `WHEN a card message has no text of its own THEN there is no message`() {
        assertNull(ContinuousOnboardingMessage.createFrom(message(text = EMPTY_CONTINUOUS_ONBOARDING_MESSAGE_BODY)))
        assertNull(ContinuousOnboardingMessage.createFrom(message(text = " ")))
    }

    @Test
    fun `WHEN continuous onboarding doesn't present the action THEN there is no message`() {
        assertNull(ContinuousOnboardingMessage.createFrom(message(action = "://settings")))
        assertNull(ContinuousOnboardingMessage.createFrom(message(action = "://open")))
    }

    private fun message(
        title: String? = "title",
        text: String = "text",
        buttonLabel: String? = "button",
        action: String = TURN_ON_SYNC,
    ): Message {
        val data: MessageData = mockk(relaxed = true)
        every { data.title } returns title
        every { data.text } returns text
        every { data.buttonLabel } returns buttonLabel
        return Message("id", data, action, StyleData(), emptyList(), emptyList(), Message.Metadata("id"))
    }
}
