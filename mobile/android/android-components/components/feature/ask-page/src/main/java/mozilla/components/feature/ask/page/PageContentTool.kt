/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.concept.llm.LlmTool

/**
 * An [LlmTool] that returns the text content of the page the user is currently viewing.
 *
 * The tool takes no model-supplied arguments; the page content is retrieved by invoking
 * [getContent] at execution time.
 *
 * @param getContent A suspend function that returns the page's text content.
 *   Throw to signal a retrieval failure, which will propagate out of [LlmSession.send].
 */
class PageContentTool(
    private val getContent: suspend () -> String,
) : LlmTool {
    override val name = "get_page_content"
    override val description = "Returns the text content of the web page the user is currently viewing."
    override val parametersSchema = """{"type":"object","properties":{}}"""

    override suspend fun execute(arguments: String): String = getContent()
}
