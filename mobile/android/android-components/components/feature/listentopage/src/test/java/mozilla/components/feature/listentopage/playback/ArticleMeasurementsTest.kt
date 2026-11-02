/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import mozilla.components.feature.listentopage.synthesis.SpeechRate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Measurements of an article of [chunkChars] characters a chunk, of which [measured] have been made and measured. */
private fun measurements(
    chunkChars: List<Int>,
    measured: Map<Int, Duration> = emptyMap(),
    rate: SpeechRate? = SpeechRate(charsPerSecond = 10.0),
) = ArticleMeasurements(chunkCharacters = chunkChars, measuredDurations = measured, speechRate = rate)

class ArticleMeasurementsTest {

    @Test
    fun `test that the text position counts the characters before the position and the fraction of its chunk read`() {
        val measurements = ArticleMeasurements(chunkCharacters = listOf(100, 300, 600))

        // Halfway through the second chunk, which lasts twice as long per character as the first.
        val textPosition =
            measurements.getTextPositionAfterVoiceChanged(
                positionMs = 20_000,
                chunkDurationsMs = listOf(10_000, 20_000, 30_000),
            )

        assertEquals((100 + 150) / 1_000f, textPosition!!, 0.0001f)
    }

    @Test
    fun `test that a text position is in the chunk that holds its character, up to the end of the text`() {
        val measurements = ArticleMeasurements(chunkCharacters = listOf(100, 300, 600))

        assertEquals(0, measurements.chunkIndexAtTextPosition(0f))
        assertEquals(1, measurements.chunkIndexAtTextPosition(0.1f))
        assertEquals(1, measurements.chunkIndexAtTextPosition(0.25f))
        assertEquals(2, measurements.chunkIndexAtTextPosition(0.4f))
        assertEquals(2, measurements.chunkIndexAtTextPosition(1f))
    }

    @Test
    fun `test that a text position and the progress at it lead back to each other`() {
        val measurements =
            ArticleMeasurements(
                chunkCharacters = listOf(100, 300, 600),
                measuredDurations = mapOf(0 to 10.seconds, 1 to 60.seconds),
                speechRate = SpeechRate(charsPerSecond = 10.0),
            )

        val progress = measurements.getArticleProgressAtTextPosition(0.7f)
        val textPosition = measurements.getTextPositionAfterVoiceChanged(progress.positionMs, progress.chunkDurationsMs)

        assertEquals(0.7f, textPosition!!, 0.0001f)
    }

    @Test
    fun `test that the progress at a text position is placed in the chunk that holds it, by its measured length`() {
        // The second chunk was measured at twice its estimate, which the progress is placed by.
        val measurements =
            ArticleMeasurements(
                chunkCharacters = listOf(100, 300, 600),
                measuredDurations = mapOf(0 to 10.seconds, 1 to 60.seconds),
                speechRate = SpeechRate(charsPerSecond = 10.0),
            )

        val progress = measurements.getArticleProgressAtTextPosition(0.25f)

        assertEquals(10_000L + 30_000L, progress.positionMs)
        assertEquals(progress.chunkDurationsMs.sum(), progress.durationMs)
        assertEquals(60_000L, progress.chunkDurationsMs[1])
        assertEquals(progress.positionMs.toFloat() / progress.durationMs, progress.barPosition!!, 0f)
    }

    @Test
    fun `test that an article that has not been split has no text position`() {
        assertNull(
            ArticleMeasurements().getTextPositionAfterVoiceChanged(positionMs = 0, chunkDurationsMs = emptyList())
        )
        assertNull(ArticleMeasurements().chunkIndexAtTextPosition(0.5f))
    }

    @Test
    fun `test that the lengths are measured where they can be and estimated where they cannot`() {
        val measurements = measurements(chunkChars = listOf(100, 200, 300), measured = mapOf(1 to 5.seconds))

        // The one that has been made counts for what it measured, and the forty characters a second it measured at
        // sizes the others, rather than the ten the opening was given.
        assertEquals(listOf(2_500L, 5_000L, 7_500L), measurements.mapper().chunkDurationsMs())
    }

    @Test
    fun `test that one measured chunk gives the rest of the article a length without an opening rate`() {
        val measurements = measurements(chunkChars = listOf(100, 200), measured = mapOf(0 to 10.seconds), rate = null)

        // The chunk that was made implies ten characters a second, which is enough to size the one that was not.
        assertEquals(listOf(10_000L, 20_000L), measurements.mapper().chunkDurationsMs())
    }

    @Test
    fun `test that a chunk far past the measured ones is estimated from the nearest of them, not as nothing`() {
        // Only the start has been made, as after a seek far ahead of the window.
        val lengths =
            measurements(chunkChars = List(20) { 100 }, measured = mapOf(0 to 10.seconds, 1 to 10.seconds), rate = null)
                .mapper()
                .chunkDurationsMs()

        assertEquals(10_000L, lengths[19])
    }

    @Test
    fun `test that a chunk that has not been measured is estimated from the measured chunks nearest it`() {
        // Ten seconds a hundred characters at the start, and twenty at the end, as references at the end of an article
        // are read out more slowly than the text before them.
        val lengths =
            measurements(
                    chunkChars = List(10) { 100 },
                    measured = mapOf(0 to 10.seconds, 1 to 10.seconds, 8 to 20.seconds, 9 to 20.seconds),
                    rate = null,
                )
                .mapper()
                .chunkDurationsMs()

        // Weighted by how near each measured chunk is: a third chunk mostly like the start, an eighth like the end.
        assertEquals(11_711.0, lengths[2].toDouble(), 1.0)
        assertEquals(18_289.0, lengths[7].toDouble(), 1.0)
    }

    @Test
    fun `test that a slow part measured at one end does not stretch the estimate of the other`() {
        val chunkChars = List(10) { 100 }
        val start = mapOf(0 to 10.seconds, 1 to 10.seconds)

        val before = measurements(chunkChars, measured = start, rate = null).mapper().chunkDurationsMs()
        val after =
            measurements(chunkChars, measured = start + mapOf(8 to 20.seconds, 9 to 20.seconds), rate = null)
                .mapper()
                .chunkDurationsMs()

        assertEquals(10_000L, before[2])
        // Averaged over the whole article, it would have gone to fifteen seconds.
        assertTrue("the start was stretched to ${after[2]}", after[2] < 12_000)
    }

    @Test
    fun `test that a short opening counts for little in the estimate of the chunk after it`() {
        // A site's name read out slowly, then a full chunk read out at the rate of the rest of the article.
        val lengths =
            measurements(
                    chunkChars = listOf(15, 500, 500),
                    measured = mapOf(0 to 2_500.milliseconds, 2 to 30.seconds),
                    rate = null,
                )
                .mapper()
                .chunkDurationsMs()

        // Counting the opening as much as the full chunk would have made the second chunk about fifty-seven seconds.
        assertEquals(31_553.0, lengths[1].toDouble(), 1.0)
    }
}
