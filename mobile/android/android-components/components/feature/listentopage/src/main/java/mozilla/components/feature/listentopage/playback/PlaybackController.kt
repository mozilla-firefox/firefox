/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.os.Bundle
import androidx.core.content.ContextCompat
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import androidx.media3.session.SessionCommand
import androidx.media3.session.SessionResult
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import mozilla.components.feature.listentopage.ChunkState
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.R
import mozilla.components.support.ktx.android.content.appName

/**
 * What the playback notification and the lock screen say is being read out.
 *
 * @property title The article title, or `null` when the page has none, which [withAppNameIfUntitled] then fills in.
 * @property site Where the article is from.
 * @property isPrivate Whether the article is from a private tab, which [hiddenIfPrivate] then hides.
 */
data class ArticleDisplayData(val title: String? = null, val site: String? = null, val isPrivate: Boolean = false)

/** [privateTitle] and [privateSite] in place of the title and site of an article from a private tab. */
internal fun ArticleDisplayData.hiddenIfPrivate(privateTitle: String, privateSite: String): ArticleDisplayData =
    if (isPrivate) copy(title = privateTitle, site = privateSite) else this

/**
 * [appName] as the title where the page gave none.
 *
 * Media3 passes a null title straight to the notification, which leaves its title line empty. The name is put on here
 * rather than where the article is described, so that the store keeps saying the page itself had no title.
 */
internal fun ArticleDisplayData.withAppNameIfUntitled(appName: String): ArticleDisplayData =
    if (title != null) this else copy(title = appName)

/** How often the position is sampled while playing. It is only published once it reaches a new second. */
internal const val POSITION_SAMPLE_INTERVAL_MS = 500L

/** Commands the playback of the synthesized audio. */
interface PlaybackController {
    /**
     * What the playback is doing, as the player reports it.
     *
     * It holds a default [PlaybackState] while no playback is connected.
     */
    val status: StateFlow<PlaybackState>

    /** Positions into the article that the notification or the lock screen asked to move to. */
    val seekRequests: Flow<Long>
        get() = emptyFlow()

    /**
     * Emits when the playback ends without [release] ending it, such as when the app is removed from the recent apps or
     * the notification is dismissed.
     */
    val sessionEnded: Flow<Unit>
        get() = emptyFlow()

    /** Tells the notification how long the whole article lasts. Does nothing while no playback is connected. */
    suspend fun setArticleDuration(durationMs: Long)

    /**
     * Starts reading an article out from [chunk], its opening, replacing anything already playing, as
     * [articleDisplayData] describes it.
     *
     * @param tabId The tab the article is from, which tapping the notification opens.
     */
    suspend fun startArticle(chunk: ChunkFile, articleDisplayData: ArticleDisplayData, tabId: String)

    /** Adds [chunk] to the end of what is queued, to be read out once the rest has been. */
    suspend fun enqueue(chunk: ChunkFile)

    /** Pauses playback, keeping the position. */
    suspend fun pause()

    /** Resumes playback from the position it was paused at. */
    suspend fun resume()

    /**
     * Moves playback to [positionMs] into the article, inside [chunkIndex].
     *
     * @return Whether the player holds the chunk at [chunkIndex]. When it does not, nothing moved, and playback has to
     *   be restarted there.
     */
    suspend fun seekTo(chunkIndex: Int, positionMs: Long): Boolean

    /**
     * Drops what is queued and starts again on [chunk] at [positionMs] into the article.
     *
     * @param chunkStartMs Where [chunk] starts in the article.
     */
    suspend fun restartAt(chunk: ChunkFile, chunkStartMs: Long, positionMs: Long)

    /**
     * Stops the audio and reports buffering at [positionMs] into the article until [restartAt] starts the playlist
     * there, for a seek whose chunk has to be made first.
     */
    suspend fun stopAndBufferAt(positionMs: Long)

    /**
     * Stops the audio where a seek finished the article, and shows [positionMs], its end, until playback is moved or
     * started again. A play from outside the app, such as the notification, then starts the article again from the top.
     */
    suspend fun finishAt(positionMs: Long)

    /** Reads the rest of the article out at [speed]. */
    suspend fun setSpeed(speed: PlaybackSpeed)

    /** Gives up the playback, which takes the notification away. A later call starts it again. */
    suspend fun release()
}

/**
 * A [PlaybackController] that drives [ListenMediaSessionService] through a [MediaController].
 *
 * This is the client half of the media session, and the only place in the module that holds a [MediaController]. It
 * exists so that a caller can drive playback without naming a media3 type, which a caller in Fenix cannot resolve.
 *
 * A [MediaController] may only be used on the main thread, so every method here moves to it. The connection is made on
 * first use and reused afterwards.
 *
 * @param context Used to connect to the session.
 * @param scope The [CoroutineScope] the connection is held in.
 * @param sessionToken Finds the session to connect to.
 * @param sessionActivity Is lambda to construct an intent to open the tab on notification click.
 */
class ListenPlaybackController
internal constructor(
    private val context: Context,
    private val scope: CoroutineScope,
    private val sessionToken: suspend () -> SessionToken,
    private val sessionActivity: (tabId: String) -> PendingIntent? = { null },
) : PlaybackController {

    /**
     * Connects to [ListenMediaSessionService].
     *
     * @param context Used to connect to [ListenMediaSessionService].
     * @param scope The [CoroutineScope] the connection is held in.
     * @param sessionActivity Is lambda to construct an intent to open the tab on notification click.
     * @param ioDispatcher The dispatcher the service lookup runs on.
     */
    constructor(
        context: Context,
        scope: CoroutineScope,
        sessionActivity: (tabId: String) -> PendingIntent? = { null },
        ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    ) : this(
        context = context,
        scope = scope,
        sessionToken = {
            // The SessionToken constructor asks the package manager to resolve the service, which is a blocking IPC.
            withContext(ioDispatcher) {
                SessionToken(context, ComponentName(context, ListenMediaSessionService::class.java))
            }
        },
        sessionActivity = sessionActivity,
    )

    // Main thread only, like the controller it produces.
    private var connection: Deferred<MediaController>? = null

    private val _status = MutableStateFlow(PlaybackState())
    override val status: StateFlow<PlaybackState> = _status.asStateFlow()

    private val _seekRequests =
        MutableSharedFlow<Long>(extraBufferCapacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    override val seekRequests: SharedFlow<Long> = _seekRequests.asSharedFlow()

    private val _sessionEnded =
        MutableSharedFlow<Unit>(extraBufferCapacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    override val sessionEnded: SharedFlow<Unit> = _sessionEnded.asSharedFlow()

    // The length of the article last given to the session, which a restart carries on.
    private var articleDurationMs = 0L

    override suspend fun setArticleDuration(durationMs: Long) {
        withContext(Dispatchers.Main) {
            // Never connects: a report that arrives after the playback was released must not start it again.
            val connected = connection ?: return@withContext
            val controller = runCatching { connected.await() }.getOrNull() ?: return@withContext
            if (durationMs == articleDurationMs) return@withContext

            articleDurationMs = durationMs
            controller.sendCustomCommand(
                SessionCommand(COMMAND_SET_ARTICLE_DURATION, Bundle.EMPTY),
                Bundle().apply { putLong(KEY_DURATION_MS, durationMs) },
            )
        }
    }

    // Main thread only, like the controller it samples.
    private var positionJob: Job? = null

    // What the article now playing says it is, kept so that every chunk of it is queued saying the same thing.
    private var displayData: ArticleDisplayData? = null

    private val appName: String by lazy { context.appName }

    private val privateTitle: String by lazy {
        context.getString(R.string.mozac_feature_listentopage_private_tab_title)
    }

    private val privateSite: String by lazy {
        context.getString(R.string.mozac_feature_listentopage_private_tab_site)
    }

    private val playerListener =
        object : Player.Listener {
            // onEvents rather than the individual callbacks: it runs once per batch of changes, where the separate
            // callbacks would each publish a snapshot of the same batch.
            override fun onEvents(player: Player, events: Player.Events) {
                _status.value = player.toPlaybackState()
                samplePosition(player)
            }
        }

    override suspend fun startArticle(chunk: ChunkFile, articleDisplayData: ArticleDisplayData, tabId: String) =
        onController {
            // Published before the command, so that a report the previous session left behind cannot be read as this
            // session's in the time it takes the player to report for itself.
            _status.value =
                PlaybackState(
                    phase = PlaybackPhase.Buffering,
                    speed = PlaybackSpeed.nearest(it.playbackParameters.speed),
                )

            val shownDisplayData =
                articleDisplayData.hiddenIfPrivate(privateTitle, privateSite).withAppNameIfUntitled(appName)
            displayData = shownDisplayData

            it.sendCustomCommand(
                SessionCommand(COMMAND_SET_SESSION_ACTIVITY, Bundle.EMPTY),
                sessionActivity(tabId).toSessionActivityArgs(),
            )

            // A new article starts at its top, with no length until one is worked out.
            articleDurationMs = 0
            it.setMediaItem(chunk.toMediaItem(displayData = shownDisplayData, articleTimeline = ArticleTimeline()))
            it.prepare()
            it.play()
        }

    override suspend fun enqueue(chunk: ChunkFile) = onController {
        val displayData = this.displayData ?: return@onController
        it.addMediaItem(chunk.toMediaItem(displayData))

        if (it.playbackState == Player.STATE_ENDED) {
            it.seekTo(it.mediaItemCount - 1, it.currentPosition)
        }
    }

    override suspend fun pause() = onController { it.pause() }

    override suspend fun resume() = onController {
        it.prepare()
        it.play()
    }

    override suspend fun seekTo(chunkIndex: Int, positionMs: Long): Boolean {
        var hasChunk = false
        onController {
            val item =
                (0 until it.mediaItemCount).firstOrNull { index -> it.getMediaItemAt(index).chunkIndex() == chunkIndex }
            if (item != null) {
                it.seekTo(item, positionMs)
                hasChunk = true
            }
        }
        return hasChunk
    }

    override suspend fun restartAt(chunk: ChunkFile, chunkStartMs: Long, positionMs: Long) = onController {
        val displayData = this.displayData ?: return@onController
        val articleTimeline = ArticleTimeline(playlistStartMs = chunkStartMs, durationMs = articleDurationMs)

        // The position goes in with the item rather than as a seek afterwards, which would let the player start at
        // the top of the chunk and be moved off it a moment later.
        it.setMediaItem(chunk.toMediaItem(displayData, articleTimeline), positionMs)

        // Prepared but not played: a player that was paused stays paused.
        it.prepare()
    }

    override suspend fun finishAt(positionMs: Long) = onController {
        it.sendCustomCommand(
            SessionCommand(COMMAND_FINISH_AT, Bundle.EMPTY),
            Bundle().apply { putLong(KEY_POSITION_MS, positionMs) },
        )
    }

    // The session stops the player itself once it has the position to buffer at.
    override suspend fun stopAndBufferAt(positionMs: Long) = onController {
        it.sendCustomCommand(
            SessionCommand(COMMAND_STOP_AND_BUFFER_AT, Bundle.EMPTY),
            Bundle().apply { putLong(KEY_POSITION_MS, positionMs) },
        )
    }

    override suspend fun setSpeed(speed: PlaybackSpeed) = onController { it.setPlaybackSpeed(speed.multiplier) }

    override suspend fun release() {
        withContext(Dispatchers.Main) {
            val released = connection ?: return@withContext
            connection = null
            forgetStatus()

            // A connection that failed has no controller to release.
            val controller = runCatching { released.await() }.getOrNull() ?: return@withContext

            controller.stop()
            controller.clearMediaItems()
            controller.release()
        }
    }

    /**
     * Keeps [status] a second fresh while the audio plays, and stops sampling when it does not.
     *
     * The player reports the events but pushes no position, so the position has to be read. Only whole-second changes
     * are published.
     */
    private fun samplePosition(player: Player) {
        if (!player.isPlaying) {
            positionJob?.cancel()
            positionJob = null
            return
        }

        if (positionJob?.isActive == true) return

        positionJob =
            scope.launch(Dispatchers.Main) {
                while (true) {
                    // We sample faster than is required for publishing to ensure that data is up to date.
                    delay(POSITION_SAMPLE_INTERVAL_MS.milliseconds)

                    val sampled = player.currentPosition
                    _status.update {
                        if (it.positionMs / MS_PER_SECOND == sampled / MS_PER_SECOND) {
                            it
                        } else {
                            it.copy(positionMs = sampled)
                        }
                    }
                }
            }
    }

    /** Drops what the last connection was doing, so that nobody is left watching a session that is gone. */
    private fun forgetStatus() {
        positionJob?.cancel()
        positionJob = null
        displayData = null
        articleDurationMs = 0
        _status.value = PlaybackState()
    }

    private suspend fun onController(command: (MediaController) -> Unit) =
        withContext(Dispatchers.Main) {
            command(connection?.await() ?: newConnection().await())
        }

    /**
     * Starts connecting, and keeps the attempt so that later commands reuse it.
     *
     * A failed attempt drops itself rather than being cached, because otherwise the first failure is replayed to every
     * later command and the session can never recover without restarting the process.
     */
    private fun newConnection(): Deferred<MediaController> =
        scope
            .async(Dispatchers.Main) { connect() }
            .also { connecting ->
                connection = connecting
                connecting.invokeOnCompletion { failure ->
                    if (failure != null && connection === connecting) {
                        connection = null
                    }
                }
            }

    private suspend fun connect(): MediaController {
        val token = sessionToken()
        val pending =
            MediaController.Builder(context, token)
                .setListener(
                    object : MediaController.Listener {
                        // The session goes away on its own, so a kept controller can go stale: media3 stops the
                        // service once the article ends with nobody commanding it. Commands sent to a disconnected
                        // controller are dropped without a word, so the next request has to build a new one.
                        override fun onDisconnected(controller: MediaController) {
                            // A connection still kept here means the session ended without being asked to.
                            val isConnected = connection != null
                            connection = null
                            forgetStatus()
                            if (isConnected) _sessionEnded.tryEmit(Unit)
                        }

                        override fun onCustomCommand(
                            controller: MediaController,
                            command: SessionCommand,
                            args: Bundle,
                        ): ListenableFuture<SessionResult> {
                            if (command.customAction == COMMAND_SEEK_ARTICLE) {
                                _seekRequests.tryEmit(args.getLong(KEY_POSITION_MS))
                            }
                            return Futures.immediateFuture(SessionResult(SessionResult.RESULT_SUCCESS))
                        }
                    }
                )
                .buildAsync()

        val controller = suspendCancellableCoroutine { continuation ->
            // The listener only runs once the future is done, so `get` returns rather than waits. Whichever
            // terminal state it reports, success, failure or cancellation, belongs to the coroutine that is waiting
            // for it.
            pending.addListener(
                { continuation.resumeWith(runCatching { pending.get() }) },
                ContextCompat.getMainExecutor(context),
            )

            continuation.invokeOnCancellation { MediaController.releaseFuture(pending) }
        }

        controller.addListener(playerListener)
        _status.value = controller.toPlaybackState()

        return controller
    }

    private companion object {
        const val MS_PER_SECOND = 1000L
    }
}

private fun Player.toPlaybackState() =
    PlaybackState(
        phase = toPlaybackPhase(),
        chunk =
            ChunkState(
                index = currentMediaItem?.chunkIndex() ?: currentMediaItemIndex,
                durationMs = currentMediaItem?.measuredDurationMs(),
            ),
        positionMs = currentPosition,
        speed = PlaybackSpeed.nearest(playbackParameters.speed),
    )

/** The phase the player is in. */
internal fun Player.toPlaybackPhase() =
    when {
        playerError != null -> PlaybackPhase.Failed
        playbackState == Player.STATE_IDLE -> PlaybackPhase.Idle
        playbackState == Player.STATE_ENDED -> PlaybackPhase.Ended
        playbackState == Player.STATE_BUFFERING && playWhenReady -> PlaybackPhase.Buffering
        isPlaying -> PlaybackPhase.Playing
        else -> PlaybackPhase.Paused
    }
