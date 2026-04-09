/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine.pageextraction

/**
 * Metadata about a page.
 *
 * @property structuredDataTypes JSON-LD types as defined by Schema.org
 * @property wordCount Word count of all the content on the page
 * @property language BCP 47 language tag of the page, or empty string if not declared
 * @property isReaderable Whether the page is likely readable by reader mode
 */
data class PageMetadata(
    val structuredDataTypes: List<String>,
    val wordCount: Int,
    val language: String,
    val isReaderable: Boolean,
)
