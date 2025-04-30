/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers.perf

import shark.AndroidReferenceMatchers.Companion.instanceFieldLeak
import shark.ReferenceMatcher

/**
 * Matchers to suppress known memory leaks, especially ones from 3rd party libraries & frameworks
 *
 * @property builder a builder lambda that takes a [MutableList] of [ReferenceMatcher] that can
 * be used to build the list of references to match the [FenixReferenceMatchers] entry.
 */
private enum class FenixReferenceMatchers(
    val builder: (references: MutableList<ReferenceMatcher>) -> Unit,
) {

    /**
     * Leaks discovered related to PopupLayout.
     *
     * Related google issues are:
     * 1. [https://issuetracker.google.com/issues/296891215#comment5](https://issuetracker.google.com/issues/296891215#comment5)
     * 2. [https://issuetracker.google.com/issues/274016293](https://issuetracker.google.com/issues/274016293)
     */
    COMPOSE_POPUP_LAYOUT(
        builder = { references ->
            references += instanceFieldLeak(
                className = "androidx.compose.ui.node.LayoutNode",
                fieldName = "nodes",
            )
            references += instanceFieldLeak(
                className = "androidx.compose.ui.node.LayoutNode",
                fieldName = "_modifier",
            )
            references += instanceFieldLeak(
                className = "androidx.compose.ui.node.LayoutNode",
                fieldName = "measurePolicy",
            )
            references += instanceFieldLeak(
                className = "androidx.compose.ui.node.LayoutNode",
                fieldName = "intrinsicsPolicy",
            )
        },
    ),
}

/**
 * Builds the list of [ReferenceMatcher] known memory leaks.
 */
val knownLeaks: List<ReferenceMatcher>
    get() {
        val references = mutableListOf<ReferenceMatcher>()
        FenixReferenceMatchers.entries.forEach {
            it.builder(references)
        }
        return references.toList()
    }
