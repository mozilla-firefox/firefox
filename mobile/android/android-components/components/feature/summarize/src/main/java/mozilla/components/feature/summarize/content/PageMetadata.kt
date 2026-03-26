/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.content

import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm

/**
 * An interface to conform to do deliver page metadata.
 */
fun interface PageMetadataExtractor {
    /**
     * Retrieve the page metadata.
     */
    suspend fun getPageMetadata(): Result<PageMetadata>

    /**
     * An exception that occurs in page metadata extraction.
     */
    class Exception : Llm.Exception("Could not extract content metadata", errorCode)
}

/**
 * Page metadata required for logical choices.
 */
data class PageMetadata(val structuredDataTypes: List<String>, val language: String)

private val errorCode = ErrorCode(2002)
