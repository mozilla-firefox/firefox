/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import androidx.media3.common.Player
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import mozilla.components.feature.listentopage.playback.ListenSessionHarness.Companion.chunk
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The controller, the session and [SessionPlayer] together, with only the ExoPlayer faked: what reaches the player is
 * what the session handed it, which is not always what the controller asked for.
 */
@RunWith(AndroidJUnit4::class)
class ListenSessionTest {

    private val listen = ListenSessionHarness()

    @After fun tearDown() = listen.close()

    @Test
    fun `test that the status reports where in the article the player is, across its items`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { enqueue(chunk(1, 20_000)) }

        listen.player.play(15_000)
        listen.idle()

        val status = listen.controller.status.value
        assertEquals(PlaybackPhase.Playing, status.phase)
        assertEquals(1, status.chunk.index)
        assertEquals(15_000L, status.positionMs)
    }

    @Test
    fun `test that a restart starts the player at the position into its chunk`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }

        listen.run { restartAt(chunk(5, 20_000), chunkStartMs = 75_000, positionMs = 80_000) }

        assertEquals(5_000L, listen.player.startPositions.last())
        assertEquals(80_000L, listen.controller.status.value.positionMs)
    }

    @Test
    fun `test that a released article is not shown at the place of its playlist to the next one`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { restartAt(chunk(5, 20_000), chunkStartMs = 75_000, positionMs = 80_000) }

        listen.run { release() }
        listen.idle()

        // Connected only now, as the controller of the next article connects.
        assertEquals(0L, listen.notification.currentPosition)
    }

    @Test
    fun `test that a seek into a queued item moves the player to the position into that item`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { enqueue(chunk(1, 20_000)) }

        listen.run { seekTo(chunkIndex = 1, positionMs = 13_000) }

        assertEquals(1 to 3_000L, listen.player.seeks.last())
        assertEquals(13_000L, listen.controller.status.value.positionMs)
    }

    @Test
    fun `test that a seek after a restart moves the player to the item holding the chunk asked for`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { restartAt(chunk(5, 20_000), chunkStartMs = 75_000, positionMs = 80_000) }
        listen.run { enqueue(chunk(6, 20_000)) }

        listen.run { seekTo(chunkIndex = 6, positionMs = 100_000) }

        assertEquals(1 to 5_000L, listen.player.seeks.last())
    }

    @Test
    fun `test that a seek into a chunk the player does not hold says so and moves nothing`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        val seeksBefore = listen.player.seeks.size

        val hasChunk = listen.run { seekTo(chunkIndex = 3, positionMs = 40_000) }

        assertFalse(hasChunk)
        assertEquals(seeksBefore, listen.player.seeks.size)
    }

    // The controller's items each last as long as the whole article, so only a measured length is the chunk's own.
    @Test
    fun `test that a chunk is reported with the length it was measured at, or none, never the article's`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { setArticleDuration(300_000) }
        assertEquals(10_000L, listen.controller.status.value.chunk.durationMs)

        listen.run {
            startArticle(ChunkFile(0, File("/chunks/unmeasured.wav"), durationMs = null), ArticleDisplayData(), TAB_ID)
        }
        listen.run { setArticleDuration(300_000) }

        assertNull(listen.controller.status.value.chunk.durationMs)
    }

    @Test
    fun `test that what the player reports names the chunk of the article, not the item of its playlist`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { restartAt(chunk(5, 20_000), chunkStartMs = 75_000, positionMs = 80_000) }

        assertEquals(5, listen.controller.status.value.chunk.index)
    }

    @Test
    fun `test that audio queued after the playback ran out is played without being asked`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.player.play(10_000)
        listen.idle()
        assertEquals(PlaybackPhase.Ended, listen.controller.status.value.phase)

        listen.run { enqueue(chunk(1, 20_000)) }

        assertEquals(1 to 0L, listen.player.seeks.last())
        assertEquals(PlaybackPhase.Playing, listen.controller.status.value.phase)
        assertEquals(10_000L, listen.controller.status.value.positionMs)
    }

    @Test
    fun `test that audio queued while the playback goes on leaves the player where it is`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }

        listen.run { enqueue(chunk(1, 20_000)) }

        assertTrue("the player was moved", listen.player.seeks.isEmpty())
    }

    @Test
    fun `test that stopping and buffering for a seek shows its target as loading until the playlist restarts there`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.notification

        listen.run { stopAndBufferAt(200_000) }

        assertEquals(1, listen.player.stops)
        assertEquals(PlaybackPhase.Buffering, listen.controller.status.value.phase)
        assertEquals(200_000L, listen.controller.status.value.positionMs)
        assertEquals(200_000L, listen.notification.currentPosition)

        listen.run { restartAt(chunk(9, 20_000), chunkStartMs = 190_000, positionMs = 200_000) }

        assertEquals(10_000L, listen.player.startPositions.last())
        assertEquals(PlaybackPhase.Playing, listen.controller.status.value.phase)
        assertEquals(200_000L, listen.controller.status.value.positionMs)
        assertFalse(
            "the notification was told the player stopped, which takes it away",
            Player.STATE_IDLE in listen.notificationStates,
        )
    }

    @Test
    fun `test that a pause while buffering for a seek is kept once the playlist restarts`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { stopAndBufferAt(200_000) }

        listen.run { pause() }

        assertEquals(PlaybackPhase.Paused, listen.controller.status.value.phase)
        assertEquals(200_000L, listen.controller.status.value.positionMs)

        listen.run { restartAt(chunk(9, 20_000), chunkStartMs = 190_000, positionMs = 200_000) }

        assertEquals(PlaybackPhase.Paused, listen.controller.status.value.phase)
        assertEquals(200_000L, listen.controller.status.value.positionMs)
    }

    @Test
    fun `test that a play while buffering for a seek does not play the playlist the seek left behind`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.run { pause() }
        listen.run { stopAndBufferAt(200_000) }

        listen.run { resume() }

        assertEquals("the old playlist was prepared again", Player.STATE_IDLE, listen.player.playbackState)
        assertEquals(PlaybackPhase.Buffering, listen.controller.status.value.phase)
        assertEquals(200_000L, listen.controller.status.value.positionMs)

        listen.run { restartAt(chunk(9, 20_000), chunkStartMs = 190_000, positionMs = 200_000) }

        assertEquals(Player.STATE_READY, listen.player.playbackState)
        assertTrue("the play was lost", listen.player.playWhenReady)
    }

    @Test
    fun `test that a seek from the notification is handed over and shown at once, without moving the player`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }

        listen.notification.seekTo(50_000)
        listen.idle()

        assertEquals(listOf(50_000L), listen.seekRequests)
        assertEquals(50_000L, listen.notification.currentPosition)
        assertTrue("the chunk playing was moved", listen.player.seeks.isEmpty())
    }

    @Test
    fun `test that skipping forward from the notification is a seek into the article rather than the chunk`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }
        listen.player.play(5_000)
        listen.idle()

        listen.notification.seekForward()
        listen.idle()

        assertEquals(listOf(5_000L + SEEK_FORWARD_INCREMENT_MS), listen.seekRequests)
        assertTrue("the chunk playing was moved", listen.player.seeks.isEmpty())
    }

    @Test
    fun `test that the notification is given the length of the article`() {
        listen.run { startArticle(chunk(0, 10_000), ArticleDisplayData(), TAB_ID) }

        listen.run { setArticleDuration(300_000) }

        assertEquals(300_000L, listen.notification.duration)
    }

    @Test
    fun `test that the length of the article is not given while nothing is connected`() {
        listen.run { setArticleDuration(300_000) }

        assertEquals("the playback was started to be given a length", 0, listen.connections)
    }
}

private const val TAB_ID = "tab"
