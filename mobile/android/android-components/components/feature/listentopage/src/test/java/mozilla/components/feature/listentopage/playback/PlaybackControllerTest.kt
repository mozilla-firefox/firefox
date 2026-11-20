/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Test

class PlaybackControllerTest {

    @Test
    fun `test that a player with nothing to play is idle`() {
        val player = player(state = Player.STATE_IDLE)

        assertEquals(PlaybackPhase.Idle, player.toPlaybackPhase())
    }

    @Test
    fun `test that a player that has played everything it was given has ended`() {
        val player = player(state = Player.STATE_ENDED)

        assertEquals(PlaybackPhase.Ended, player.toPlaybackPhase())
    }

    @Test
    fun `test that a player waiting on audio it was asked to play is buffering`() {
        val player = player(state = Player.STATE_BUFFERING, playWhenReady = true)

        assertEquals(PlaybackPhase.Buffering, player.toPlaybackPhase())
    }

    // Buffering while the user has paused is a pause, not a wait: nothing is going to come out of the speaker when the
    // audio arrives, so showing a spinner would be a lie.
    @Test
    fun `test that a paused player waiting on audio is paused rather than buffering`() {
        val player = player(state = Player.STATE_BUFFERING, playWhenReady = false)

        assertEquals(PlaybackPhase.Paused, player.toPlaybackPhase())
    }

    @Test
    fun `test that a player with audio coming out of it is playing`() {
        val player = player(state = Player.STATE_READY, playWhenReady = true, isPlaying = true)

        assertEquals(PlaybackPhase.Playing, player.toPlaybackPhase())
    }

    @Test
    fun `test that a player holding a ready track is paused`() {
        val player = player(state = Player.STATE_READY, playWhenReady = false)

        assertEquals(PlaybackPhase.Paused, player.toPlaybackPhase())
    }

    // Something else holding audio focus leaves the player ready and still asked to play, so playWhenReady alone would
    // report audio the user cannot hear as playing.
    @Test
    fun `test that a player asked to play but suppressed is paused`() {
        val player = player(state = Player.STATE_READY, playWhenReady = true, isPlaying = false)

        assertEquals(PlaybackPhase.Paused, player.toPlaybackPhase())
    }

    // A player that fails drops to idle carrying the error, so the error has to be read before the state or every
    // failure is reported as an ordinary stop.
    @Test
    fun `test that a player carrying an error has failed rather than gone idle`() {
        val player = player(state = Player.STATE_IDLE, error = mock<PlaybackException>())

        assertEquals(PlaybackPhase.Failed, player.toPlaybackPhase())
    }

    @Test
    fun `test that a player carrying an error has failed however far it had got`() {
        val states = listOf(Player.STATE_IDLE, Player.STATE_BUFFERING, Player.STATE_READY, Player.STATE_ENDED)

        states.forEach { state ->
            val player = player(state = state, playWhenReady = true, isPlaying = true, error = mock())

            assertEquals("state $state was not reported as failed", PlaybackPhase.Failed, player.toPlaybackPhase())
        }
    }

    @Test
    fun `test that a page with no title is read out under the app's name`() {
        val displayData = ArticleDisplayData(site = "example.org").withAppNameIfUntitled("Firefox Nightly")

        assertEquals(ArticleDisplayData(title = "Firefox Nightly", site = "example.org"), displayData)
    }

    @Test
    fun `test that a page with a title keeps the title`() {
        val displayData = ArticleDisplayData(title = "An article", site = "example.org")

        assertEquals(displayData, displayData.withAppNameIfUntitled("Firefox Nightly"))
    }

    @Test
    fun `test that an article from a private tab hides its title and site`() {
        val displayData =
            ArticleDisplayData(title = "An article", site = "example.org", isPrivate = true)
                .hiddenIfPrivate("Private tab", "Website hidden")

        assertEquals(ArticleDisplayData(title = "Private tab", site = "Website hidden", isPrivate = true), displayData)
    }

    @Test
    fun `test that an article from a normal tab keeps its title and site`() {
        val displayData = ArticleDisplayData(title = "An article", site = "example.org")

        assertEquals(displayData, displayData.hiddenIfPrivate("Private tab", "Website hidden"))
    }

    private fun player(
        state: Int,
        playWhenReady: Boolean = false,
        isPlaying: Boolean = false,
        error: PlaybackException? = null,
    ): Player = mock {
        whenever(this.playbackState).thenReturn(state)
        whenever(this.playWhenReady).thenReturn(playWhenReady)
        whenever(this.isPlaying).thenReturn(isPlaying)
        whenever(this.playerError).thenReturn(error)
    }
}
