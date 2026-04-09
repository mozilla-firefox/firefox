/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.content

import mozilla.components.feature.summarize.ext.shouldUseReaderModeContent

/**
 * Represents the extracted content of a web page, combining its textual body and metadata.
 *
 * @property metadata Metadata associated with the page, such as title and author.
 * @property body The main textual content of the page.
 */

data class Content(
    val metadata: PageMetadata = PageMetadata(),
    val body: String = "",
)

/**
 * Provides the [Content] of a web page for summarization.
 *
 * Use [fromPage] to create an instance backed by a [PageContentExtractor] and
 * [PageMetadataExtractor], or supply a custom implementation.
 */
fun interface ContentProvider {
    /**
     * Returns the page [Content], or a failure if the content could not be retrieved.
     */
    suspend fun getContent(): Result<Content>

    companion object {
        /**
         * Creates a [ContentProvider] that derives [Content] from the given extractors.
         *
         * Metadata failures are non-fatal and fall back to a default [PageMetadata].
         * Content failures are propagated and cause the returned [Result] to fail.
         *
         * @param pageContentExtractor Extracts the main textual content of the page.
         * @param pageMetadataExtractor Extracts metadata such as the page title and author.
         */
        fun fromPage(
            pageContentExtractor: PageContentExtractor,
            pageMetadataExtractor: PageMetadataExtractor,
        ) = ContentProvider {
            runCatching {
                val metadata = pageMetadataExtractor.getPageMetadata().getOrDefault(PageMetadata())
                val content = pageContentExtractor.getPageContent(
                    options = PageContentExtractor.Options(
                        shouldUseReaderModeContent = metadata.shouldUseReaderModeContent,
                    ),
                ).getOrThrow()

                Content(metadata, content)
            }
        }
    }
}
