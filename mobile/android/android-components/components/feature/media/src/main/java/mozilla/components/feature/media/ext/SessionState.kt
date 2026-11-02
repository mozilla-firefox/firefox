/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.ext

import android.content.Context
import android.graphics.Bitmap
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.R

internal fun SessionState?.mediaDisplayKey(context: Context): Any? =
    when {
        this == null -> null
        content.private -> mediaSessionState
        else -> {
            val metadata = mediaSessionState?.metadata
            listOf(
                mediaSessionState,
                getTitleOrUrl(context, metadata?.title),
                getArtistOrUrl(metadata?.artist),
                content.icon,
            )
        }
    }

internal fun SessionState?.getTitleOrUrl(context: Context, title: String? = null): String =
    when {
        this == null -> context.getString(R.string.mozac_feature_media_notification_private_mode)
        content.private -> context.getString(R.string.mozac_feature_media_notification_private_mode)
        title != null -> title
        content.title.isNotEmpty() -> content.title
        else -> content.url
    }

internal fun SessionState?.getArtistOrUrl(artist: String? = null): String =
    when {
        this == null || content.private -> ""
        artist != null -> artist
        else -> content.url
    }

@Suppress("TooGenericExceptionCaught")
internal suspend fun SessionState?.getNonPrivateIcon(getArtwork: (suspend () -> Bitmap?)?): Bitmap? =
    when {
        this == null -> null
        content.private -> null
        getArtwork != null -> getArtwork() ?: content.icon
        else -> content.icon
    }

/**
 * Finds the [SessionState] (tab or custom tab) that has an active media session. Returns `null` if no tab has a media
 * session attached.
 */
fun BrowserState.findActiveMediaTab(): SessionState? {
    return (tabs.asSequence() + customTabs.asSequence())
        .filter { tab ->
            tab.mediaSessionState != null && tab.mediaSessionState!!.playbackState != MediaSession.PlaybackState.UNKNOWN
        }
        .sortedByDescending { tab ->
            tab.mediaSessionState
        }
        .firstOrNull()
}
