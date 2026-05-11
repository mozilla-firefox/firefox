/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.focus

import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager

/**
 * [AudioFocusController] implementation for Android API 26+.
 */
internal class AudioFocusControllerV26(
    private val audioManager: AudioManager,
    listener: AudioManager.OnAudioFocusChangeListener,
) : AudioFocusController {
    private val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
        .setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build(),
        )
        .setAcceptsDelayedFocusGain(true)
        .setWillPauseWhenDucked(false)
        .setOnAudioFocusChangeListener(listener)
        .build()

    override fun request(): Int {
        return audioManager.requestAudioFocus(request)
    }

    override fun abandon() {
        audioManager.abandonAudioFocusRequest(request)
    }
}
