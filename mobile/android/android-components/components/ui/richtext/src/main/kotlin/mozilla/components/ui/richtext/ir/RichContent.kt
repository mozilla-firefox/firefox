/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
@file:Suppress("UndocumentedPublicClass")

package mozilla.components.ui.richtext.ir

import androidx.compose.runtime.Immutable
import androidx.compose.runtime.Stable

/**
 * Intermediate Representation (IR) of a rich document.
 *
 * This is meant to be an abstraction that is agnostic of the underlying parser or rich content format.
 */
@Immutable
data class RichDocument(val blocks: List<BlockContent>)

/**
 * Inline content.
 */
@Stable
sealed interface InlineContent {

    data object LineBreak : InlineContent
    data class Plain(val value: String) : InlineContent
    data class Strong(val children: List<InlineContent>) : InlineContent
    data class Emphasis(val children: List<InlineContent>) : InlineContent
    data class Code(val value: String) : InlineContent
    data class Link(val url: String, val children: List<InlineContent>) : InlineContent
}

@JvmInline
@Stable
value class HeadingLevel(val level: Int) {

    companion object {
        val H1 = HeadingLevel(1)
        val H2 = HeadingLevel(2)
        val H3 = HeadingLevel(3)
        val H4 = HeadingLevel(4)
        val H5 = HeadingLevel(5)
        val H6 = HeadingLevel(6)
    }
}

/**
 * The most top-level type of content that is in a [RichDocument]
 *
 * A [BlockContent] can contain any combination of one or more [InlineContent] and other [BlockContent]
 */
@Stable
sealed interface BlockContent {

    /**
     * A paragraph of inline content.
     *
     * @param content The inline contents of the paragraph.
     */
    @Immutable
    data class Paragraph(
        val content: List<InlineContent>,
    ) : BlockContent

    /**
     * A heading containing inline content.
     *
     * @param level The level of the heading.
     * @param content The inline contents of the heading.
     */
    @Immutable
    data class Heading(
        val level: HeadingLevel,
        val content: List<InlineContent>,
    ) : BlockContent

    /**
     * A list of items.
     *
     * @param ordered Whether the list is ordered.
     * @param items The list items.
     */
    @Immutable
    data class ListBlock(
        val ordered: Boolean = false,
        val items: List<ListItem> = emptyList(),
    ) : BlockContent {

        /**
         * A list item.
         *
         * @param content The contents of the list item.
         */
        data class ListItem(val content: List<BlockContent>)
    }

    /**
     * A block quote containing other block content.
     *
     * @param content The block contents of the quote.
     */
    @Immutable
    data class BlockQuote(
        val content: List<BlockContent>,
    ) : BlockContent
}
