/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.voices

import java.util.Locale

/**
 * Metadata defining a narrator voice.
 *
 * @property id The engine's own name for the voice, which is what it is selected and saved by.
 * @property locale The language and region the voice reads in. The list offers one voice per region.
 */
data class Voice(val id: String, val locale: Locale) {
    /**
     * The voice as the user reads it, for example "English (United Kingdom)".
     *
     * Written in the display language of the device rather than in the language of the voice, so that a reader who does
     * not know the article language can still tell which region each entry belongs to.
     */
    val displayName: String
        get() = locale.displayName
}
