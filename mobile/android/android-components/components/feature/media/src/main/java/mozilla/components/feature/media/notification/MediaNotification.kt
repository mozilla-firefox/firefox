/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.notification

import android.app.Notification
import android.app.PendingIntent
import android.app.PendingIntent.FLAG_UPDATE_CURRENT
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import androidx.annotation.DrawableRes
import androidx.annotation.OptIn
import androidx.annotation.StringRes
import androidx.core.app.NotificationCompat
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaStyleNotificationHelper
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.mediasession.MediaSession as MozMediaSession
import mozilla.components.feature.media.R
import mozilla.components.feature.media.ext.getArtistOrUrl
import mozilla.components.feature.media.ext.getTitleOrUrl
import mozilla.components.feature.media.service.AbstractMediaSessionService
import mozilla.components.support.base.ids.SharedIdsHelper

/** Helper to display a notification for web content playing media. */
@OptIn(UnstableApi::class)
internal class MediaNotification(
    private val context: Context,
    private val cls: Class<*>,
) {
    /** Creates a new [Notification] for the given [sessionState], showing [artwork] as its large icon. */
    fun create(sessionState: SessionState?, mediaSession: MediaSession, artwork: Bitmap?): Notification {
        val data = sessionState?.toNotificationData(context, cls, artwork) ?: NotificationData()

        return buildNotification(data, mediaSession, sessionState !is CustomTabSessionState)
    }

    private fun buildNotification(
        data: NotificationData,
        mediaSession: MediaSession,
        isNormalTab: Boolean,
    ): Notification {
        val channel = MediaNotificationChannel.ensureChannelExists(context)
        val style = MediaStyleNotificationHelper.MediaStyle(mediaSession)
        val builder =
            NotificationCompat.Builder(context, channel)
                .setSmallIcon(data.icon)
                .setContentTitle(data.title)
                .setContentText(data.description)
                .setLargeIcon(data.largeIcon)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)

        data.actions.all.forEach { builder.addAction(it) }
        if (data.actions.compactIndices.isNotEmpty()) {
            @Suppress("SpreadOperator") style.setShowActionsInCompactView(*data.actions.compactIndices)
        }
        builder.setStyle(style)
        if (isNormalTab) {
            // We only set a content intent if this media notification is not for an "external app"
            // like a custom tab. Currently we can't route the user to that particular activity:
            // https://github.com/mozilla-mobile/android-components/issues/3986
            builder.setContentIntent(data.contentIntent)
        }

        return builder.build()
    }
}

private fun SessionState.toNotificationData(
    context: Context,
    cls: Class<*>,
    artwork: Bitmap?,
): NotificationData {
    val intent =
        context.packageManager.getLaunchIntentForPackage(context.packageName)?.also {
            it.action = AbstractMediaSessionService.ACTION_SWITCH_TAB
        }

    val mediaState = mediaSessionState ?: return NotificationData()
    val playPauseAction =
        when (mediaState.playbackState) {
            MozMediaSession.PlaybackState.PLAYING ->
                buildAction(
                    context = context,
                    iconRes = R.drawable.mozac_feature_media_action_pause,
                    titleRes = R.string.mozac_feature_media_notification_action_pause,
                    intent = AbstractMediaSessionService.pauseIntent(context, cls),
                )
            MozMediaSession.PlaybackState.PAUSED ->
                buildAction(
                    context = context,
                    iconRes = R.drawable.mozac_feature_media_action_play,
                    titleRes = R.string.mozac_feature_media_notification_action_play,
                    intent = AbstractMediaSessionService.playIntent(context, cls),
                )
            else -> return NotificationData()
        }

    val icon =
        when (mediaState.playbackState) {
            MozMediaSession.PlaybackState.PLAYING -> R.drawable.mozac_feature_media_playing
            else -> R.drawable.mozac_feature_media_paused
        }

    return NotificationData(
        title = getTitleOrUrl(context, mediaState.metadata?.title),
        description = getArtistOrUrl(mediaState.metadata?.artist),
        icon = icon,
        largeIcon = artwork,
        actions = mediaState.buildActions(context, cls, playPauseAction),
        contentIntent =
            PendingIntent.getActivity(
                context,
                SharedIdsHelper.getIdForTag(context, AbstractMediaSessionService.PENDING_INTENT_TAG),
                intent?.apply { putExtra(AbstractMediaSessionService.EXTRA_TAB_ID, id) },
                getUpdateNotificationFlag(),
            ),
    )
}

private fun MediaSessionState.buildActions(
    context: Context,
    cls: Class<*>,
    playPauseAction: NotificationCompat.Action,
): NotificationActions {
    val actions = mutableListOf<NotificationCompat.Action>()
    val compactIndices = mutableListOf<Int>()

    if (features.contains(MozMediaSession.Feature.PREVIOUS_TRACK)) {
        compactIndices += actions.size
        actions +=
            buildAction(
                context = context,
                iconRes = R.drawable.mozac_feature_media_action_previous,
                titleRes = R.string.mozac_feature_media_notification_action_previous,
                intent = AbstractMediaSessionService.previousTrackIntent(context, cls),
            )
    }
    compactIndices += actions.size
    actions += playPauseAction
    if (features.contains(MozMediaSession.Feature.NEXT_TRACK)) {
        compactIndices += actions.size
        actions +=
            buildAction(
                context = context,
                iconRes = R.drawable.mozac_feature_media_action_next,
                titleRes = R.string.mozac_feature_media_notification_action_next,
                intent = AbstractMediaSessionService.nextTrackIntent(context, cls),
            )
    }

    return NotificationActions(actions, compactIndices.toIntArray())
}

private fun buildAction(
    context: Context,
    @DrawableRes iconRes: Int,
    @StringRes titleRes: Int,
    intent: Intent,
): NotificationCompat.Action =
    NotificationCompat.Action.Builder(
            iconRes,
            context.getString(titleRes),
            PendingIntent.getService(
                context,
                0,
                intent,
                getNotificationFlag(),
            ),
        )
        .build()

private data class NotificationData(
    val title: String = "",
    val description: String = "",
    @param:DrawableRes val icon: Int = R.drawable.mozac_feature_media_playing,
    val largeIcon: Bitmap? = null,
    val actions: NotificationActions = NotificationActions(),
    val contentIntent: PendingIntent? = null,
)

private data class NotificationActions(
    val all: List<NotificationCompat.Action> = emptyList(),
    val compactIndices: IntArray = IntArray(0),
)

private fun getNotificationFlag() = PendingIntent.FLAG_IMMUTABLE

private fun getUpdateNotificationFlag() = PendingIntent.FLAG_IMMUTABLE or FLAG_UPDATE_CURRENT
