/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.content

import android.icu.text.BreakIterator
import java.util.Locale
import kotlin.math.ceil

/**
 * Splits article text into the chunks.
 *
 * Use [android] for the implementation backed by the platform ICU sentence boundaries, or supply a custom one.
 */
interface TextChunker {
    /**
     * Splits [text] into chunks of at most [maxInputLength] characters each, in reading order.
     *
     * Chunks are cut on sentence boundaries, so a chunk holds whole sentences and never starts mid-sentence. A single
     * sentence longer than [maxInputLength] is the one exception: it has no boundary to cut on, so it is split into
     * roughly equal parts instead of being dropped.
     *
     * @param text The article text.
     * @param maxInputLength The longest acceptable chunk. Must be positive.
     * @param languageTag BCP 47 language tag of the article, which decides where the sentences are. Must be well-
     *   formed: a page that declares no language of its own is the caller's to answer for, with the device language or
     *   with one it has guessed, because a chunker cannot tell which of the two the article is in.
     * @return The chunks, none of them blank, or an empty list when [text] holds nothing to read out.
     */
    fun chunk(text: String, maxInputLength: Int, languageTag: String): List<String>

    /**
     * Where the first sentence of [text] with something in it ends, or the length of [text] when it has none.
     *
     * @param text The article text.
     * @param languageTag BCP 47 language tag of the article, which decides where the sentences are.
     */
    fun firstSentenceEnd(text: String, languageTag: String): Int

    companion object {
        /** Creates a [TextChunker] that cuts on the sentence boundaries the platform ICU library reports. */
        fun android(): TextChunker = AndroidTextChunker()
    }
}

/**
 * [TextChunker] backed by the platform ICU [BreakIterator], which knows the sentence terminators of every language an
 * article can be in, rather than the ASCII ones alone.
 */
internal class AndroidTextChunker : TextChunker {

    override fun chunk(text: String, maxInputLength: Int, languageTag: String): List<String> {
        require(maxInputLength > 0) { "maxInputLength must be positive, was $maxInputLength" }

        val locale = localeFor(languageTag)

        return sentences(text, locale).flatMap { it.toSegments(maxInputLength, locale) }.packInto(maxInputLength)
    }

    override fun firstSentenceEnd(text: String, languageTag: String): Int {
        val boundaries = BreakIterator.getSentenceInstance(localeFor(languageTag))
        boundaries.setText(text)

        var end = boundaries.next()
        while (end != BreakIterator.DONE && text.substring(0, end).all(::isSpace)) {
            end = boundaries.next()
        }

        return if (end == BreakIterator.DONE) text.length else end
    }

    /**
     * The sentences of [text], excluding any "sentences" that contain only whitespace.
     *
     * A sentence that follows a line break is separated from the one before it by a single [LINE_SEPARATOR], so that
     * the engine pauses there. We associate the separator with the beginning of a sentence to allow the parts of an
     * oversized sentence to easily start chunks of their own. Otherwise, the first part may get packed together with
     * the sentence before it and lead to chunks of more uneven size.
     */
    private fun sentences(text: String, locale: Locale): List<Sentence> {
        val boundaries = BreakIterator.getSentenceInstance(locale)
        boundaries.setText(text)

        val sentences = mutableListOf<Sentence>()
        var separator = SEPARATOR
        var start = boundaries.first()
        var end = boundaries.next()
        while (end != BreakIterator.DONE) {
            val rawSentence = text.substring(start, end)
            val sentence = rawSentence.trim(::isSpace)
            if (sentence.isNotEmpty()) {
                sentences.add(Sentence(sentence, separator))
                separator = SEPARATOR
            }
            if (rawSentence.takeLastWhile(::isSpace).any(::isLineBreak)) separator = LINE_SEPARATOR

            start = end
            end = boundaries.next()
        }

        return sentences
    }

    /** The sentence as a single segment when it fits in [maxInputLength], or else broken into roughly even parts. */
    private fun Sentence.toSegments(maxInputLength: Int, locale: Locale): List<Segment> =
        if (text.length <= maxInputLength) {
            listOf(Segment(text, separator))
        } else {
            splitOversizedSentence(text, maxInputLength, locale).map { Segment(it, separator = null) }
        }

    /** Fills a chunk with segments until the next one would not fit, and then starts the next chunk on it. */
    private fun List<Segment>.packInto(maxInputLength: Int): List<String> {
        val chunks = mutableListOf<String>()
        val pending = StringBuilder()

        fun flush() {
            if (pending.isNotEmpty()) chunks.add(pending.toString())
            pending.clear()
        }

        for ((text, separator) in this) {
            when {
                separator == null -> {
                    flush()
                    chunks.add(text)
                }
                pending.isEmpty() -> pending.append(text)
                pending.length + separator.length + text.length > maxInputLength -> {
                    flush()
                    pending.append(text)
                }
                else -> pending.append(separator).append(text)
            }
        }

        flush()

        return chunks
    }

    /**
     * Splits one sentence that does not fit in a chunk on its own into parts of at most [maxInputLength] characters.
     *
     * The parts are as close to equal in length as the text allows, so that a sentence of two and a bit chunks is read
     * out as three even parts rather than as two full ones and a fragment. Each cut is moved back to the nearest line
     * boundary, which keeps words and CJK phrases whole; a stretch with no boundary at all, such as a very long URL, is
     * cut where it has to be.
     */
    private fun splitOversizedSentence(sentence: String, maxInputLength: Int, locale: Locale): List<String> {
        val parts = mutableListOf<String>()
        val boundaries = BreakIterator.getLineInstance(locale)
        var rest = sentence

        while (rest.length > maxInputLength) {
            val partCount = ceil(rest.length.toDouble() / maxInputLength)
            val target = ceil(rest.length / partCount).toInt()

            boundaries.setText(rest)
            val boundary = boundaries.preceding(target + 1)
            val cut = if (boundary >= (target + 1) / 2) boundary else hardCut(rest, target)

            parts.add(rest.substring(0, cut).trimEnd())
            rest = rest.substring(cut).trimStart()
        }
        if (rest.isNotEmpty()) parts.add(rest)

        return parts
    }

    /** Cuts [text] at [target], or one character earlier when that would tear a surrogate pair in half. */
    private fun hardCut(text: String, target: Int): Int =
        if (text[target - 1].isHighSurrogate()) (target - 1).coerceAtLeast(1) else target

    /** The locale [languageTag] names. Requires a well-formed tag. */
    private fun localeFor(languageTag: String): Locale {
        val locale = Locale.forLanguageTag(languageTag)
        require(locale.language.isNotEmpty()) { "languageTag must be a well formed BCP 47 tag, was \"$languageTag\"" }

        return locale
    }
}

// Separators can be different whitespace to break them up appropriately when they share a chunk.
// Semantically separated from Segment to indicate complete sentences.
private class Sentence(val text: String, val separator: String)

/**
 * Text that is inserted into a chunk. Semantically separated from Sentence because this can be either a whole
 * [Sentence] or a part of one, if the sentence is too long for a chunk.
 *
 * @property text The text, with no whitespace around it.
 * @property separator What goes between [text] and the segment before it when the two share a chunk. Null if [text] is
 *   a chunk of its own.
 */
private data class Segment(val text: String, val separator: String?)

private const val SEPARATOR = " "
private const val LINE_SEPARATOR = "\n"

private fun isLineBreak(char: Char) = char in "\n\r\u000B\u000C\u0085\u2028\u2029"

/** Whether [char] is whitespace, counting the line breaks such as U+0085 that [Char.isWhitespace] does not. */
private fun isSpace(char: Char) = char.isWhitespace() || isLineBreak(char)
