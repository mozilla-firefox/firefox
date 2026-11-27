/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.nimbus.messaging

import androidx.annotation.VisibleForTesting

/**
 * A data class that holds a representation of a message from Nimbus.
 *
 * In order to be eligible to be shown, all `triggerIfAll` expressions AND none of the `excludeIfAny` expressions must
 * evaluate to `true`.
 *
 * @property id identifies a message as unique.
 * @property data Data information provided from Nimbus.
 * @property action A strings that represents which action should be performed after a message is clicked.
 * @property style Indicates how a message should be styled.
 * @property triggerIfAll A list of strings corresponding to JEXL targeting expressions. The message will be shown if
 *   _all_ expressions evaluate to `true`.
 * @property excludeIfAny A list of strings corresponding to JEXL targeting expressions. The message will _not_ be shown
 *   if _any_ expressions evaluate to `true`.
 * @property metadata Metadata that help to identify if a message should shown.
 */
data class Message(
    val id: String,
    internal val data: MessageData,
    val action: String,
    internal val style: StyleData,
    internal val triggerIfAll: List<String>,
    internal val excludeIfAny: List<String> = listOf(),
    internal val metadata: Metadata,
) {
    val text: String
        get() = data.text

    val title: String?
        get() = data.title

    val buttonLabel: String?
        get() = data.buttonLabel

    val priority: Int
        get() = style.priority

    val surface: MessageSurfaceId
        get() = data.surface

    val isExpired: Boolean
        get() = metadata.displayCount >= style.maxDisplayCount

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    val displayCount: Int
        get() = metadata.displayCount

    val microsurvey: MicrosurveyConfig?
        get() = data.microsurveyConfig

    /**
     * Returns true if the passed boot id, taken from [BootUtils] matches the one associated with this message when it
     * was last displayed.
     */
    fun hasShownThisCycle(bootId: String) = bootId == metadata.latestBootIdentifier

    /**
     * A data class that holds metadata that help to identify if a message should shown.
     *
     * @param id identifies a message as unique.
     * @param displayCount Indicates how many times a message is displayed.
     * @param pressed Indicates if a message has been clicked.
     * @param dismissed Indicates if a message has been closed.
     * @param lastTimeShown A timestamp indicating when was the last time, the message was shown.
     * @param latestBootIdentifier A unique boot identifier for when the message was last displayed (this may be a boot
     *   count or a boot id).
     */
    data class Metadata(
        val id: String,
        val displayCount: Int = 0,
        val pressed: Boolean = false,
        val dismissed: Boolean = false,
        val lastTimeShown: Long = 0L,
        val latestBootIdentifier: String? = null,
    )
}
