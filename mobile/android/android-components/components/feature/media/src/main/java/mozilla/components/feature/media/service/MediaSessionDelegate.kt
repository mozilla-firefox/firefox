/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.service

import mozilla.components.browser.state.state.SessionState

/** A delegate for handling all possible media states of a [SessionState]. */
interface MediaSessionDelegate {
    /** Handle media playing in the passed in [sessionState]. */
    fun handleMediaPlaying(sessionState: SessionState)

    /** Handle media being paused in the passed in [sessionState]. */
    fun handleMediaPaused(sessionState: SessionState)

    /** Handle media being stopped in the passed in [sessionState]. */
    fun handleMediaStopped(sessionState: SessionState)

    /** Refresh the media shown for [sessionState] after a change that leaves its media session state untouched. */
    fun handleMediaUpdated(sessionState: SessionState)

    /** Handle no media available. */
    fun handleNoMedia()
}
