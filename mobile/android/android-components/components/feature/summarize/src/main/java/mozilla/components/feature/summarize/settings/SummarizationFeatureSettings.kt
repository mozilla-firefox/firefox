/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

/**
 * Interface for managing page summarization settings.
 *
 * Shared between the settings screen and the summarization bottom sheet.
 */
interface SummarizationFeatureSettings {
    /**
     * Whether page summarization is enabled by the user.
     */
    var summarizePagesEnabled: Boolean

    /**
     * Whether the shake-to-summarize gesture is enabled by the user.
     */
    var shakeToSummarizeEnabled: Boolean
}
