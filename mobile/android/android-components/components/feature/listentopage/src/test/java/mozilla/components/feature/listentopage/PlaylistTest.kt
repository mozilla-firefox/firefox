/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import mozilla.components.feature.listentopage.playback.PlaybackPhase
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PlaylistTest {

    private var playlist = Playlist()

    @Test
    fun `test that a new article starts with nothing queued at its top`() {
        playlist =
            playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = List(6) { 10_000L }, startMs = 61_000)

        playlist = startArticle(playlist)

        assertEquals(NO_CHUNK, playlist.lastQueuedChunkIndex)
        assertEquals(0, playlist.nextToQueue)
        assertTrue(playlist.chunkDurationsBeforePlaylistMs.isEmpty())
    }

    @Test
    fun `test that the next chunk to hand to the player follows the last one queued`() {
        playlist = playlist.queued(0)
        playlist = playlist.queued(1)

        assertEquals(1, playlist.lastQueuedChunkIndex)
        assertEquals(2, playlist.nextToQueue)
    }

    @Test
    fun `test that a report after a restart is read as the chunk it names`() {
        playlist =
            playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = List(6) { 10_000L }, startMs = 61_000)
        send(playing(chunk = 6, positionMs = 61_000))

        assertEquals(7, send(playing(chunk = 7, positionMs = 72_000))?.chunk?.index)
        assertEquals(6, playlist.lastQueuedChunkIndex)
    }

    @Test
    fun `test that what the player reports before it plays from a restart is not read`() {
        playlist = playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000)

        assertNull("the playlist it replaced, ended", send(PlaybackState(phase = PlaybackPhase.Ended)))
        assertNull(
            "the position buffered at, still loading",
            send(PlaybackState(phase = PlaybackPhase.Buffering, positionMs = 61_000)),
        )
        assertNull("from before the restart", send(playing(chunk = 6, positionMs = 30_000)))

        assertEquals(6, send(playing(chunk = 6, positionMs = 61_000))?.chunk?.index)
        assertEquals(
            "what comes after the restart was not read",
            PlaybackPhase.Ended,
            send(PlaybackState(phase = PlaybackPhase.Ended))?.phase,
        )
    }

    @Test
    fun `test that an end of the chunk a playlist restarted on is read, when it is the first report from it`() {
        playlist = playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000)

        val ended = PlaybackState(phase = PlaybackPhase.Ended, chunk = ChunkState(index = 6), positionMs = 61_500)

        assertEquals(PlaybackPhase.Ended, send(ended)?.phase)
    }

    @Test
    fun `test that an end of another chunk after a restart is still left over from the playlist it replaced`() {
        playlist = playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000)

        assertNull(send(PlaybackState(phase = PlaybackPhase.Ended, chunk = ChunkState(index = 2), positionMs = 70_000)))
    }

    @Test
    fun `test that nothing the player reports is read once a seek finished the article, until playback moves again`() {
        playlist = playlist.finished()

        assertNull(send(playing(chunk = 2, positionMs = 12_000)))
        assertNull(send(PlaybackState(phase = PlaybackPhase.Paused, positionMs = 12_000)))

        playlist = playlist.seeking(0, chunkIndex = 0)

        assertEquals(0L, send(playing(chunk = 0, positionMs = 0))?.positionMs)
    }

    @Test
    fun `test that what the player reports from before a seek reaches it is not read`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        assertNull("sampled before the seek", send(playing(positionMs = 120_000)))
        assertEquals(
            150_000L,
            send(PlaybackState(phase = PlaybackPhase.Buffering, positionMs = 150_000))?.positionMs,
        )
        assertEquals(
            "the seek was waited on after it arrived",
            121_000L,
            send(playing(positionMs = 121_000))?.positionMs,
        )
    }

    @Test
    fun `test that the chunk a seek is moving into is known until the player reports from there`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)
        assertEquals(12, playlist.seekingToChunkIndex)

        send(PlaybackState(phase = PlaybackPhase.Buffering, positionMs = 150_000))

        assertEquals(NO_CHUNK, playlist.seekingToChunkIndex)
    }

    @Test
    fun `test that a seek the player never reports from does not stop the article for long`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        assertNull(send(playing(positionMs = 120_000)))
        assertNull(send(playing(positionMs = 121_000)))
        assertEquals(122_000L, send(playing(positionMs = 122_000))?.positionMs)
    }

    @Test
    fun `test that a seek restarted earlier in its chunk than it asked for is read from the restart`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        playlist = playlist.restarted(chunkIndex = 12, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 120_000)

        assertEquals(120_000L, send(playing(chunk = 12, positionMs = 120_000))?.positionMs)
    }

    @Test
    fun `test that a report far from where a seek restarted is still not read`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        playlist = playlist.restarted(chunkIndex = 12, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 120_000)

        assertNull("from the playlist before a seek back", send(playing(chunk = 30, positionMs = 400_000)))
    }

    @Test
    fun `test that a seek is still waited on once the playlist is cleared to make its chunk`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        playlist = playlist.cleared()

        assertNull(send(playing(positionMs = 1_500)))
    }

    @Test
    fun `test that a new article does not wait on a seek in the one before it`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        playlist = startArticle(playlist)

        assertEquals(PlaybackPhase.Failed, send(PlaybackState(phase = PlaybackPhase.Failed))?.phase)
        assertEquals(0L, send(PlaybackState(phase = PlaybackPhase.Buffering))?.positionMs)
    }

    @Test
    fun `test that what the player reports after a seek is given up is read`() {
        playlist = playlist.seeking(150_000, chunkIndex = 12)

        playlist = playlist.seekAbandoned()

        assertEquals(120_000L, send(playing(positionMs = 120_000))?.positionMs)
    }

    @Test
    fun `test that a failure after a restart is read at once`() {
        playlist = playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000)

        assertEquals(PlaybackPhase.Failed, send(PlaybackState(phase = PlaybackPhase.Failed))?.phase)
    }

    @Test
    fun `test that clearing the playlist forgets what was queued and the restart waited on`() {
        playlist = playlist.restarted(chunkIndex = 6, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000)

        playlist = playlist.cleared()

        assertEquals(NO_CHUNK, playlist.lastQueuedChunkIndex)
        assertEquals(NO_CHUNK, playlist.playingChunkIndex)
        assertEquals(PlaybackPhase.Buffering, send(PlaybackState(phase = PlaybackPhase.Buffering))?.phase)
    }

    /** Sends [report] as the player does, and returns it when it counts, or `null` when it was left over. */
    private fun send(report: PlaybackState): PlaybackState? {
        playlist = playlist.received(report)
        return playlist.lastReadReport
    }

    private fun startArticle(playlist: Playlist): Playlist =
        listenReducer(ListenState(playlist = playlist), PlaylistAction.ArticleStarted).playlist

    private fun playing(chunk: Int = 0, positionMs: Long) =
        PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = chunk), positionMs = positionMs)
}
