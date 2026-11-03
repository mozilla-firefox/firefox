/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.content

import java.util.Locale
import kotlinx.coroutines.CancellationException

/**
 * The article a listening session reads out.
 *
 * @property text The article text, as plain prose.
 * @property languageTag BCP 47 language tag of the article, used to pick a voice and to find sentence boundaries.
 * @property languageSource Where [languageTag] came from.
 */
data class Content(
    val text: String,
    val languageTag: String,
    val languageSource: LanguageSource = LanguageSource.Page,
)

/** Where the language of an article came from. */
enum class LanguageSource {
    /** The page declares the language. */
    Page,

    /** The page declares no usable language, so the fallback language is used. */
    Fallback,
}

/**
 * Provides the [Content] of the article a listening session reads out.
 *
 * Use [fromPage] to create an instance backed by a [PageContentExtractor] and a [PageMetadataExtractor], or supply a
 * custom implementation.
 */
fun interface ContentExtractor {
    /** Returns the [Content] of the page loaded in [tabId], or a failure if it could not be retrieved. */
    suspend fun getContent(tabId: String): Result<Content>

    companion object {
        /**
         * Creates a [ContentExtractor] that derives [Content] from the given extractors.
         *
         * A failure of the content or metadata extractor fails the returned [Result]. The article language is the one
         * the page declares in metadata, else the fallback language.
         *
         * @param pageContentExtractor Extracts the article text.
         * @param pageMetadataExtractor Extracts the metadata the declared article language is read from.
         * @param fallbackLanguageTag Provides the BCP 47 tag used when the page declares no usable language. Called
         *   each time content is retrieved, so it can follow language changes.
         */
        @Suppress("TooGenericExceptionCaught")
        fun fromPage(
            pageContentExtractor: PageContentExtractor,
            pageMetadataExtractor: PageMetadataExtractor,
            fallbackLanguageTag: () -> String,
        ) = ContentExtractor { tabId ->
            try {
                val metadata = pageMetadataExtractor.getPageMetadata(tabId).getOrThrow()
                val text = pageContentExtractor.getPageContent(tabId).getOrThrow()
                val declaredLanguageTag = metadata.languageTag.asLanguageTagOrNull()
                val content =
                    if (declaredLanguageTag != null) {
                        Content(text = text, languageTag = declaredLanguageTag, languageSource = LanguageSource.Page)
                    } else {
                        Content(
                            text = text,
                            languageTag = fallbackLanguageTag(),
                            languageSource = LanguageSource.Fallback,
                        )
                    }

                Result.success(content)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(e)
            }
        }
    }
}

/** This language tag in the form everything downstream works from, or `null` when it names no language. */
private fun String.asLanguageTagOrNull(): String? =
    trim().replace('_', '-').takeIf { Locale.forLanguageTag(it).language.isNotEmpty() }
