/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.Flow
import mozilla.components.concept.llm.Llm

/**
 * Content metadata used to determine whether summarization should run.
 */
data class ContentProperties(
    val contentLength: Int,
    val isReaderable: Boolean,
)

/**
 * Abstraction for components that can summarize reader content.
 */
interface Summarizer {
    /**
     * Generates summary responses for the provided content.
     */
    suspend fun summarize(content: String, prompt: String): Flow<Llm.Response>

    /**
     * Returns whether content can be summarized based on metadata and LLM readiness.
     */
    suspend fun canSummarize(contentProperties: ContentProperties): Boolean
}
