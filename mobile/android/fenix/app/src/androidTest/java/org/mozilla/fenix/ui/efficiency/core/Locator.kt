/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.core

import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

/**
 * What a [SelectorStrategy] actually asks for, as data.
 *
 * The strategies are not all separate ideas. They are the product of five small ones:
 *
 * layer which UI toolkit answers the question handle what to match on match exactly, or as a substring tree Compose
 * only: the merged semantics tree or the unmerged one pick Compose only: the single node, or the first of all matches
 * extra an optional second clause (a sibling's text, a child's text, ...)
 *
 * Resolution used to be a 295-line `when` with one arm per combination, which is why adding a strategy meant adding an
 * arm, and why several arms differed only in a detail the caller should never have needed to know -
 * UIAUTOMATOR_WITH_RES_ID prefixes the package and UIAUTOMATOR_WITH_RAW_RES_ID does not, and choosing wrong finds
 * nothing silently.
 *
 * The table below is deliberately dull and long: it is the whole compatibility surface, so every existing catalog keeps
 * working untouched. The LOGIC it feeds is four small resolvers.
 */
data class Locator(
    val layer: Layer,
    val handle: Handle,
    val match: Match = Match.EXACT,
    val tree: Tree = Tree.MERGED,
    val pick: Pick = Pick.ONE,
    val extra: Extra = Extra.NONE,
)

enum class Layer {
    COMPOSE,
    ESPRESSO,
    UIAUTOMATOR,
    UIAUTOMATOR2,
}

/** RAW_ID is the id as written; ID is prefixed with the app's package, as Android reports it. */
enum class Handle {
    TAG,
    ID,
    RAW_ID,
    TEXT,
    DESC,
    CLASS,
    RES_NAME,
}

enum class Match {
    EXACT,
    SUBSTRING,
}

enum class Tree {
    MERGED,
    UNMERGED,
}

enum class Pick {
    ONE,
    FIRST_OF_ALL,
}

/** A second clause, matched against [org.mozilla.fenix.ui.efficiency.helpers.Selector.secondaryValue]. */
enum class Extra {
    NONE,
    TEXT,
    TEXT_SUBSTRING,
    DESC_SUBSTRING,
    STATE_DESC,
    CLICKABLE_WITHOUT_TEXT,
    SIBLING_TEXT,
    DESCENDANT_TEXT,
    CHILD_TEXT,
    EDITABLE_UNDER,
    ANCESTOR_TAG,
}

/**
 * Every strategy, as a locator. A strategy absent from this map has no descriptor form and is resolved by its own
 * code - see BasePage.mozGetElement. Keeping the map total is the goal; an entry here is what makes a strategy
 * ordinary.
 */
val STRATEGY_LOCATORS: Map<SelectorStrategy, Locator> =
    mapOf(
        // Compose
        SelectorStrategy.COMPOSE_BY_TAG to Locator(Layer.COMPOSE, Handle.TAG),
        SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST to
            Locator(Layer.COMPOSE, Handle.TAG, pick = Pick.FIRST_OF_ALL),
        SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST to
            Locator(Layer.COMPOSE, Handle.TAG, pick = Pick.FIRST_OF_ALL, extra = Extra.CHILD_TEXT),
        SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT to
            Locator(Layer.COMPOSE, Handle.TAG, tree = Tree.UNMERGED, extra = Extra.TEXT),
        SelectorStrategy.COMPOSE_BY_TAG_AND_CONTENT_DESCRIPTION_SUBSTRING to
            Locator(Layer.COMPOSE, Handle.TAG, extra = Extra.DESC_SUBSTRING),
        SelectorStrategy.COMPOSE_EDITABLE_BY_ANCESTOR_TAG to
            Locator(Layer.COMPOSE, Handle.TAG, extra = Extra.EDITABLE_UNDER),
        SelectorStrategy.COMPOSE_BY_TEXT to Locator(Layer.COMPOSE, Handle.TEXT, tree = Tree.UNMERGED),
        SelectorStrategy.COMPOSE_BY_TEXT_MERGED to Locator(Layer.COMPOSE, Handle.TEXT),
        SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING to
            Locator(Layer.COMPOSE, Handle.TEXT, Match.SUBSTRING, Tree.UNMERGED, Pick.FIRST_OF_ALL),
        SelectorStrategy.COMPOSE_BY_TEXT_AND_TEXT_SUBSTRING to
            Locator(Layer.COMPOSE, Handle.TEXT, extra = Extra.TEXT_SUBSTRING),
        SelectorStrategy.COMPOSE_BY_TEXT_AND_STATE_DESCRIPTION to
            Locator(Layer.COMPOSE, Handle.TEXT, extra = Extra.STATE_DESC),
        SelectorStrategy.COMPOSE_CLICKABLE_BY_TEXT_SUBSTRING_WITHOUT_TEXT to
            Locator(Layer.COMPOSE, Handle.TEXT, Match.SUBSTRING, extra = Extra.CLICKABLE_WITHOUT_TEXT),
        SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION to Locator(Layer.COMPOSE, Handle.DESC),
        SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING to
            Locator(Layer.COMPOSE, Handle.DESC, Match.SUBSTRING),
        SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG to
            Locator(Layer.COMPOSE, Handle.DESC, extra = Extra.ANCESTOR_TAG),

        // Espresso
        SelectorStrategy.ESPRESSO_BY_ID to Locator(Layer.ESPRESSO, Handle.ID),
        SelectorStrategy.ESPRESSO_BY_ID_WITH_DESCENDANT_TEXT to
            Locator(Layer.ESPRESSO, Handle.ID, extra = Extra.DESCENDANT_TEXT),
        SelectorStrategy.ESPRESSO_BY_ID_WITH_SIBLING_TEXT to
            Locator(Layer.ESPRESSO, Handle.ID, extra = Extra.SIBLING_TEXT),
        SelectorStrategy.ESPRESSO_BY_TEXT to Locator(Layer.ESPRESSO, Handle.TEXT),
        SelectorStrategy.ESPRESSO_BY_TEXT_WITH_SIBLING_TEXT to
            Locator(Layer.ESPRESSO, Handle.TEXT, extra = Extra.SIBLING_TEXT),
        SelectorStrategy.ESPRESSO_BY_CONTENT_DESC to Locator(Layer.ESPRESSO, Handle.DESC),
        SelectorStrategy.ESPRESSO_BY_RES_NAME to Locator(Layer.ESPRESSO, Handle.RES_NAME, Match.SUBSTRING),

        // UiAutomator (UiSelector)
        SelectorStrategy.UIAUTOMATOR_WITH_RES_ID to Locator(Layer.UIAUTOMATOR, Handle.ID),
        SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT to Locator(Layer.UIAUTOMATOR, Handle.ID, extra = Extra.TEXT),
        SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT to
            Locator(Layer.UIAUTOMATOR, Handle.ID, extra = Extra.TEXT_SUBSTRING),
        SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_DESCRIPTION_CONTAINS to
            Locator(Layer.UIAUTOMATOR, Handle.RAW_ID, extra = Extra.DESC_SUBSTRING),
        SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID to Locator(Layer.UIAUTOMATOR, Handle.RAW_ID),
        SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG to Locator(Layer.UIAUTOMATOR, Handle.RAW_ID),
        SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID_CONTAINING_TEXT to
            Locator(Layer.UIAUTOMATOR, Handle.RAW_ID, extra = Extra.TEXT_SUBSTRING),
        SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT to
            Locator(Layer.UIAUTOMATOR, Handle.RAW_ID, extra = Extra.TEXT),
        SelectorStrategy.UIAUTOMATOR_WITH_TEXT to Locator(Layer.UIAUTOMATOR, Handle.TEXT),
        SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS to Locator(Layer.UIAUTOMATOR, Handle.TEXT, Match.SUBSTRING),
        SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS to
            Locator(Layer.UIAUTOMATOR, Handle.DESC, Match.SUBSTRING),

        // UiAutomator2 (By)
        SelectorStrategy.UIAUTOMATOR2_BY_RES to Locator(Layer.UIAUTOMATOR2, Handle.ID),
        SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES to Locator(Layer.UIAUTOMATOR2, Handle.RAW_ID),
        SelectorStrategy.UIAUTOMATOR2_BY_TEXT to Locator(Layer.UIAUTOMATOR2, Handle.TEXT),
        SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS to Locator(Layer.UIAUTOMATOR2, Handle.TEXT, Match.SUBSTRING),
        SelectorStrategy.UIAUTOMATOR2_BY_DESCRIPTION_CONTAINS to
            Locator(Layer.UIAUTOMATOR2, Handle.DESC, Match.SUBSTRING),
        SelectorStrategy.UIAUTOMATOR2_BY_CLASS to Locator(Layer.UIAUTOMATOR2, Handle.CLASS),
    )
