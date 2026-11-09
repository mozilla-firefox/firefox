/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state

import mozilla.components.concept.engine.mediasession.MediaSession

/**
 * Value type representing a media session on a website.
 *
 * @property controller The [MediaSession.Controller] for controlling playback of this media session.
 * @property metadata The [MediaSession.Metadata] for this media session.
 * @property elementMetadata The [MediaSession.ElementMetadata] for this media session.
 * @property playbackState The current simplified [MediaSession.PlaybackState] of this media session.
 * @property features The [MediaSession.Feature] for this media session.
 * @property positionState The current simplified [MediaSession.PositionState] of this media session. Its position is
 *   reset to 0 when the title in [metadata] changes.
 * @property muted True if media session is muted.
 * @property fullscreen True if media session is fullscreen.
 * @property timestamp The timestamp of when [MediaSessionState] was created.
 */
data class MediaSessionState(
    val controller: MediaSession.Controller,
    val metadata: MediaSession.Metadata? = null,
    val elementMetadata: MediaSession.ElementMetadata? = null,
    val playbackState: MediaSession.PlaybackState = MediaSession.PlaybackState.UNKNOWN,
    val features: MediaSession.Feature = MediaSession.Feature(),
    val positionState: MediaSession.PositionState = MediaSession.PositionState(),
    val muted: Boolean = false,
    val fullscreen: Boolean = false,
    val audioSessionType: MediaSession.AudioSessionType = MediaSession.AudioSessionType.AUTO,
    val timestamp: Long = System.currentTimeMillis(),
) : Comparable<MediaSessionState> {
    override operator fun compareTo(other: MediaSessionState): Int {
        if (playbackState == other.playbackState) {
            return timestamp.compareTo(other.timestamp)
        }

        return playbackState.compareTo(other.playbackState)
    }
}
