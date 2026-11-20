/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.core

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.SemanticsNodeInteractionCollection
import androidx.compose.ui.test.filter
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withResourceName
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiSelector
import org.hamcrest.Matchers.allOf
import org.hamcrest.Matchers.containsString
import org.mozilla.fenix.ui.efficiency.helpers.Selector

/**
 * One resolver per UI toolkit, each interpreting a [Locator].
 *
 * This replaces a 295-line `when` with one arm per strategy. The arms were not different ideas: they were the same four
 * lookups with different arguments, and the duplication is what allowed two strategies to disagree about something the
 * caller never chose - which tree to search, or whether to prefix a resource id with the package.
 *
 * All four return null only for "not present". Resolver errors propagate so the host can distinguish them from absence.
 * Espresso is the exception by design: `onView` is a promise, not a lookup, so it returns an interaction that may fail
 * later at the point of use.
 */
object Resolvers {

    /**
     * Espresso returns an interaction rather than an element, so "not found" cannot be detected here - only an
     * unresolvable resource id can, which is the one case that returns null.
     */
    fun espresso(locator: Locator, selector: Selector, resourceId: () -> Int): Any? {
        val second = selector.secondaryValue ?: ""
        val base =
            when (locator.handle) {
                Handle.ID -> {
                    val id = resourceId()
                    if (id == 0) return null
                    withId(id)
                }
                Handle.TEXT -> withText(selector.value)
                Handle.DESC -> withContentDescription(selector.value)
                Handle.RES_NAME -> withResourceName(containsString(selector.value))
                else -> return null
            }
        return when (locator.extra) {
            Extra.SIBLING_TEXT -> onView(allOf(base, hasSibling(withText(second))))
            Extra.DESCENDANT_TEXT -> onView(allOf(base, hasDescendant(withText(second))))
            else -> onView(base)
        }
    }

    fun uiAutomator(device: UiDevice, packageName: String, locator: Locator, selector: Selector): Any? {
        val second = selector.secondaryValue ?: ""
        var query =
            when (locator.handle) {
                Handle.ID -> UiSelector().resourceId("$packageName:id/${selector.value}")
                Handle.RAW_ID -> UiSelector().resourceId(selector.value)
                Handle.TEXT ->
                    if (locator.match == Match.SUBSTRING) {
                        UiSelector().textContains(selector.value)
                    } else {
                        UiSelector().text(selector.value)
                    }
                Handle.DESC -> UiSelector().descriptionContains(selector.value)
                else -> return null
            }
        query =
            when (locator.extra) {
                Extra.TEXT -> query.text(second)
                Extra.TEXT_SUBSTRING -> query.textContains(second)
                Extra.DESC_SUBSTRING -> query.descriptionContains(second)
                else -> query
            }
        val obj = device.findObject(query)
        return if (obj.exists()) obj else null
    }

    fun uiAutomator2(device: UiDevice, packageName: String, locator: Locator, selector: Selector): Any? {
        val by =
            when (locator.handle) {
                Handle.ID -> By.res("$packageName:id/${selector.value}")
                Handle.RAW_ID -> By.res(selector.value)
                Handle.TEXT ->
                    if (locator.match == Match.SUBSTRING) By.textContains(selector.value) else By.text(selector.value)
                Handle.DESC -> By.descContains(selector.value)
                Handle.CLASS -> By.clazz(selector.value)
                else -> return null
            }
        return device.findObject(by)
    }

    /**
     * Resolve a Compose selector to the *displayed* match, hiding the merged-vs-unmerged tree distinction from callers.
     *
     * The other resolvers answer "which node does this selector name". This one answers "which of them is the one on
     * screen", which is what an interaction needs: text that renders in a panel and again in the address bar produces
     * two matches, and only one of them can be tapped.
     *
     * Tries the locator's declared tree first and then the other, retaining all compound selector constraints. The
     * merged/unmerged mismatch on COMPOSE_BY_TEXT is what broke navigation when this path was first introduced, and
     * content-description had the same latent trap.
     *
     * Returns null for non-Compose strategies; those go through [compose]/[espresso]/[uiAutomator].
     */
    fun displayed(
        rule: AndroidComposeTestRule<*, *>,
        locator: Locator,
        selector: Selector,
    ): SemanticsNodeInteraction? {
        val primaryUnmerged = locator.tree == Tree.UNMERGED
        val node =
            composeCandidates(rule, locator, selector, primaryUnmerged)?.let { firstDisplayed(it) }
                ?: composeCandidates(rule, locator, selector, !primaryUnmerged)?.let { firstDisplayed(it) }
        return node
    }

    private fun composeMatcher(locator: Locator, selector: Selector): SemanticsMatcher? {
        val second = selector.secondaryValue ?: ""
        val base =
            when (locator.handle) {
                Handle.TAG -> hasTestTag(selector.value)
                Handle.TEXT -> hasText(selector.value, substring = locator.match == Match.SUBSTRING)
                Handle.DESC -> hasContentDescription(selector.value, substring = locator.match == Match.SUBSTRING)
                else -> return null
            }
        return when (locator.extra) {
            Extra.TEXT -> base and hasText(second)
            Extra.TEXT_SUBSTRING -> base and hasText(second, substring = true)
            Extra.DESC_SUBSTRING -> base and hasContentDescription(second, substring = true)
            Extra.STATE_DESC -> base and SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, second)
            Extra.CLICKABLE_WITHOUT_TEXT -> base and hasClickAction() and !hasText(second)
            Extra.EDITABLE_UNDER -> hasSetTextAction() and hasAnyAncestor(hasTestTag(selector.value))
            Extra.ANCESTOR_TAG -> base and hasAnyAncestor(hasTestTag(second))
            else -> base
        }
    }

    private fun composeCandidates(
        rule: AndroidComposeTestRule<*, *>,
        locator: Locator,
        selector: Selector,
        unmerged: Boolean,
    ): SemanticsNodeInteractionCollection? {
        val matcher = composeMatcher(locator, selector) ?: return null
        val candidates = rule.onAllNodes(matcher, useUnmergedTree = unmerged)
        return if (locator.extra == Extra.CHILD_TEXT) {
            candidates.filter(hasAnyChild(hasText(selector.secondaryValue ?: "")))
        } else {
            candidates
        }
    }

    /** The first *displayed* node in a collection, else the first node, else null. */
    private fun firstDisplayed(collection: SemanticsNodeInteractionCollection): SemanticsNodeInteraction? {
        val count = collection.fetchSemanticsNodes().size
        if (count == 0) return null
        for (i in 0 until count) {
            val node = collection[i]
            if (ElementState.probe(ComposeUiElement(node), ElementState.Trait.DISPLAYED)) return node
        }
        return collection[0]
    }
}
