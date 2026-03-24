/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization.eligibility

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.pageextraction.PageMetadata
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * Checks if a session is eligible for summarization
 */
interface SummarizationEligibilityChecker {

    /**
     * Checks if a session is eligible for summarization based on multiple criteria.
     *
     * @param session The session to check
     */
    suspend fun check(session: EngineSession): Result<Boolean>

    /**
     * Checks if a session is eligible for summarization based on language only.
     *
     * @param session The session to check
     */
    suspend fun checkLanguage(session: EngineSession): Result<Boolean>
}

/**
 * Default implementation for checking if a content is eligible for summarization
 */
internal class DefaultSummarizationEligibilityChecker : SummarizationEligibilityChecker {

    override suspend fun check(session: EngineSession): Result<Boolean> {
        // Remove in https://bugzilla.mozilla.org/show_bug.cgi?id=2020509 has landed
        // we will no longer need to get the entire page content just to check for eligibility
        return session.getPageMetadata()
            .map { metadata ->
                metadata.verifyEligibility()
            }
    }

    override suspend fun checkLanguage(session: EngineSession): Result<Boolean> {
        return session.getPageMetadata()
            .map { metadata ->
                metadata.language.inAcceptedLanguages()
            }
    }

    private suspend fun EngineSession.getPageMetadata() = runCatching {
        withContext(Dispatchers.Main) {
            suspendCancellableCoroutine { continuation ->
                getPageMetadata(
                    onResult = { metadata ->
                        continuation.resume(metadata)
                    },
                    onException = { error ->
                        continuation.resumeWithException(error)
                    },
                )
            }
        }
    }

    private fun PageMetadata.verifyEligibility(): Boolean {
        return wordCount in WORD_COUNT_RANGE && language.inAcceptedLanguages()
    }

    private fun String.inAcceptedLanguages() = listOf("en").any { acceptedLang ->
        this.contains(acceptedLang)
    }

    companion object {
        private val WORD_COUNT_RANGE = IntRange(100, 5000)
    }
}
