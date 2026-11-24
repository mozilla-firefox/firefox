/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

/**
 * The player elements that accessibility services can focus.
 *
 * @property testTag Identifies the element in the semantics tree, and to accessibility services as its view id.
 */
internal enum class PlayerControl(val testTag: String) {
    Close("listenCloseButton"),
    Heading("listenArticleHeading"),
    Seek(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED),
    Voices("listenVoicesButton"),
    Rewind("listenRewindButton"),
    PlayPause(PLAY_PAUSE_BUTTON_TEST_TAG),
    Forward("listenForwardButton"),
    Speed("listenSpeedButton");

    /** The element of the compact player that takes over from this one when the player collapses. */
    val compactCounterpart: PlayerControl
        get() =
            when (this) {
                Seek,
                Voices -> Rewind
                Forward,
                Speed -> PlayPause
                Close,
                Heading,
                Rewind,
                PlayPause -> this
            }

    companion object {
        /** Returns the element with the given [testTag], or `null` if it is not one of the player's. */
        fun fromTestTag(testTag: String?): PlayerControl? = entries.firstOrNull { it.testTag == testTag }
    }
}
