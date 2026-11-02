/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.content

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertFailsWith
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

private val ENGLISH_SENTENCES =
    listOf(
        "The quick brown fox jumps over the lazy dog.",
        "Pack my box with five dozen liquor jugs.",
        "How vexingly quick daft zebras jump!",
        "Is this a question?",
    )

private val GERMAN_SENTENCES =
    listOf(
        "Der schnelle braune Fuchs springt über den faulen Hund.",
        "Wie viele Bücher hast du in diesem Sommer gelesen?",
        "Franz jagt im komplett verwahrlosten Taxi quer durch Bayern.",
        "Das ist wirklich beeindruckend!",
    )

private val CHINESE_SENTENCES =
    listOf(
        "今天天气很好。",
        "我们一起去公园散步吧。",
        "你觉得这个主意怎么样？",
        "那我们明天早上八点出发。",
    )

private val JAPANESE_SENTENCES =
    listOf(
        "今日はいい天気ですね。",
        "公園まで散歩に行きましょう。",
        "この案についてどう思いますか？",
        "では明日の朝八時に出発しましょう。",
    )

@RunWith(AndroidJUnit4::class)
class TextChunkerTest {

    private val chunker = AndroidTextChunker()

    @Test
    fun `test that English text is cut on sentence boundaries`() {
        val text = ENGLISH_SENTENCES.joinToString(" ")

        val chunks = chunker.chunk(text, maxInputLength = 90, languageTag = "en")

        assertEquals(
            listOf(
                "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs.",
                "How vexingly quick daft zebras jump! Is this a question?",
            ),
            chunks,
        )
    }

    @Test
    fun `test that chunking a chunk again cuts it at its own front`() {
        val chunk = chunker.chunk(ENGLISH_SENTENCES.joinToString(" "), 200, "en").first()

        val pieces = chunker.chunk(chunk, 50, "en")

        assertTrue("Only ${pieces.size} pieces to walk", pieces.size > 1)
        assertTrue("<${pieces.first()}> is not the front of <$chunk>", chunk.startsWith(pieces.first()))
        assertEquals(chunk, pieces.joinToString(" "))
    }

    @Test
    fun `test that chunking a chunk which already fits gives it back whole`() {
        val chunk = chunker.chunk(ENGLISH_SENTENCES.joinToString(" "), 200, "en").first()

        assertEquals(listOf(chunk), chunker.chunk(chunk, 200, "en"))
    }

    @Test
    fun `test that German text is cut on sentence boundaries`() =
        assertChunksHoldWholeSentences(GERMAN_SENTENCES, separator = " ", languageTag = "de", maxInputLength = 120)

    @Test
    fun `test that Chinese text is cut on sentence boundaries`() =
        assertChunksHoldWholeSentences(CHINESE_SENTENCES, separator = "", languageTag = "zh-CN", maxInputLength = 20)

    @Test
    fun `test that Japanese text is cut on sentence boundaries`() =
        assertChunksHoldWholeSentences(JAPANESE_SENTENCES, separator = "", languageTag = "ja", maxInputLength = 30)

    @Test
    fun `test that no chunk is longer than the limit`() {
        val cases =
            listOf(
                ENGLISH_SENTENCES.joinToString(" ") to "en",
                GERMAN_SENTENCES.joinToString(" ") to "de",
                CHINESE_SENTENCES.joinToString("") to "zh-CN",
                JAPANESE_SENTENCES.joinToString("") to "ja",
            )

        for ((text, languageTag) in cases) {
            for (maxInputLength in 1..text.length + 1) {
                val chunks = chunker.chunk(text, maxInputLength, languageTag)

                for (chunk in chunks) {
                    assertTrue(
                        "$languageTag chunk of ${chunk.length} characters exceeds $maxInputLength: <$chunk>",
                        chunk.length <= maxInputLength,
                    )
                    assertTrue(chunk.isNotBlank())
                }
                assertEquals(
                    "$languageTag text was not preserved at a limit of $maxInputLength",
                    text.filterNot { it.isWhitespace() },
                    chunks.joinToString("").filterNot { it.isWhitespace() },
                )
            }
        }
    }

    @Test
    fun `test that a sentence longer than the limit is split into roughly equal parts`() {
        val maxInputLength = 100
        val sentence = (1..60).joinToString(" ") { "word$it" } + "."

        val chunks = chunker.chunk(sentence, maxInputLength, languageTag = "en")

        assertEquals(ceilingDivision(sentence.length, maxInputLength), chunks.size)
        assertRoughlyEqual(chunks, maxInputLength)
        assertTrue("A word was torn in half: $chunks", chunks.all { it.matches(Regex("""[\w .]+""")) })
        assertEquals(sentence, chunks.joinToString(" "))
    }

    @Test
    fun `test that a sentence with no place to break is still split into roughly equal parts`() {
        val sentence = "x".repeat(250) + "."

        val chunks = chunker.chunk(sentence, maxInputLength = 100, languageTag = "en")

        assertEquals(listOf(84, 84, 83), chunks.map { it.length })
        assertEquals(sentence, chunks.joinToString(""))
    }

    @Test
    fun `test that an emoji sitting on a cut is not torn in half`() {
        // 150 characters, so the sentence is cut in two, with the emoji straddling the 75 character mark.
        val sentence = "x".repeat(74) + "😀" + "x".repeat(74)

        val chunks = chunker.chunk(sentence, maxInputLength = 100, languageTag = "en")

        assertEquals(listOf("x".repeat(74), "😀" + "x".repeat(74)), chunks)
        assertNoLoneSurrogates(chunks)
    }

    @Test
    fun `test that a cut with nowhere to go still leaves the surrogate pairs of an emoji whole`() {
        // A family emoji is a single grapheme of eleven characters that a line boundary cannot be found inside, so a
        // limit this small leaves the cut nowhere to go but the middle of it.
        val maxInputLength = 5
        val sentence = "👨‍👩‍👧‍👦"

        val chunks = chunker.chunk(sentence, maxInputLength, languageTag = "en")

        assertNoLoneSurrogates(chunks)
        assertTrue("A chunk is too long: ${chunks.map { it.length }}", chunks.all { it.length <= maxInputLength })
        assertEquals(sentence, chunks.joinToString(""))
    }

    @Test
    fun `test that a Japanese sentence longer than the limit is split into roughly equal parts`() {
        val maxInputLength = 40
        val sentence = "長い文章を読み上げるためには適切な分割が必要になります".repeat(4) + "。"

        val chunks = chunker.chunk(sentence, maxInputLength, languageTag = "ja")

        assertEquals(ceilingDivision(sentence.length, maxInputLength), chunks.size)
        assertRoughlyEqual(chunks, maxInputLength)
        assertEquals(sentence, chunks.joinToString(""))
    }

    @Test
    fun `test that text without a sentence terminator is one chunk`() {
        val text = "This article has a title that simply stops"

        assertEquals(listOf(text), chunker.chunk(text, maxInputLength = 4000, languageTag = "en"))
        assertEquals(listOf("見出しには句点がありません"), chunker.chunk("見出しには句点がありません", 4000, "ja"))
    }

    @Test
    fun `test that a sentence is never cut in half when it fits in a chunk of its own`() {
        val text = ENGLISH_SENTENCES.joinToString(" ")

        val chunks = chunker.chunk(text, maxInputLength = ENGLISH_SENTENCES.maxOf { it.length }, languageTag = "en")

        assertEquals(ENGLISH_SENTENCES, chunks)
    }

    @Test
    fun `test that text with nothing to read out produces no chunks`() {
        assertEquals(emptyList<String>(), chunker.chunk("", maxInputLength = 100, languageTag = "en"))
        assertEquals(emptyList<String>(), chunker.chunk("   \n\n \t ", maxInputLength = 100, languageTag = "en"))
    }

    @Test
    fun `test that the whitespace between two sentences on one line is read out as a single space`() {
        val text = "First sentence.     Second sentence.\t Third sentence."

        assertEquals(
            listOf("First sentence. Second sentence. Third sentence."),
            chunker.chunk(text, 100, languageTag = "en"),
        )
    }

    @Test
    fun `test that the whitespace between two lines is read out as a single line break`() {
        val text = "First paragraph.\n\n     Second paragraph.\n"

        assertEquals(listOf("First paragraph.\nSecond paragraph."), chunker.chunk(text, 40, languageTag = "en"))
        assertEquals(listOf("First paragraph.", "Second paragraph."), chunker.chunk(text, 20, languageTag = "en"))
    }

    @Test
    fun `test that a line without a sentence terminator is kept apart from the next one`() {
        val text = "A heading\nThe first sentence. The second sentence.\r\nA list item\u2028Another list item"

        assertEquals(
            listOf("A heading\nThe first sentence. The second sentence.\nA list item\nAnother list item"),
            chunker.chunk(text, 200, languageTag = "en"),
        )
    }

    @Test
    fun `test that a line break at the start of the text is dropped`() {
        assertEquals(listOf("A heading\nA sentence."), chunker.chunk("\n\nA heading\nA sentence.", 100, "en"))
    }

    @Test
    fun `test that a blank line holding other whitespace is read out as a single line break`() {
        val text = "First paragraph.\n   \t    \n  Second paragraph."

        assertEquals(listOf("First paragraph.\nSecond paragraph."), chunker.chunk(text, 100, languageTag = "en"))
    }

    @Test
    fun `test that a paragraph separator is read out as a line break`() {
        assertEquals(listOf("A heading\nA sentence."), chunker.chunk("A heading A sentence.", 100, "en"))
    }

    @Test
    fun `test that a next line character is read out as a line break`() {
        assertEquals(listOf("A heading\nA sentence."), chunker.chunk("A heading\u0085A sentence.", 100, "en"))
    }

    @Test
    fun `test that the parts of a sentence longer than the limit share a chunk with nothing`() {
        val sentence = (1..15).joinToString(" ") { "word$it" } + "."
        val text = "A heading\n$sentence\nThe end."

        val chunks = chunker.chunk(text, maxInputLength = 50, languageTag = "en")

        assertEquals("A heading", chunks.first())
        assertEquals("The end.", chunks.last())
        assertEquals(sentence, chunks.subList(1, chunks.size - 1).joinToString(" "))
    }

    @Test
    fun `test that a line break counts towards the limit`() {
        val text = "A heading\nA sentence."

        assertEquals(listOf("A heading\nA sentence."), chunker.chunk(text, 21, languageTag = "en"))
        assertEquals(listOf("A heading", "A sentence."), chunker.chunk(text, 20, languageTag = "en"))
    }

    @Test
    fun `test that an unusable limit is rejected`() {
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 0, languageTag = "en") }
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", -1, languageTag = "en") }
    }

    @Test
    fun `test that a language tag that is not well formed is rejected`() {
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 100, languageTag = "") }
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 100, languageTag = "  ") }
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 100, languageTag = "e") }
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 100, languageTag = "-en") }
        assertFailsWith<IllegalArgumentException> { chunker.chunk("Some text.", 100, languageTag = "en_US") }
    }

    @Test
    fun `test that a well formed language tag with a region or a script is accepted`() {
        val text = "One sentence. And another."

        assertEquals(listOf(text), chunker.chunk(text, 100, languageTag = "en-GB"))
        assertEquals(listOf("今天天气很好。"), chunker.chunk("今天天气很好。", 100, languageTag = "zh-Hant-TW"))
    }

    /**
     * Asserts that no chunk has been cut through the middle of a surrogate pair, which would leave it holding half of a
     * character that the engine can only read as a replacement character.
     */
    private fun assertNoLoneSurrogates(chunks: List<String>) {
        for (chunk in chunks) {
            assertFalse("Chunk starts on the low half of a surrogate pair: <$chunk>", chunk.first().isLowSurrogate())
            assertFalse("Chunk ends on the high half of a surrogate pair: <$chunk>", chunk.last().isHighSurrogate())
        }
    }

    /** Asserts that the chunks of an article written as [sentences] hold nothing but whole sentences, in order. */
    private fun assertChunksHoldWholeSentences(
        sentences: List<String>,
        separator: String,
        languageTag: String,
        maxInputLength: Int,
    ) {
        val text = sentences.joinToString(separator)

        val chunks = chunker.chunk(text, maxInputLength, languageTag)

        assertTrue("Expected more than one chunk, got $chunks", chunks.size > 1)
        chunks.forEach { assertTrue("Chunk is too long: <$it>", it.length <= maxInputLength) }

        val remaining = sentences.toMutableList()
        for (chunk in chunks) {
            val rebuilt = StringBuilder()
            while (rebuilt.toString() != chunk) {
                assertTrue("Chunk <$chunk> does not start on a sentence boundary", remaining.isNotEmpty())
                if (rebuilt.isNotEmpty()) rebuilt.append(" ")
                rebuilt.append(remaining.removeAt(0))
                assertTrue("Chunk <$chunk> is cut inside a sentence", chunk.startsWith(rebuilt.toString()))
            }
        }
        assertTrue("Sentences were dropped: $remaining", remaining.isEmpty())
    }

    private fun assertRoughlyEqual(chunks: List<String>, maxInputLength: Int) {
        val shortest = chunks.minOf { it.length }

        assertTrue(
            "A part is much shorter than the rest: ${chunks.map { it.length }}",
            shortest >= maxInputLength * 3 / 4,
        )
    }

    private fun ceilingDivision(length: Int, maxInputLength: Int) = (length + maxInputLength - 1) / maxInputLength
}
