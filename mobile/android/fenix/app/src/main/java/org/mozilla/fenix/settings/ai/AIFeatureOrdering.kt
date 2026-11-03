/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata.FeatureId
import mozilla.components.feature.summarize.PageSummaryFeature
import org.mozilla.fenix.listentopage.ListenToPageAIControlFeature
import org.mozilla.fenix.search.VoiceSearchAIControlFeature
import org.mozilla.fenix.translations.TranslationsAIControllableFeature

/**
 * Canonical ordering of AI Controls features as they appear in the Fenix settings UI. Features not listed here sort to
 * the end and keep their relative registration order. Position in this list IS the rank — adding, removing, or
 * reordering an entry is a single-line edit.
 */
private val aiFeatureDisplayOrder: List<FeatureId> =
    listOf(
        TranslationsAIControllableFeature.id,
        PageSummaryFeature.id,
        VoiceSearchAIControlFeature.id,
        ListenToPageAIControlFeature.id,
    )

private val aiFeatureDisplayRank: Map<FeatureId, Int> =
    aiFeatureDisplayOrder.withIndex().associate { (index, id) -> id to index }

/**
 * Returns these features ordered for display in the AI Controls UI. The sort is stable: features sharing the same rank
 * (including all unranked features at the end) keep their original relative order.
 */
fun List<AIControllableFeature>.sortedForDisplay(): List<AIControllableFeature> = sortedBy {
    aiFeatureDisplayRank[it.id] ?: Int.MAX_VALUE
}
