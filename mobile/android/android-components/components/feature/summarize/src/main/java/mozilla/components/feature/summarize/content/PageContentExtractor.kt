/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.content

import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm

/**
 * An interface to conform to do deliver page content for summarization.
 */
fun interface PageContentExtractor {
    /**
     * Retrieve the page content.
     */
    suspend fun getPageContent(): Result<String>

    /**
     * An exception that occurs in page content extraction.
     */
    class Exception : Llm.Exception("Could not extract content", errorCode)
}

private val errorCode = ErrorCode(2001)
