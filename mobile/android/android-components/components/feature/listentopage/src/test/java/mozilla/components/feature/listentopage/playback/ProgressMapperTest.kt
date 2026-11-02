/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.synthesis.SpeechRate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// Ten characters a second, so a hundred characters is ten seconds of speech and the arithmetic below is checkable by
// eye. All of it is made up: what a device really reads at is measured at runtime.
private val RATE = SpeechRate(charsPerSecond = 10.0)

/** A mapper over an article of [chunkChars] characters a chunk, of which [measured] have been made and measured. */
private fun mapper(
    chunkChars: List<Int>,
    measured: Map<Int, Duration> = emptyMap(),
    rate: SpeechRate? = RATE,
    chunkDurationsBeforePlaylistMs: List<Long> = emptyList(),
) =
    ArticleMeasurements(chunkCharacters = chunkChars, measuredDurations = measured, speechRate = rate)
        .mapper(chunkDurationsBeforePlaylistMs)

class ProgressMapperTest {

    @Test
    fun `test that a character offset maps to a chunk and back to a bar fraction`() {
        // Ten, twenty and thirty seconds of speech, so a minute of article.
        val mapper = mapper(listOf(100, 200, 300))

        assertEquals(0, mapper.chunkAt(0))
        assertEquals(0, mapper.chunkAt(99))
        assertEquals(1, mapper.chunkAt(100))
        assertEquals(1, mapper.chunkAt(299))
        assertEquals(2, mapper.chunkAt(300))
        assertEquals(0, mapper.characterOffsetOf(0))
        assertEquals(100, mapper.characterOffsetOf(1))
        assertEquals(300, mapper.characterOffsetOf(2))

        // A quarter of the way in is halfway through the second chunk, and maps back to it.
        assertEquals(0.25f, mapper.fractionAt(150), 0f)
        assertEquals(
            ChunkPosition(chunkIndex = 1, offsetMs = 5_000),
            mapper.positionAt(mapper.fractionAt(150)),
        )

        // Halfway in is the start of the third.
        assertEquals(0.5f, mapper.fractionAt(300), 0f)
        assertEquals(ChunkPosition(chunkIndex = 2, offsetMs = 0), mapper.positionAt(mapper.fractionAt(300)))
    }

    @Test
    fun `test that every chunk boundary maps back to the chunk it opens`() {
        val chunkChars = listOf(120, 340, 90, 500, 260)
        val mapper = mapper(chunkChars)

        for (chunk in chunkChars.indices) {
            val offset = mapper.characterOffsetOf(chunk)

            assertEquals("chunk $chunk", chunk, mapper.chunkAt(offset))
            assertEquals(
                "chunk $chunk",
                ChunkPosition(chunkIndex = chunk, offsetMs = 0),
                mapper.positionAt(mapper.fractionAt(offset)),
            )
        }
    }

    @Test
    fun `test that the bar reads zero before the opening has been measured`() {
        // Before the opening returns there is no article to divide up, and after it fails to measure there is no rate
        // to size one at. Neither is a fraction of anything, and neither may read as full or as not a number.
        val unsplit = mapper(chunkChars = emptyList(), rate = null).updatedProgress(ArticleProgress(), 0, false)
        val unmeasured =
            mapper(chunkChars = listOf(100, 200), rate = null).updatedProgress(ArticleProgress(), 3_000, false)

        assertEquals(ArticleProgress(), unsplit)
        assertEquals(ArticleProgress(), unmeasured)
        assertEquals(0f, unsplit.fraction, 0f)
        assertEquals(0f, unmeasured.fraction, 0f)
        assertFalse(unsplit.fraction.isNaN())
        assertFalse(unmeasured.fraction.isNaN())
    }

    @Test
    fun `test that a chunk not yet synthesized is estimated from the measured rate`() {
        val progress = mapper(listOf(100, 200)).updatedProgress(ArticleProgress(), 5_000, isArticleFinished = false)

        assertEquals(5_000, progress.positionMs)
        assertEquals(30_000, progress.durationMs)
    }

    @Test
    fun `test that a chunk with audio but no measured duration is sized from its characters`() {
        // The second chunk has audio, because the window ran ahead and made it, and no duration: the file is added
        // before it is read. It has to count as the sixteen seconds its characters are worth at the rate the first
        // chunk measured at, rather than as nothing.
        val progress =
            mapper(listOf(100, 200), measured = mapOf(0 to 8.seconds))
                .updatedProgress(ArticleProgress(), 1_000, isArticleFinished = false)

        assertEquals(24_000, progress.durationMs)
    }

    @Test
    fun `test that the bar reaches exactly one at the end of the article`() {
        val mapper = mapper(listOf(100, 200, 300), measured = mapOf(0 to 12.seconds, 1 to 19.seconds, 2 to 31.seconds))

        val progress =
            mapper.updatedProgress(
                previous = ArticleProgress(positionMs = 31_000, durationMs = 62_000),
                // Short of the end, as the last position the player sampled before the chunk ended always is.
                positionMs = 61_999,
                isArticleFinished = true,
            )

        assertEquals(62_000, progress.positionMs)
        assertEquals(62_000, progress.durationMs)
        assertEquals(1f, progress.fraction, 0f)
    }

    @Test
    fun `test that the bar stays short of full until the last chunk has ended`() {
        val mapper = mapper(List(3) { 100 }, measured = (0..2).associateWith { 10.seconds })

        // Every chunk of the article played in full, which on its own is a position at the end of the article and a bar
        // that would read as finished while the player is still reading the last of it out.
        val sampled = mapper.updatedProgress(ArticleProgress(), positionMs = 30_000, isArticleFinished = false)
        val ended = mapper.updatedProgress(sampled, positionMs = 30_000, isArticleFinished = true)

        assertEquals(30_000, sampled.positionMs)
        assertTrue("the bar is at ${sampled.fraction}", sampled.fraction < 1f)
        assertEquals(1f, ended.fraction, 0f)
    }

    @Test
    fun `test that replacing an estimate with a real duration never moves the bar backwards`() {
        val chunkChars = List(6) { 100 }
        // Real durations both longer and shorter than the ten seconds their characters estimate, so the article grows
        // and shrinks under the bar rather than only ever growing.
        val real = listOf(18.seconds, 6.seconds, 14.seconds, 9.seconds, 21.seconds, 5.seconds)
        val measured = mutableMapOf<Int, Duration>()
        var progress = ArticleProgress()

        for (chunk in chunkChars.indices) {
            // The window has made, and so measured, the chunks around this one before it is played.
            for (made in chunk..minOf(chunk + AUDIO_WINDOW_RADIUS, chunkChars.lastIndex)) {
                measured[made] = real[made]
            }
            val mapper = mapper(chunkChars, measured)
            // Where the chunk starts in the article, as the session reports it: the real lengths of those before it.
            val startMs = real.take(chunk).sumOf { it.inWholeMilliseconds }

            for (positionMs in 0..real[chunk].inWholeMilliseconds step 1_000) {
                val next = mapper.updatedProgress(progress, startMs + positionMs, isArticleFinished = false)

                assertTrue(
                    "chunk $chunk at ${positionMs}ms went back from ${progress.fraction} to ${next.fraction}",
                    next.fraction >= progress.fraction,
                )
                progress = next
            }

            progress =
                mapper.updatedProgress(
                    progress,
                    startMs + real[chunk].inWholeMilliseconds,
                    isArticleFinished = chunk == chunkChars.lastIndex,
                )
        }

        assertEquals(1f, progress.fraction, 0f)
    }

    @Test
    fun `test that a real duration shorter than its estimate does not pin the bar at full`() {
        val chunkChars = List(4) { 100 }
        // A quarter of what their characters estimate, so the article is four times shorter than the bar was drawn
        // against.
        val measured = (0..3).associateWith { 2_500.milliseconds }
        val halfway = ArticleProgress(positionMs = 20_000, durationMs = 40_000)

        // A second into the third chunk, as the session reports it from the real lengths.
        val progress =
            mapper(chunkChars, measured).updatedProgress(halfway, positionMs = 6_000, isArticleFinished = false)

        assertTrue("the bar is at ${progress.fraction}", progress.fraction < 1f)
        assertTrue("the position is inside the article", progress.positionMs <= progress.durationMs)
    }

    @Test
    fun `test that a seek moves the bar without stretching or shrinking the article`() {
        // Forty seconds of article, held at thirty while playback was halfway through.
        val mapper = mapper(List(4) { 100 })
        val playing = ArticleProgress(positionMs = 20_000, durationMs = 30_000)

        val back = mapper.updatedProgress(playing, positionMs = 5_000, isArticleFinished = false)
        val forward = mapper.updatedProgress(playing, positionMs = 35_000, isArticleFinished = false)

        assertEquals(40_000, back.durationMs)
        assertEquals(40_000, forward.durationMs)
    }

    @Test
    fun `test that an article of one chunk is a bar of its own`() {
        val mapper = mapper(listOf(100), measured = mapOf(0 to 10.seconds))

        assertEquals(0.5f, mapper.updatedProgress(ArticleProgress(), 5_000, false).fraction, 0f)
        assertEquals(1f, mapper.updatedProgress(ArticleProgress(), 0, isArticleFinished = true).fraction, 0f)
    }

    @Test
    fun `test that an article nothing can be sized from has no lengths at all`() {
        val mapper = mapper(chunkChars = listOf(100, 200), rate = null)

        assertEquals(emptyList<Long>(), mapper.chunkDurationsMs())
    }

    @Test
    fun `test that a position in the article resolves to the chunk holding it`() {
        val lengths = listOf(10_000L, 20_000L, 30_000L)

        assertEquals(ChunkPosition(chunkIndex = 0, offsetMs = 0), lengths.chunkPositionAt(0))
        assertEquals(ChunkPosition(chunkIndex = 0, offsetMs = 9_999), lengths.chunkPositionAt(9_999))
        assertEquals(ChunkPosition(chunkIndex = 1, offsetMs = 0), lengths.chunkPositionAt(10_000))
        assertEquals(ChunkPosition(chunkIndex = 1, offsetMs = 19_999), lengths.chunkPositionAt(29_999))
        assertEquals(ChunkPosition(chunkIndex = 2, offsetMs = 0), lengths.chunkPositionAt(30_000))
    }

    @Test
    fun `test that a chunk position resolves back to the position in the article it came from`() {
        val lengths = listOf(10_000L, 20_000L, 30_000L)

        assertEquals(0L, lengths.articlePositionOf(ChunkPosition(chunkIndex = 0, offsetMs = 0)))
        assertEquals(25_000L, lengths.articlePositionOf(ChunkPosition(chunkIndex = 1, offsetMs = 15_000)))
        for (positionMs in listOf(0L, 9_999L, 10_000L, 45_000L)) {
            assertEquals(positionMs, lengths.articlePositionOf(checkNotNull(lengths.chunkPositionAt(positionMs))))
        }
        assertEquals(
            25_000L,
            ArticleProgress(chunkDurationsMs = lengths)
                .articlePositionOf(ChunkPosition(chunkIndex = 1, offsetMs = 15_000)),
        )
    }

    @Test
    fun `test that a position past the end of the article lands at the end of its last chunk`() {
        val lengths = listOf(10_000L, 20_000L)

        assertEquals(ChunkPosition(chunkIndex = 1, offsetMs = 20_000), lengths.chunkPositionAt(1_000_000))
    }

    @Test
    fun `test that a position before the article lands at its start`() {
        val lengths = listOf(10_000L, 20_000L)

        assertEquals(ChunkPosition(chunkIndex = 0, offsetMs = 0), lengths.chunkPositionAt(-5_000))
    }

    @Test
    fun `test that an article with no lengths has nowhere to resolve a position to`() {
        assertNull(emptyList<Long>().chunkPositionAt(1_000))
    }

    // The lengths a reader was shown are the ones their position has to be read against, so resolving has to work off
    // a list they can be held rldrehrtr.
    @Test
    fun `test that a held set of lengths resolves the same after the article has moved on`() {
        val shown = mapper(chunkChars = listOf(100, 200, 300)).chunkDurationsMs()

        // The middle chunk turns out to be far shorter than its characters implied, which sizes the others at its rate
        // too and pulls everything after it earlier. The lengths taken before that still answer where they did.
        val moved = mapper(chunkChars = listOf(100, 200, 300), measured = mapOf(1 to 1.seconds)).chunkDurationsMs()

        assertEquals(ChunkPosition(chunkIndex = 1, offsetMs = 15_000), shown.chunkPositionAt(25_000))
        assertEquals(ChunkPosition(chunkIndex = 2, offsetMs = 1_500), moved.chunkPositionAt(25_000))
    }

    @Test
    fun `test that a position into a chunk is kept once the chunk is measured, short of the end of its audio`() {
        assertEquals(15_000L, offsetInMeasuredChunk(offsetMs = 15_000, measuredMs = 20_000))
        assertEquals(20_000L - CHUNK_END_MARGIN_MS, offsetInMeasuredChunk(offsetMs = 24_000, measuredMs = 20_000))
        assertEquals(24_000L, offsetInMeasuredChunk(offsetMs = 24_000, measuredMs = null))
    }

    @Test
    fun `test that an offset past the end of a chunk that turned out shorter lands at the same share of it`() {
        // Three quarters of a chunk estimated at forty seconds, which measured twenty.
        assertEquals(15_000L, offsetInMeasuredChunk(offsetMs = 30_000, measuredMs = 20_000, estimatedMs = 40_000))
        assertEquals(
            "a position the audio still holds was moved",
            15_000L,
            offsetInMeasuredChunk(offsetMs = 15_000, measuredMs = 20_000, estimatedMs = 40_000),
        )
    }

    @Test
    fun `test that the chunks before the playlist keep the lengths they had when it started`() {
        // The first chunk was estimated at ten seconds when the playlist started at the second. Measuring the second
        // at five seconds halves the rate the first would now be estimated at, which must not move the second.
        val mapper =
            mapper(
                chunkChars = listOf(100, 100, 100),
                measured = mapOf(1 to 5.seconds),
                rate = null,
                chunkDurationsBeforePlaylistMs = listOf(10_000),
            )

        assertEquals(listOf(10_000L, 5_000L, 5_000L), mapper.chunkDurationsMs())
        assertEquals(20_000L, mapper.updatedProgress(ArticleProgress(), 12_000, false).durationMs)
    }
}
