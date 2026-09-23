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
import kotlinx.coroutines.flow.first
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
import mozilla.components.feature.listentopage.playback.ArticleDisplayData
import mozilla.components.feature.listentopage.playback.AudioFileCache
import mozilla.components.feature.listentopage.playback.ChunkAudio
import mozilla.components.feature.listentopage.playback.ChunkPosition
import mozilla.components.feature.listentopage.playback.PlaybackController
import mozilla.components.feature.listentopage.playback.PlaybackIntent
import mozilla.components.feature.listentopage.playback.SEEK_BACK_INCREMENT_MS
import mozilla.components.feature.listentopage.playback.SEEK_FORWARD_INCREMENT_MS
import mozilla.components.feature.listentopage.playback.SynthesisJobPriorityLock
import mozilla.components.feature.listentopage.playback.chunkPositionAt
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
 * @property settings The module's own preferences, holding the voice the user picked for each article language.
 * @property scope The [CoroutineScope] the extraction, the synthesis and the playback commands run in. It has to
 *   dispatch on one thread: the session fields below are read from `invoke`, which the store runs on whichever thread
 *   dispatched, and written from this scope.
 * @property ioDispatcher The dispatcher for the work that must not run on the thread the store dispatched on.
 * @property chunker Splits article text into the chunks.
 */
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

    // Whether the user has asked this session to start reading. The article and its voices are made ready as soon as
    // the session starts, but nothing is synthesized or played until then.
    private var playRequested = false

    // Which chunk of the article the player last reported reading out.
    private var playingChunk = NO_CHUNK

    // The last chunk that was enqueued to the player.
    private var appendedThrough = NO_CHUNK

    // The chunk that started the most chunk most recently seeked in the player.
    private var playlistStartChunk = 0

    // Lock with associated priorities to manage which playback operations override each other
    private val synthesisJobPriorityLock = SynthesisJobPriorityLock()

    // The chunk the session is making again after a playback failure, and how many times it has tried.
    private var recoveringChunk = NO_CHUNK
    private var recoveryAttempts = 0

    // Whether the player was just moved by a seek.
    private var seeked = false

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

        next(action)

        when (action) {
            is ListenAction.Session.ListenRequested -> {
                endSession(releasePlayback = false)
                observePlayback(store)
                requestContent(store, action.tabId)
                trackTabClosure(store, action.tabId)
            }

            ListenAction.Session.StopRequested -> endSession(releasePlayback = true)

            is ListenAction.Content.ContentReady -> store.loadVoicesAndPlayIfRequested(action.languageTag)

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

            is ListenAction.Playback.SeekRequested -> store.seekToArticlePosition(action.positionMs)

            ListenAction.Controls.PlayPauseClicked -> store.togglePlayback()
            ListenAction.Controls.RewindClicked -> store.skipBy(-SEEK_BACK_INCREMENT_MS)
            ListenAction.Controls.ForwardClicked -> store.skipBy(SEEK_FORWARD_INCREMENT_MS)

            is ListenAction.Controls -> Unit
            is ListenAction.Playback,
            ListenAction.Content.ContentUnavailable,
            is ListenAction.Voices.AvailableVoicesLoaded,
            ListenAction.Voices.NoOfflineVoicesAvailable,
            ListenAction.Synthesis.SynthesisFailed,
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
            playbackController.status.collect { store.reportPlayback(it) }
        }
    }

    /**
     * Reports to the Store what [report] represents in terms of the article. Will also manipulate the queue as needed,
     * since the player does not understand the context of the queue or full article.
     */
    private fun ListenStore.reportPlayback(report: PlaybackState) {
        // The player counts the items of its playlist, which resets after a seek since the playlist is forced to
        // restart at the new chunk. The rest of the playback operations work off of that new chunk, so we update it in
        // State here.
        val playback = report.copy(chunk = report.chunk.copy(index = playlistStartChunk + report.chunk.index))

        // Every report moves the article on.
        reportArticleProgress(playback)

        when (playback.phase) {
            PlaybackPhase.Failed -> recoverOrReportFailure()
            PlaybackPhase.Ended -> refillOrEnd(this::dispatch)

            // If the player reaches a chunk that is not playing yet, it indicates the chunk is still loading
            PlaybackPhase.Playing if playback.chunk.index != playingChunk -> {
                playingChunk = playback.chunk.index
                dispatch(ListenAction.Playback.PlaybackStarted(playback.chunk, playback.positionMs))
                synthesizing(this::dispatch) {
                    synthesisQueue?.let {
                        it.workAheadOf(playingChunk)
                    }
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

        tabClosureJob = scope.launch {
            browserStore.stateFlow
                .first { state ->
                    val tab = state.findTab(tabId) ?: return@first true
                    // checks if the tab's url has been updated while listening to the tab
                    tab.content.url != startingTabUrl && tab.pageUrl != startingTabPageUrl
                }
                .let {
                    store.dispatch(ListenAction.Session.StopRequested)
                }
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
     * The article is read in the language the page declares. Where it declares none this can use, it cannot be read at
     * all: the chunker needs a language to find the sentence boundaries in, and the voice lookup needs one to match a
     * voice against.
     *
     * @param tabId The tab [content] was extracted from.
     */
    private fun ListenStore.readArticle(tabId: String, content: Content) {
        if (content.text.isBlank()) {
            dispatch(ListenAction.Content.ContentUnavailable)
            return
        }

        val language = content.languageTag.asLanguageTagOrNull()
        if (language == null) {
            logger.debug("The page declares no usable language (\"${content.languageTag}\")")
            dispatch(ListenAction.Content.ContentUnavailable)
            return
        }

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
                title = displayData.title,
                site = displayData.site,
            )
        )
    }

    /**
     * Loads the voices of the article language, unless they are loaded already, and then reads the article out if the
     * user has already asked for it.
     *
     * The load and the synthesis are one job rather than two because the engine reads with the voice it was last given:
     * starting the synthesis alongside the load reads the opening of the article in whichever voice the engine happens
     * to default to. A language with no offline voice is not read out at all.
     */
    private fun ListenStore.loadVoicesAndPlayIfRequested(langTag: String) {
        voicesJob?.cancel()
        voicesJob = scope.launch {
            if (state.voiceState.loadState != VoiceLoadState.Loaded && !loadVoices(langTag)) {
                return@launch
            }

            if (playRequested) {
                synthesizeAndPlay(state.tabId, this@loadVoicesAndPlayIfRequested::dispatch)
            }
        }
    }

    /**
     * Starts reading the article on the first press, and afterwards pauses or resumes it depending on what the player
     * last reported.
     *
     * A press before the article is ready is remembered, and the article is read out once its voices are loaded.
     */
    private fun ListenStore.togglePlayback() {
        if (!playRequested) {
            playRequested = true

            // Without an article the voice load has not started yet, and while it runs it reads the article out once
            // it is done.
            if (article != null && voicesJob?.isActive != true) {
                voicesJob = scope.launch { synthesizeAndPlay(state.tabId, this@togglePlayback::dispatch) }
            }
            return
        }

        when (state.playbackState.phase) {
            PlaybackPhase.Playing,
            PlaybackPhase.Buffering -> scope.launch { playbackController.pause() }

            PlaybackPhase.Paused,
            PlaybackPhase.Failed -> scope.launch { playbackController.resume() }

            PlaybackPhase.Ended -> {
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
     * The audio already made is in the previous voice, so it is thrown away rather than played to its end. "Where it
     * had got to" is a position in the chunk that was playing, so it only resumes the reader where they were while the
     * article is one chunk: the rest of it is re-chunked at the new voice's rate, which leaves no index the position of
     * the old chunk can be carried over to.
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
            val resumeAtMs = playbackController.currentPositionMs()

            // Only this article's audio goes: the playback and the engine are kept, because the new voice reads over
            // what is playing and reads with the engine already bound.
            val emptying = synthesisQueue
            synthesisQueue = null
            stopSynthesizing { emptying?.clear() }

            // Joined rather than only asked for, so that the request in flight cannot be handed the new voice and
            // write its audio in it after the audio of the old voice has been thrown away.
            teardownJob?.join()

            withContext(ioDispatcher) { synthesizer().setVoice(voice) }

            if (playRequested) {
                synthesizeAndPlay(state.tabId, this@changeVoiceTo::dispatch, resumeAtMs)
            }
        }
    }

    /**
     * Synthesizes the opening of the article, plays it, and works ahead on what follows it.
     *
     * @param tabId The tab the live session is reading. A session that has already ended may still have written its
     *   article to the field, so anything extracted for a different tab is ignored.
     * @param dispatch Dispatch an action to the store.
     * @param resumeAtMs Where to move the new audio to once it plays, or `0` to play it from the start.
     */
    private fun synthesizeAndPlay(tabId: String?, dispatch: (ListenAction) -> Unit, resumeAtMs: Long = 0L) {
        val article = article?.takeIf { it.tabId == tabId } ?: return
        val firstChunkIndex = 0

        playingChunk = NO_CHUNK
        appendedThrough = NO_CHUNK
        playlistStartChunk = firstChunkIndex
        recoveringChunk = NO_CHUNK
        recoveryAttempts = 0

        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Restart) ?: return

        synthesizing(dispatch) {
            val queue = SynthesisQueue(synthesizer(), ChunkAudio(audioCache), chunker, ioDispatcher)
            synthesisQueue = queue

            try {
                val opening = queue.startReading(article.text, article.languageTag)
                playbackController.play(file = opening, articleDisplayData = article.displayData)
                appendedThrough = firstChunkIndex
                if (resumeAtMs > 0) {
                    playbackController.seekTo(resumeAtMs)
                }
            } finally {
                lock.release()
            }

            // Runs ahead of the opening while it plays, so the chunk after it is queued behind it rather than started
            // when the opening ends. A chunk that arrives after the opening has finished cannot be joined onto it.
            queue.workAheadOf(playingChunk = firstChunkIndex)
        }
    }

    /**
     * The end of playback indicates either that queue synthesis is lagging or the article is complete. This determines
     * which case has been reached and reacts accordingly.
     *
     * @param dispatch Dispatch an action to the store.
     */
    private fun refillOrEnd(dispatch: (ListenAction) -> Unit) {
        val queue = synthesisQueue ?: return

        // Exit early if there are not additional chunks to synthesize
        val missing = appendedThrough + 1
        if (missing >= queue.chunkCount) {
            dispatch(ListenAction.Playback.PlaybackEnded)
            return
        }

        dispatch(ListenAction.Playback.PlaybackWaiting)

        synthesizing(dispatch) {
            // In case we receive a repeated report of playback ending we check to see if the next chunk has already
            // been queued
            if (appendedThrough >= missing) {
                return@synthesizing
            }

            val file = queue.audioFor(missing) ?: return@synthesizing
            playbackController.enqueue(file)
            appendedThrough = missing

            // Restart the player once there is synthesized content
            playbackController.resume()

            queue.workAheadOf(missing)
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

    /**
     * Moves playback to [positionMs] into the article.
     *
     * The lengths come from the Store, rather than the queue. The Store should have data that matches the last lengths
     * reported to the player, so this keeps them in sync even if the queue has been actively synthesizing lengths for
     * other chunks.
     */
    private fun ListenStore.seekToArticlePosition(positionMs: Long) {
        val queue = synthesisQueue ?: return
        val target = state.articleProgress.chunkDurationsMs.chunkPositionAt(positionMs) ?: return
        val lock = synthesisJobPriorityLock.tryClaim(PlaybackIntent.Seek) ?: return

        scope.launch {
            if (!lock.isHeld) return@launch

            // Check if seek is within already synthesized chunks
            if (target.chunkIndex in playlistStartChunk..appendedThrough && queue.fileFor(target.chunkIndex) != null) {
                playbackController.seekTo(target.chunkIndex - playlistStartChunk, target.positionMs)
                seeked = true
                lock.release()
                return@launch
            }

            remakeAndRestartAt(target, lock, queue)
        }
    }

    /** Drops the work in flight, makes the audio of [target] and starts the playlist again there. */
    private fun ListenStore.remakeAndRestartAt(
        target: ChunkPosition,
        lock: SynthesisJobPriorityLock.Handle,
        queue: SynthesisQueue,
    ) {
        dispatch(ListenAction.Playback.PlaybackWaiting)

        stopSynthesizing {}

        synthesizing(this::dispatch) {
            try {
                // Ensure another seek has not occurred
                if (!lock.isHeld) return@synthesizing

                val file = queue.audioFor(target.chunkIndex) ?: return@synthesizing

                // Check again in case of repeated seeks while waiting for the above synthesis
                if (!lock.isHeld) return@synthesizing

                playbackController.restartAt(file, target.positionMs)
                seeked = true
                playlistStartChunk = target.chunkIndex
                playingChunk = target.chunkIndex
                appendedThrough = target.chunkIndex
            } finally {
                lock.release()
            }

            queue.workAheadOf(target.chunkIndex)
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
     * Fills the window around [playingChunk] without reporting what it fails with.
     *
     * The chunk a reader is waiting on is made by [SynthesisQueue.audioFor], which does report a failure. A chunk
     * further ahead than that is made again when playback reaches it, so failing to work ahead is not the session's
     * failure and must not put an error over a player that is still reading the article out.
     */
    private suspend fun SynthesisQueue.workAheadOf(playingChunk: Int) {
        try {
            moveWindowTo(playingChunk, queuedThrough = appendedThrough)
        } catch (e: CancellationException) {
            throw e
        } catch (@Suppress("TooGenericExceptionCaught") e: Exception) {
            logger.warn("Could not work ahead on the article", e)
        }

        // now that the window has been synthesized, enqueue what was successfully synthesized with the player
        while (appendedThrough + 1 < chunkCount) {
            val next = appendedThrough + 1
            val file = fileFor(next) ?: return

            playbackController.enqueue(file)
            appendedThrough = next
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
        synthesizer ?: withContext(ioDispatcher) { synthesizerProvider() }.also { synthesizer = it }
    }

    /**
     * Stops everything the session has in flight and throws away the audio it made.
     *
     * @param releasePlayback Whether to give up the playback and close the engine, which takes the notification away
     *   with the service. A session starting in place of this one keeps both: it replaces what is playing, and it reads
     *   with the same engine rather than paying to bind another.
     */
    private fun endSession(releasePlayback: Boolean) {
        contentJob?.cancel()
        voicesJob?.cancel()
        tabClosureJob?.cancel()
        article = null
        playRequested = false

        val emptying = synthesisQueue
        synthesisQueue = null

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
    private fun stopSynthesizing(andThen: suspend () -> Unit) {
        playingChunk = NO_CHUNK
        appendedThrough = NO_CHUNK

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

    /**
     * Works out how far through the whole article playback has got, and reports it when it has moved.
     *
     * The chunk is the one [playback] names rather than [playingChunk], so that the position and the chunk it is a
     * position in always come from the same report.
     */
    private fun ListenStore.reportArticleProgress(playback: PlaybackState) {
        val queue = synthesisQueue ?: return
        val mapper = queue.mapper()
        val previous = if (seeked) state.articleProgress.copy(positionMs = 0) else state.articleProgress
        seeked = false
        val progress =
            mapper
                .progress(
                    previous = previous,
                    playingChunk = playback.chunk.index,
                    chunkPositionMs = playback.positionMs,
                    chunkEnded = playback.phase == PlaybackPhase.Ended,
                )
                .copy(chunkDurationsMs = mapper.chunkLengthsMs())

        if (progress != state.articleProgress) {
            dispatch(
                ListenAction.Playback.ArticleProgressChanged(
                    positionMs = progress.positionMs,
                    durationMs = progress.durationMs,
                    chunkDurationsMs = progress.chunkDurationsMs,
                )
            )
        }
    }
}

private const val NO_CHUNK = -1

internal const val MAX_RECOVERY_ATTEMPTS = 3

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
    )

/** This language tag in the form everything downstream works from, or `null` when it names no language. */
private fun String.asLanguageTagOrNull(): String? =
    trim().replace('_', '-').takeIf { Locale.forLanguageTag(it).language.isNotEmpty() }

/**
 * The language subtag of a BCP 47 tag, or the tag itself when it is too malformed to parse.
 *
 * The voice list ignores the region of the article language, so the saved choice has to ignore it too: a voice picked
 * on an "en-US" article is the one offered again on an "en-GB" one, rather than being forgotten between them.
 */
private val String.language: String
    get() = Locale.forLanguageTag(this).language.ifEmpty { this }
