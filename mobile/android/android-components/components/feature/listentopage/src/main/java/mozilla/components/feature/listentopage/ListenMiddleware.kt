/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import androidx.core.net.toUri
import java.util.Locale
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.isActive
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.content.Content
import mozilla.components.feature.listentopage.content.ContentProvider
import mozilla.components.feature.listentopage.content.TextChunker
import mozilla.components.feature.listentopage.facts.emitEngineBoundFact
import mozilla.components.feature.listentopage.playback.ArticleDisplayData
import mozilla.components.feature.listentopage.playback.AudioFileCache
import mozilla.components.feature.listentopage.playback.ChunkAudio
import mozilla.components.feature.listentopage.playback.ChunkFile
import mozilla.components.feature.listentopage.playback.ChunkPosition
import mozilla.components.feature.listentopage.playback.PlaybackController
import mozilla.components.feature.listentopage.playback.PlaybackIntent
import mozilla.components.feature.listentopage.playback.SEEK_BACK_INCREMENT_MS
import mozilla.components.feature.listentopage.playback.SEEK_FORWARD_INCREMENT_MS
import mozilla.components.feature.listentopage.playback.SynthesisJobPriorityLock
import mozilla.components.feature.listentopage.playback.articlePositionOf
import mozilla.components.feature.listentopage.playback.chunkPositionAt
import mozilla.components.feature.listentopage.playback.offsetInMeasuredChunk
import mozilla.components.feature.listentopage.playback.shownDurationMs
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import mozilla.components.feature.listentopage.settings.ListenSettings
import mozilla.components.feature.listentopage.synthesis.NoOfflineVoiceAvailableException
import mozilla.components.feature.listentopage.synthesis.NothingToReadException
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesizer
import mozilla.components.feature.listentopage.synthesis.SynthesisQueue
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.stripCommonSubdomains

/**
 * [Middleware] that extracts the article a listening session reads out, synthesizes it and plays it.
 *
 * @property contentProvider Provides the article text and language for a tab.
 * @property synthesizerProvider Builds the speech engine. A provider rather than an instance for two reasons: building
 *   one binds the platform engine over IPC, which must not happen on the caller's thread, and [SpeechSynthesizer.close]
 *   is terminal, so a session that closes its engine needs a way to get another one.
 * @property audioCache Holds the audio files. It is emptied when the session stops.
 * @property playbackController Plays the audio file. It is used instead of the player directly, because only playback
 *   commanded through the media session keeps the audio alive in the background and shows the notification.
 * @property settings The module's own preferences, holding the voice the user picked for each article language and
 *   whether the feature is turned on. The session stops when the feature is turned off.
 * @property scope The [CoroutineScope] the extraction, the synthesis and the playback commands run in. It has to
 *   dispatch on one thread: the session fields below are read from `invoke`, which the store runs on whichever thread
 *   dispatched, and written from this scope.
 * @property ioDispatcher The dispatcher for the work that must not run on the thread the store dispatched on.
 * @property chunker Splits article text into the chunks.
 */
@Suppress("TooManyFunctions")
class ListenMiddleware(
    private val browserStore: BrowserStore,
    private val contentProvider: ContentProvider,
    private val synthesizerProvider: () -> SpeechSynthesizer,
    private val audioCache: AudioFileCache,
    private val playbackController: PlaybackController,
    private val settings: ListenSettings,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val chunker: TextChunker = TextChunker.android(),
) : Middleware<ListenState, ListenAction> {

    private val logger = Logger("ListenMiddleware")

    private var contentJob: Job? = null
    private var tabClosureJob: Job? = null
    private var voicesJob: Job? = null
    private var refreshVoicesJob: Job? = null
    private var playbackStatusJob: Job? = null

    // The end of every session so far, chained.
    private var teardownJob: Job? = null

    // The article the session reads out.
    //
    // It carries the tab it was extracted for, so that an extraction finishing after its session ended cannot be read
    // by the session after it. Clearing it on stop is still not optional: the store is a singleton, so the middleware
    // is too, and an article left here would live until the process dies.
    private var article: Article? = null

    // The engine this session reads with, built on first use and closed when the session stops.
    private var synthesizer: SpeechSynthesizer? = null

    // What the session has made of the article, held so that the audio can be thrown away when the session ends.
    private var synthesisQueue: SynthesisQueue? = null

    // Which chunks have been handed to the player. Kept in the store, so that only the reducer changes it.
    private val ListenStore.playlist: Playlist
        get() = state.playlist

    // Lock with associated priorities to manage which playback operations override each other
    private val synthesisJobPriorityLock = SynthesisJobPriorityLock()

    // The chunk the session is making again after a playback failure, and how many times it has tried.
    private var recoveringChunk = NO_CHUNK
    private var recoveryAttempts = 0

    // The scope every piece of this session's synthesis runs in, so that ending the session cancels all of it at once,
    // including work that is still waiting its turn.
    private var sessionScope: CoroutineScope? = null

    // Orders the engine's requests. Work waits its turn rather than replacing what is running, because the engine
    // cannot cancel one request: its own stop() drops everything it holds.
    private val requestTurn = Mutex()

    // Guards building the engine, so that a request arriving while another is still binding cannot leave a second
    // engine bound with nothing to close it.
    private val synthesizerLock = Mutex()

    override fun invoke(
        store: Store<ListenState, ListenAction>,
        next: (ListenAction) -> Unit,
        action: ListenAction,
    ) {
        // Read before the reducer runs, because afterwards the store already holds the voice the action carries and
        // there would be nothing left to compare it against.
        val previousVoice = store.state.voiceState.selectedVoice
        // Read before the reducer runs, to save the previous article progress before a seek in case the seek fails.
        val previousProgress = store.state.articleProgress

        next(action)

        when (action) {
            is ListenAction.Session.ListenRequested -> {
                store.endSession(releasePlayback = false)
                observePlayback(store)
                requestContent(store, action.tabId)
                trackTabClosure(store, action.tabId)
            }

            is ListenAction.Session.StopRequested -> store.endSession(releasePlayback = true)

            is ListenAction.Content.ContentReady -> store.loadVoicesAndPlay(action.languageTag)

            is ListenAction.Voices.VoiceSelected -> {
                // Saved even when the voice is the one already reading, because the voice a load picks for itself is
                // put in the store without being saved: the first tap on it is what makes the choice the user's own.
                store.state.languageTag?.let { action.voice.persistChoiceFor(it) }

                // Picking the voice already reading asks for nothing, so the audio is left alone. Making it again
                // would throw away what has been synthesized only to restart the article in the very same voice.
                if (action.voice != previousVoice) {
                    store.changeVoiceTo(action.voice)
                }
            }

            is ListenAction.Playback.SeekRequested -> {
                store.seekToArticlePosition(
                    positionMs = store.state.articleProgress.positionMs,
                    progressBeforeSeek = previousProgress,
                )
            }
            ListenAction.Controls.PlayPauseClicked -> store.togglePlayback()
            ListenAction.Controls.RewindClicked -> store.skipBy(-SEEK_BACK_INCREMENT_MS)
            ListenAction.Controls.ForwardClicked -> store.skipBy(SEEK_FORWARD_INCREMENT_MS)
            ListenAction.Controls.VoicesClicked -> store.refreshVoices()
            is ListenAction.Controls.PlaybackSpeedSelected ->
                scope.launch { playbackController.setSpeed(action.playbackSpeed) }
            is ListenAction.Playback,
            is ListenAction.Controls,
            ListenAction.Content.ContentUnavailable,
            is ListenAction.Voices.AvailableVoicesLoaded,
            ListenAction.Voices.NoOfflineVoicesAvailable,
            is ListenAction.Synthesis,
            is PlaylistAction,
            is ListenAction.ModeChanged,
            ListenAction.ErrorDismissed -> Unit
        }
    }

    /**
     * Reports what the player is doing into the store, for as long as the session lasts. Since playback can be
     * controlled through the notification (or outside audio sources) we cannot create this state based on our commands.
     */
    private fun observePlayback(store: ListenStore) {
        playbackStatusJob?.cancel()
        playbackStatusJob = scope.launch {
            launch {
                playbackController.seekRequests.collect { store.seekFromNotification(it) }
            }
            launch {
                playbackController.sessionEnded.first()
                store.dispatch(
                    ListenAction.Session.StopRequested(ListenAction.Session.StopSource.MediaSessionEndedOutsideApp)
                )
            }
            launch {
                settings.enabled.first { it == ListenEnabledState.Disabled }
                store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.FeatureDisabled))
            }
            playbackController.status.collect { store.reportPlayback(it) }
        }
    }

    /**
     * Reports to the Store what [report] represents in terms of the article. Will also manipulate the queue as needed,
     * since the player does not understand the context of the queue or full article.
     */
    private fun ListenStore.reportPlayback(report: PlaybackState) {
        // The reducer moves the article on from the report. A report left over from a seek or restart is not read at
        // all.
        val previousShownDurationMs = state.articleProgress.shownDurationMs()
        dispatch(PlayerReportReceived(report))
        val playback = playlist.lastReadReport ?: return

        // The notification draws its bar from the position and the length alone, so it is given the length that puts
        // its bar where the sheet's is.
        val shownDurationMs = state.articleProgress.shownDurationMs()
        if (shownDurationMs != previousShownDurationMs) {
            scope.launch { playbackController.setArticleDuration(shownDurationMs) }
        }

        when (playback.phase) {
            PlaybackPhase.Failed -> recoverOrReportFailure()
            PlaybackPhase.Ended -> refillOrEnd()

            // If the player reaches a chunk that is not playing yet, it indicates the chunk is still loading
            PlaybackPhase.Playing if playback.chunk.index != playlist.playingChunkIndex -> {
                dispatch(ListenAction.Playback.PlaybackStarted(playback.chunk, playback.positionMs))
                synthesizing(this::dispatch) {
                    synthesisQueue?.let { workAheadOf(it, playlist.playingChunkIndex) }
                }
            }

            else -> dispatch(ListenAction.Playback.StateChangeObserved(playback))
        }
    }

    private fun trackTabClosure(store: Store<ListenState, ListenAction>, tabId: String) {
        tabClosureJob?.cancel()

        val startingTab = browserStore.state.findTab(tabId)
        val startingTabUrl = startingTab?.content?.url
        val startingTabPageUrl = startingTab?.pageUrl
        val startedInReaderMode = startingTab?.readerState?.active == true

        tabClosureJob = scope.launch {
            val source =
                browserStore.stateFlow
                    .mapNotNull { state ->
                        val tab = state.findTab(tabId)
                        when {
                            tab == null -> ListenAction.Session.StopSource.TabClosed
                            // checks if the tab's url has been updated while listening to the tab
                            tab.content.url != startingTabUrl && tab.pageUrl != startingTabPageUrl ->
                                ListenAction.Session.StopSource.TabClosed
                            startedInReaderMode && !tab.readerState.active ->
                                ListenAction.Session.StopSource.ReaderModeClosed
                            else -> null
                        }
                    }
                    .first()
            store.dispatch(ListenAction.Session.StopRequested(source))
        }
    }

    private fun requestContent(store: Store<ListenState, ListenAction>, tabId: String) {
        contentJob?.cancel()
        contentJob = scope.launch {
            val content = contentProvider.getContent(tabId)

            // A session for another tab was started, or the session was stopped, while the article was extracted.
            if (store.state.tabId != tabId) {
                return@launch
            }

            content
                .onSuccess { store.readArticle(tabId, it) }
                .onFailure { store.dispatch(ListenAction.Content.ContentUnavailable) }
        }
    }

    /**
     * Takes [content] as the session's article, or reports that there is nothing to read out.
     *
     * The article is read in the language of [content].
     *
     * @param tabId The tab [content] was extracted from.
     */
    private fun ListenStore.readArticle(tabId: String, content: Content) {
        if (content.text.isBlank()) {
            dispatch(ListenAction.Content.ContentUnavailable)
            return
        }

        val language = content.languageTag
        val displayData = browserStore.state.findTab(tabId).describeArticle()
        article =
            Article(
                tabId = tabId,
                text = content.text,
                languageTag = language,
                displayData = displayData,
            )
        dispatch(
            ListenAction.Content.ContentReady(
                languageTag = language,
                languageSource = content.languageSource,
                title = displayData.title,
                site = displayData.site,
            )
        )
    }

    /**
     * Loads the voices of the article language, unless they are loaded already, and then reads the article out. A
     * session reads as soon as it starts, so the reader hears the article from a single tap.
     *
     * The load and the synthesis are one job rather than two because the engine reads with the voice it was last given:
     * starting the synthesis alongside the load reads the opening of the article in whichever voice the engine happens
     * to default to. A language with no offline voice is not read out at all.
     */
    private fun ListenStore.loadVoicesAndPlay(langTag: String) {
        // Shown as buffering from here until the opening plays
        dispatch(ListenAction.Playback.PlaybackWaiting)
        voicesJob?.cancel()
        voicesJob = scope.launch {
            if (state.voiceState.loadState != VoiceLoadState.Loaded && !loadVoices(langTag)) {
                return@launch
            }

            synthesizeAndPlay(state.tabId)
        }
    }

    /** Pauses or resumes the article, depending on what the player last reported. */
    private fun ListenStore.togglePlayback() {
        when (state.playbackState.phase) {
            PlaybackPhase.Playing,
            PlaybackPhase.Buffering -> scope.launch { playbackController.pause() }

            PlaybackPhase.Paused,
            PlaybackPhase.Failed -> scope.launch { playbackController.resume() }

            PlaybackPhase.Ended -> {
                dispatch(ListenAction.Playback.PlaybackWaiting)
                seekToArticlePosition(0)
                scope.launch { playbackController.resume() }
            }

            // The opening is still being synthesized, and plays on its own once it is ready.
            PlaybackPhase.Idle -> Unit
        }
    }

    /**
     * Asks the engine which offline voices it has for [langTag] and puts them in the store.
     *
     * @return Whether the article can be read out, which it cannot when the language has no offline voice.
     */
    private suspend fun ListenStore.loadVoices(langTag: String): Boolean =
        withContext(ioDispatcher) {
            val engine = synthesizer()
            settings.clearSavedVoicesOnEngineChange(engine.enginePackageName)
            val voices = engine.loadAvailableVoices(langTag)

            if (voices.isEmpty()) {
                dispatch(ListenAction.Voices.NoOfflineVoicesAvailable)
                false
            } else {
                val selected = voices.loadSavedVoiceFor(langTag)
                engine.setVoice(selected)
                dispatch(ListenAction.Voices.AvailableVoicesLoaded(voices, selected))
                true
            }
        }

    /**
     * Refreshes the voices that are loaded for the player. Will select a fallback voice if the selected voice is no
     * longer available, or the user has not made a selection. Will pause playback and show an error if no voices are
     * available.
     */
    private fun ListenStore.refreshVoices() {
        val languageTag = state.languageTag ?: return
        // abandon a refresh before the page has loaded the first time
        if (state.voiceState.loadState != VoiceLoadState.Loaded) return
        refreshVoicesJob?.cancel()
        refreshVoicesJob = scope.launch {
            val voices = withContext(ioDispatcher) { synthesizer().loadAvailableVoices(languageTag) }
            val selected = state.voiceState.selectedVoice

            when {
                voices.isEmpty() -> {
                    if (
                        state.playbackState.phase == PlaybackPhase.Playing ||
                            state.playbackState.phase == PlaybackPhase.Buffering
                    ) {
                        playbackController.pause()
                    }
                    dispatch(ListenAction.Voices.NoOfflineVoicesAvailable)
                }
                selected != null && selected in voices ->
                    dispatch(
                        ListenAction.Voices.AvailableVoicesLoaded(
                            voices = voices,
                            selectedVoice = selected,
                        )
                    )
                else -> {
                    val fallback = voices.loadSavedVoiceFor(languageTag)
                    dispatch(
                        ListenAction.Voices.AvailableVoicesLoaded(voices = voices, selectedVoice = selected ?: fallback)
                    )
                    dispatch(ListenAction.Voices.VoiceSelected(fallback, ListenAction.Voices.VoiceSource.Fallback))
                }
            }
        }
    }

    private suspend fun List<Voice>.loadSavedVoiceFor(langTag: String): Voice {
        val savedId = settings.getSelectedVoiceId(langTag.language)
        // The list is presumed sorted by a useful heuristic so `first()` is best
        return firstOrNull { it.id == savedId } ?: first()
    }

    private fun Voice.persistChoiceFor(langTag: String) {
        scope.launch(ioDispatcher) { settings.setSelectedVoiceId(langTag.language, id) }
    }

    /**
     * Reads the article out again in [voice], from where the playback of the one before it had got to.
     *
     * The audio already made is in the previous voice, so it is thrown away rather than played to its end. The new
     * voice reads at its own rate, so the same time is a different place in the text: playback carries on from the same
     * place in the text instead.
     *
     * It takes over [voicesJob] rather than running beside it, so that a load still in flight cannot finish afterwards
     * and put the engine back on the voice it had chosen for itself.
     */
    private fun ListenStore.changeVoiceTo(voice: Voice) {
        // A voice can be picked with no session running, where there is nothing to make again and binding an engine
        // to set it on would leave that engine bound with nothing to close it.
        if (article == null) {
            return
        }

        voicesJob?.cancel()
        voicesJob = scope.launch {
            val progress = state.articleProgress
            val textPositionAfterVoiceChanged =
                state.articleMeasurements
                    ?.getTextPositionAfterVoiceChanged(progress.positionMs, progress.chunkDurationsMs)
                    ?.takeIf { progress.durationMs > 0 }

            // The voice before stops at once, rather than reading on until the new one is ready.
            if (textPositionAfterVoiceChanged != null) {
                playbackController.stopAndBufferAt(progress.positionMs)
                dispatch(ListenAction.Playback.PlaybackWaiting)
            }

            // Only this article's audio goes: the playback and the engine are kept, because the new voice reads over
            // what is playing and reads with the engine already bound.
            val emptying = synthesisQueue
            synthesisQueue = null
            dispatch(MeasurementAction.Cleared)
            stopSynthesizing { emptying?.clear() }

            // Joined rather than only asked for, so that the request in flight cannot be handed the new voice and
            // write its audio in it after the audio of the old voice has been thrown away.
            teardownJob?.join()

            withContext(ioDispatcher) { synthesizer().setVoice(voice) }

            synthesizeAndPlay(state.tabId, updatedPosition = textPositionAfterVoiceChanged)
        }
    }

    /** Starts the playlist again where [VoiceChangeStarted] put the article, in audio of the voice just set. */
    private fun ListenStore.resumeInNewVoice(queue: SynthesisQueue) {
        val progress = state.articleProgress
        scope.launch { playbackController.setArticleDuration(progress.shownDurationMs()) }

        if (progress.durationMs > 0 && progress.positionMs >= progress.durationMs) {
            finishArticle()
            return
        }

        val targetPosition = progress.chunkDurationsMs.chunkPositionAt(progress.positionMs) ?: return
        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Seek) ?: return
        dispatch(PlaylistAction.SeekStarted(progress.articlePositionOf(targetPosition), targetPosition.chunkIndex))

        // Always made and restarted on, since the player still holds the chunks of the voice before.
        remakeAndRestartAt(targetPosition, lock, queue)
    }

    /**
     * Synthesizes the opening of the article, plays it, and works ahead on what follows it.
     *
     * @param tabId The tab the live session is reading. A session that has already ended may still have written its
     *   article to the field, so anything extracted for a different tab is ignored.
     * @param updatedPosition How far through the text a new voice carries on from, or `null` to play from the top.
     */
    private fun ListenStore.synthesizeAndPlay(tabId: String?, updatedPosition: Float? = null) {
        val article = article?.takeIf { it.tabId == tabId } ?: return
        val firstChunkIndex = 0

        dispatch(PlaylistAction.ArticleStarted)
        recoveringChunk = NO_CHUNK
        recoveryAttempts = 0

        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Restart) ?: return

        synthesizing(this::dispatch) {
            val queue =
                SynthesisQueue(
                    synthesizer = synthesizer(),
                    audio = ChunkAudio(audioCache),
                    chunker = chunker,
                    workDispatcher = ioDispatcher,
                    onMeasured = { index, duration -> dispatch(MeasurementAction.ChunkMeasured(index, duration)) },
                )
            synthesisQueue = queue
            dispatch(MeasurementAction.Started)

            try {
                val opening = queue.startReading(article.text, article.languageTag)
                dispatch(
                    MeasurementAction.ArticleSplit(
                        chunkCharacters = (0 until queue.chunkCount).map(queue::charactersIn),
                        speechRate = queue.speechRate,
                    )
                )
                if (updatedPosition != null) {
                    state.articleMeasurements?.chunkIndexAtTextPosition(updatedPosition)?.let { queue.audioFor(it) }
                    dispatch(VoiceChangeStarted(updatedPosition))
                } else {
                    playbackController.startArticle(
                        chunk = ChunkFile(firstChunkIndex, opening, queue.durationMs(firstChunkIndex)),
                        articleDisplayData = article.displayData,
                        tabId = article.tabId,
                    )
                    dispatch(PlaylistAction.ChunkQueued(firstChunkIndex))
                }
            } finally {
                lock.release()
            }

            if (updatedPosition != null) return@synthesizing resumeInNewVoice(queue)

            // Runs ahead of the opening while it plays, so the chunk after it is queued behind it rather than started
            // when the opening ends. A chunk that arrives after the opening has finished cannot be joined onto it.
            workAheadOf(queue, playingChunkIndex = firstChunkIndex)
        }
    }

    /**
     * The end of playback indicates either that queue synthesis is lagging or the article is complete. This determines
     * which case has been reached and reacts accordingly.
     */
    private fun ListenStore.refillOrEnd() {
        val queue = synthesisQueue ?: return
        val chunkCount = state.articleMeasurements?.chunkCount ?: return

        // Exit early if there are no additional chunks to synthesize
        val missing = playlist.nextToQueue
        if (missing >= chunkCount) {
            dispatch(ListenAction.Playback.PlaybackEnded)
            return
        }

        dispatch(ListenAction.Playback.PlaybackWaiting)

        synthesizing(this::dispatch) {
            // In case we receive a repeated report of playback ending we check to see if the next chunk has already
            // been queued
            if (playlist.lastQueuedChunkIndex >= missing) {
                return@synthesizing
            }

            val file = queue.audioFor(missing) ?: return@synthesizing
            // The player carries on with it by itself if it ran out of audio meanwhile.
            playbackController.enqueue(ChunkFile(missing, file, queue.durationMs(missing)))
            dispatch(PlaylistAction.ChunkQueued(missing))

            workAheadOf(queue, missing)
        }
    }

    /** Makes the chunk the player failed on again, or reports the failure if the chunk cannot be remade. */
    private fun ListenStore.recoverOrReportFailure() {
        val queue = synthesisQueue
        val progress = state.articleProgress
        val target = progress.chunkDurationsMs.chunkPositionAt(progress.positionMs)
        if (queue == null || target == null) {
            // No way to remake a chunk that we can't find
            dispatch(ListenAction.Playback.PlaybackFailed)
            return
        }

        if (target.chunkIndex != recoveringChunk) {
            recoveringChunk = target.chunkIndex
            recoveryAttempts = 0
        }

        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Recovery) ?: return

        if (recoveryAttempts >= MAX_RECOVERY_ATTEMPTS) {
            lock.release()
            dispatch(ListenAction.Playback.PlaybackFailed)
            return
        }
        recoveryAttempts += 1

        scope.launch {
            queue.discard(target.chunkIndex)
            remakeAndRestartAt(target, lock, queue)
        }
    }

    /** Seeks to [positionMs] into the article. A seek to its end finishes it, since there is nothing left to play. */
    private fun ListenStore.seekToArticlePosition(positionMs: Long, progressBeforeSeek: ArticleProgress? = null) {
        val articleMs = state.articleProgress.durationMs
        if (articleMs in 1..positionMs) {
            finishArticle()
        } else {
            moveToArticlePosition(positionMs, progressBeforeSeek)
        }
    }

    /**
     * Moves playback to [positionMs] into the article.
     *
     * The lengths come from the Store, rather than the queue. The Store should have data that matches the last lengths
     * reported to the player, so this keeps them in sync even if the queue has been actively synthesizing lengths for
     * other chunks.
     */
    private fun ListenStore.moveToArticlePosition(positionMs: Long, progressBeforeSeek: ArticleProgress?) {
        val revert: () -> Unit = {
            if (progressBeforeSeek != null) {
                revertArticleProgress(progressBeforeSeek)
            }
        }
        val queue = synthesisQueue ?: return revert()
        val targetPosition = state.articleProgress.chunkDurationsMs.chunkPositionAt(positionMs) ?: return revert()
        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Seek) ?: return revert()
        val newArticlePositionMs = state.articleProgress.articlePositionOf(targetPosition)

        // Set at once, since the player keeps reporting from where it was until the seek below reaches it.
        dispatch(PlaylistAction.SeekStarted(newArticlePositionMs, targetPosition.chunkIndex))

        scope.launch {
            if (!lock.isHeld) return@launch

            // Seeks within the playlist when the chunk's audio is on disk and the player says it holds the chunk.
            if (
                queue.fileFor(targetPosition.chunkIndex) != null &&
                    playbackController.seekTo(targetPosition.chunkIndex, newArticlePositionMs)
            ) {
                lock.release()
                return@launch
            }

            remakeAndRestartAt(targetPosition, lock, queue) {
                dispatch(PlaylistAction.SeekAbandoned)
                revert()
                // The session buffers at the target, and would go on reporting it over the reverted progress.
                progressBeforeSeek?.let { scope.launch { playbackController.stopAndBufferAt(it.positionMs) } }
            }
        }
    }

    /**
     * Finishes the article where a seek moved playback to its end: the audio stops, and nothing more is made for it.
     */
    private fun ListenStore.finishArticle() {
        // Claimed and let go of at once, so that a seek still waiting on its chunk does not restart playback after
        // this.
        synthesisJobPriorityLock.tryClaim(PlaybackIntent.Seek)?.release()
        val articleEndMs = state.articleProgress.durationMs
        dispatch(PlaylistAction.Finished)
        scope.launch { playbackController.finishAt(articleEndMs) }
        dispatch(ListenAction.Playback.PlaybackEnded)
    }

    private fun ListenStore.revertArticleProgress(progress: ArticleProgress) {
        dispatch(
            ListenAction.Playback.ArticleProgressReverted(
                positionMs = progress.positionMs,
                durationMs = progress.durationMs,
                chunkDurationsMs = progress.chunkDurationsMs,
                barPosition = progress.barPosition,
            )
        )
    }

    /**
     * Seeks to [positionMs] into the article, which the notification asked for. The notification was given the length
     * the bar is drawn against, so the share of it is where the reader let go on the notification's bar.
     */
    private fun ListenStore.seekFromNotification(positionMs: Long) {
        val shownDurationMs = state.articleProgress.shownDurationMs()
        if (shownDurationMs > 0) {
            dispatch(ListenAction.Playback.SeekRequested(positionMs.toFloat() / shownDurationMs))
        }
    }

    /** Drops the work in flight, makes the chunk that holds [targetPosition] and starts the playlist again there. */
    private fun ListenStore.remakeAndRestartAt(
        targetPosition: ChunkPosition,
        lock: SynthesisJobPriorityLock.Handle,
        queue: SynthesisQueue,
        onFailed: () -> Unit = {},
    ) {
        val progress = state.articleProgress

        // Kept from now, so that where the target was shown is where the playlist goes on to start.
        val chunkDurationsBeforeTargetChunk = progress.chunkDurationsMs.take(targetPosition.chunkIndex)

        // The audio stops and the session reports the target at once, rather than the audio playing on where it was
        // until the chunk has been made.
        val targetChunkStartMs = chunkDurationsBeforeTargetChunk.sum()
        scope.launch { playbackController.stopAndBufferAt(targetChunkStartMs + targetPosition.offsetMs) }

        dispatch(ListenAction.Playback.PlaybackWaiting)

        stopSynthesizing {}

        synthesizing(this::dispatch) {
            var restarted = false
            try {
                // Ensure another seek has not occurred
                if (!lock.isHeld) return@synthesizing

                val file = queue.audioFor(targetPosition.chunkIndex) ?: return@synthesizing

                // Check again in case of repeated seeks while waiting for the above synthesis
                if (!lock.isHeld) return@synthesizing

                val measuredChunkDurationMs = queue.durationMs(targetPosition.chunkIndex)
                val offsetMs =
                    startInMadeChunk(queue, targetPosition, progress.chunkDurationsMs[targetPosition.chunkIndex])
                if (!lock.isHeld) return@synthesizing

                val startMs = targetChunkStartMs + offsetMs
                playbackController.restartAt(
                    chunk = ChunkFile(targetPosition.chunkIndex, file, measuredChunkDurationMs),
                    chunkStartMs = targetChunkStartMs,
                    positionMs = startMs,
                )
                restarted = true
                dispatch(
                    PlaylistAction.Restarted(
                        chunkIndex = targetPosition.chunkIndex,
                        chunkDurationsBeforePlaylistMs = chunkDurationsBeforeTargetChunk,
                        startMs = startMs,
                    )
                )
            } finally {
                // Do not revert the progress if another seek has occurred or the coroutine is canceled - this is not
                // a failure, and that seek has written its own progress position
                if (!restarted && lock.isHeld && currentCoroutineContext().isActive) {
                    onFailed()
                }
                lock.release()
            }

            workAheadOf(queue, targetPosition.chunkIndex)
        }
    }

    /**
     * How far into [targetPosition]'s chunk, now made and measured, playback starts, given that it was estimated at
     * [estimatedMs]. With little of the chunk left to play from there, the next chunk is made first, so that the player
     * does not run out of audio before it is ready.
     */
    private suspend fun startInMadeChunk(
        queue: SynthesisQueue,
        targetPosition: ChunkPosition,
        estimatedMs: Long,
    ): Long {
        val measuredMs = queue.durationMs(targetPosition.chunkIndex)
        val offsetMs = offsetInMeasuredChunk(targetPosition.offsetMs, measuredMs, estimatedMs)
        if (measuredMs != null && measuredMs - offsetMs < MIN_AUDIO_AFTER_SEEK_MS) {
            makeChunkAheadOfSeek(queue, targetPosition.chunkIndex + 1)
        }

        return offsetMs
    }

    /**
     * Makes the chunk at [chunkIndex] ahead of a seek, if the article has one. A chunk that fails is not the seek's
     * failure: it is made again when playback reaches it.
     */
    private suspend fun makeChunkAheadOfSeek(queue: SynthesisQueue, chunkIndex: Int) {
        if (chunkIndex >= queue.chunkCount) return

        try {
            queue.audioFor(chunkIndex)
        } catch (e: CancellationException) {
            throw e
        } catch (@Suppress("TooGenericExceptionCaught") e: Exception) {
            logger.warn("Could not make the chunk after a seek", e)
        }
    }

    /**
     * Moves playback [offsetMs] through the article from where it is, kept inside the article. It goes through the
     * article rather than the player, whose own skip stops at the edges of the chunk it is playing.
     */
    private fun ListenStore.skipBy(offsetMs: Long) {
        val progress = state.articleProgress
        seekToArticlePosition((progress.positionMs + offsetMs).coerceIn(0, progress.durationMs))
    }

    /**
     * Fills the window around [playingChunkIndex] without reporting what it fails with, and queues what is made.
     *
     * The chunk a reader is waiting on is made by [SynthesisQueue.audioFor], which does report a failure. A chunk
     * further ahead than that is made again when playback reaches it, so failing to work ahead is not the session's
     * failure and must not put an error over a player that is still reading the article out.
     */
    private suspend fun ListenStore.workAheadOf(queue: SynthesisQueue, playingChunkIndex: Int) {
        // What is already made goes to the player first, and each chunk the window makes as soon as it is made, so that
        // the player does not run out while the rest of the window is made.
        queueWhatIsMade(queue)
        try {
            queue.moveWindowTo(
                playingChunkIndex,
                lastQueuedChunkIndex = playlist.lastQueuedChunkIndex,
                seekingToChunkIndex = playlist.seekingToChunkIndex,
                onChunkMade = { queueWhatIsMade(queue) },
            )
        } catch (e: CancellationException) {
            throw e
        } catch (@Suppress("TooGenericExceptionCaught") e: Exception) {
            logger.warn("Could not work ahead on the article", e)
        }
    }

    /** Hands the player every chunk after what it has, in order, up to the first that has not been made. */
    private suspend fun ListenStore.queueWhatIsMade(queue: SynthesisQueue) {
        while (playlist.nextToQueue < queue.chunkCount) {
            val next = playlist.nextToQueue
            val file = queue.fileFor(next) ?: return

            playbackController.enqueue(ChunkFile(next, file, queue.durationMs(next)))
            dispatch(PlaylistAction.ChunkQueued(next))
        }
    }

    /** Runs [work] after whatever synthesis is already in flight, and reports whatever it fails with. */
    private fun synthesizing(dispatch: (ListenAction) -> Unit, work: suspend () -> Unit) {
        sessionScope().launch {
            try {
                teardownJob?.join()

                requestTurn.withLock { work() }
            } catch (e: CancellationException) {
                throw e
            } catch (_: NothingToReadException) {
                dispatch(ListenAction.Content.ContentUnavailable)
            } catch (_: NoOfflineVoiceAvailableException) {
                // The engine only reaches for the network when it has no offline voice for the language, so a network
                // failure during synthesis means the same thing to the user as an empty voice list.
                dispatch(ListenAction.Voices.NoOfflineVoicesAvailable)
            } catch (@Suppress("TooGenericExceptionCaught") e: Exception) {
                logger.error("Could not read the article out loud", e)
                dispatch(ListenAction.Synthesis.SynthesisFailed)
            }
        }
    }

    /** The scope this session's synthesis runs in, made on first use and cancelled when the session ends. */
    private fun sessionScope(): CoroutineScope =
        sessionScope
            ?: CoroutineScope(scope.coroutineContext + SupervisorJob(scope.coroutineContext.job)).also {
                sessionScope = it
            }

    /** The engine for this session, bound on first use because binding it is IPC. */
    private suspend fun synthesizer(): SpeechSynthesizer = synthesizerLock.withLock {
        synthesizer
            ?: withContext(ioDispatcher) { synthesizerProvider() }
                .also { synthesizer = it }
                .also { emitEngineBoundFact(it.enginePackageName) }
    }

    /**
     * Stops everything the session has in flight and throws away the audio it made.
     *
     * @param releasePlayback Whether to give up the playback and close the engine, which takes the notification away
     *   with the service. A session starting in place of this one keeps both: it replaces what is playing, and it reads
     *   with the same engine rather than paying to bind another.
     */
    private fun ListenStore.endSession(releasePlayback: Boolean) {
        contentJob?.cancel()
        voicesJob?.cancel()
        refreshVoicesJob?.cancel()
        tabClosureJob?.cancel()
        article = null

        val emptying = synthesisQueue
        synthesisQueue = null
        dispatch(MeasurementAction.Cleared)

        val closing = synthesizer.takeIf { releasePlayback }
        if (releasePlayback) {
            playbackStatusJob?.cancel()
            synthesizer = null
        }

        stopSynthesizing {
            if (releasePlayback) {
                playbackController.release()

                // The engine is an IPC binding into the text to speech app, which stays alive for the rest of this
                // app's life unless it is shut down. Closing is terminal, which is why the next session builds its own.
                withContext(ioDispatcher) { closing?.close() }

                // Nothing else deletes the audio, so without this a session leaves its files behind for as long as the
                // system keeps the cache directory. Only safe once the playback above has given up the files.
                audioCache.clear()
            } else {
                // The playback carries on into the session replacing this one, so only this article's own audio goes.
                emptying?.clear()
            }
        }
    }

    /**
     * Cancels every piece of synthesis in flight and then runs [andThen], leaving [teardownJob] to be waited on.
     *
     * @param andThen What to do once the synthesis has stopped. Our cancellation asks the engine to stop but cannot
     *   un-write a file it has already produced, so throwing audio away belongs here: by the time this runs, the
     *   request that was in flight has finished with its file.
     */
    private fun ListenStore.stopSynthesizing(andThen: suspend () -> Unit) {
        dispatch(PlaylistAction.Cleared)

        val ending = sessionScope
        sessionScope = null

        // Chained rather than replaced, so that waiting for the teardown waits for every teardown still to finish.
        val earlier = teardownJob
        teardownJob = scope.launch {
            earlier?.join()
            ending?.coroutineContext?.job?.cancelAndJoin()

            andThen()
        }
    }
}

internal const val MAX_RECOVERY_ATTEMPTS = 3

/** The least audio a seek can leave in its chunk without the next one made first. */
private const val MIN_AUDIO_AFTER_SEEK_MS = 2_000L

/**
 * The page this tab shows: in reader mode the article the reader view renders, rather than the reader view's own URL.
 */
private val TabSessionState.pageUrl: String
    get() = readerState.activeUrl ?: content.url

/** The article of one listening session, the tab it was extracted from, and the language it is being read as. */
private class Article(
    val tabId: String,
    val text: String,
    val languageTag: String,
    val displayData: ArticleDisplayData,
)

/** What the playback notification and the lock screen say about the article in this tab. */
private fun TabSessionState?.describeArticle(): ArticleDisplayData =
    ArticleDisplayData(
        title = this?.content?.title?.takeIf { it.isNotBlank() },
        site = this?.pageUrl?.toUri()?.host?.takeIf { it.isNotBlank() }?.stripCommonSubdomains(),
        isPrivate = this?.content?.private == true,
    )

/**
 * The language subtag of a BCP 47 tag, or the tag itself when it is too malformed to parse.
 *
 * The voice list ignores the region of the article language, so the saved choice has to ignore it too: a voice picked
 * on an "en-US" article is the one offered again on an "en-GB" one, rather than being forgotten between them.
 */
private val String.language: String
    get() = Locale.forLanguageTag(this).language.ifEmpty { this }
