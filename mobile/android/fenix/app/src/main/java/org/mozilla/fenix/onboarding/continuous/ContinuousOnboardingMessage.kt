/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import mozilla.components.service.nimbus.messaging.Message
import org.mozilla.fenix.R

private const val MAKE_DEFAULT_BROWSER = "://make_default_browser"
private const val OPEN_SETTINGS_IP_PROTECTION = "://settings_ip_protection"
private const val CONTINUOUS_ONBOARDING_STAGE_COMPLETED = "://continuous_onboarding_stage_completed"

/**
 * Wrapper type around a [Message] specific to the continuous onboarding surface.
 *
 * All continuous onboarding messages share one surface, so this type exists to tell what a message is for, from its
 * action, and to add what a message can't describe.
 *
 * Messages that show nothing of their own use the known [EMPTY_CONTINUOUS_ONBOARDING_MESSAGE_BODY] text, as a message's
 * text can't be blank.
 */
sealed interface ContinuousOnboardingMessage {
    /** Original nimbus message. */
    val original: Message

    /** Requests the default browser role with the system's prompt, for the `MAKE_DEFAULT_BROWSER` action. */
    data class DefaultBrowserRoleRequest(override val original: Message) : ContinuousOnboardingMessage

    /**
     * A card with an image and two buttons.
     *
     * @property title Title of the card.
     * @property text Body of the card.
     * @property primaryButtonLabel Text of the button that performs the card's action.
     * @property type The kind of card, which adds its image and its secondary button.
     */
    data class Card(
        val title: String,
        val text: String,
        val primaryButtonLabel: String,
        val type: Type,
        override val original: Message,
    ) : ContinuousOnboardingMessage {
        /**
         * The cards, by the action of their primary button.
         *
         * @property deepLink The action's deep link, as in the `actions` of `messaging-fenix.fml.yaml`.
         * @property imageRes The image shown on the card.
         * @property secondaryButtonLabel The text of the button that dismisses the card.
         */
        enum class Type(
            val deepLink: String,
            @param:DrawableRes val imageRes: Int,
            @param:StringRes val secondaryButtonLabel: Int,
        ) {
            REQUEST_NOTIFICATION_PERMISSION(
                deepLink = "://request_notification_permission",
                imageRes = R.drawable.nova_onboarding_notifications,
                secondaryButtonLabel = R.string.nova_onboarding_negative_button,
            ),
            TURN_ON_SYNC(
                deepLink = "://turn_on_sync",
                imageRes = R.drawable.nova_onboarding_sync,
                secondaryButtonLabel = R.string.nova_onboarding_continue_button,
            ),
        }
    }

    /**
     * Shows the IP Protection onboarding prompt, whose content is owned by the IP Protection feature, for the
     * `OPEN_SETTINGS_IP_PROTECTION` action.
     */
    data class IpProtectionPrompt(override val original: Message) : ContinuousOnboardingMessage

    /**
     * Shows nothing: displaying it records that a stage is over, e.g. because the stage had nothing to ask for, for the
     * `CONTINUOUS_ONBOARDING_STAGE_COMPLETED` action.
     */
    data class StageCompleted(override val original: Message) : ContinuousOnboardingMessage

    companion object {
        /**
         * Creates a continuous onboarding message from a nimbus message.
         *
         * Returns `null` if the message's action is not one continuous onboarding presents, or for a card, if the
         * message has no text or button label.
         *
         * @param message Original nimbus message.
         */
        fun createFrom(message: Message): ContinuousOnboardingMessage? =
            when (message.action) {
                MAKE_DEFAULT_BROWSER -> DefaultBrowserRoleRequest(message)
                OPEN_SETTINGS_IP_PROTECTION -> IpProtectionPrompt(message)
                CONTINUOUS_ONBOARDING_STAGE_COMPLETED -> StageCompleted(message)
                else -> Card.Type.entries.find { it.deepLink == message.action }?.let { createCard(message, it) }
            }

        private fun createCard(message: Message, type: Card.Type): Card? {
            val text =
                message.text.takeUnless { it == EMPTY_CONTINUOUS_ONBOARDING_MESSAGE_BODY || it.isBlank() } ?: return null
            val primaryButtonLabel = message.buttonLabel?.takeUnless { it.isBlank() } ?: return null

            return Card(
                title = message.title.orEmpty(),
                text = text,
                primaryButtonLabel = primaryButtonLabel,
                type = type,
                original = message,
            )
        }
    }
}

internal const val EMPTY_CONTINUOUS_ONBOARDING_MESSAGE_BODY = "CONTINUOUS_ONBOARDING_MESSAGE_EMPTY_BODY"
