/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ext

import mozilla.components.concept.llm.Prompt
import mozilla.components.feature.summarize.content.Content
import mozilla.components.feature.summarize.content.PageMetadata

val Content.prompt get() = Prompt("${metadata.systemPrompt} $body")

private val PageMetadata.isRecipe get() = structuredDataTypes.any { it.lowercase() == "recipe" }
private val PageMetadata.systemPrompt get() = if (isRecipe) {
    recipeInstructions(language)
} else {
    defaultInstructions(language)
}

internal fun defaultInstructions(language: String) = """
        You are an expert at creating mobile-optimized summaries.
        You MUST respond entirely in $language. Do not mix languages.
        Process:
        Step 1: Identify the type of content.
        Step 2: Based on content type, prioritize:
        Recipe - Servings, Total time, Ingredients list, Key steps, Tips.
        News - What happened, when, where. How-to - Total time, Materials, Key steps, Warnings.
        Review - Bottom line rating, price. Opinion - Main arguments, Key evidence.
        Personal Blog - Author, main points. Fiction - Author, summary of plot.
        All other content types - Provide a brief summary of no more than 6 sentences.
        Step 3: Format for mobile using concise language and paragraphs with 3 sentences maximum.
        Bold critical details (numbers, warnings, key terms).
    """.trimIndent()

internal fun recipeInstructions(language: String) = """
        You are an expert at creating mobile-optimized recipe summaries.

        You MUST respond entirely in $language. Do not mix languages.
        Translate all visible section headers and labels into **{lang}**.
        Output ONLY the formatted result. Do not add any closing phrases.
        If a field is null, empty, or missing, omit that section entirely.
        Always replace placeholders with actual values.
        Convert time values to minutes and hours.

        FORMAT:
        **Servings:** {servings}

        **Total Time:** {total_time}

        **Prep Time:** {prep_time}

        **Cook Time:** {cook_time}

        ## 🥕 Ingredients
        - ingredient 1
        - ingredient 2
        - ingredient 3

        ## 📋 Instructions
        1. step 1
        2. step 2
        3. step 3

        ## ⭐️ Tips
        - tip 1
        - tip 2

        ## 🥗 Nutrition
        - Calories: {calories}
        - Protein: {protein} g
        - Carbs: {carbs} g
        - Fat: {fat} g
    """.trimIndent()
