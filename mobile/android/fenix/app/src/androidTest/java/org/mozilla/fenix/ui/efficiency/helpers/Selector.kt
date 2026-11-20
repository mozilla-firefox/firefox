/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import org.mozilla.fenix.R

data class Selector(
    val strategy: SelectorStrategy,
    val value: String,
    val secondaryValue: String? = null,
    val description: String,
    val groups: Set<SelectorGroup> = emptySet(),
    val readiness: Set<PageReadinessProfile> = emptySet(),
    val scrollDirection: SwipeDirection? = null,
    val lifecycle: SelectorLifecycle = SelectorLifecycle.Active,
    val appearsAfter: Set<SelectorId> = emptySet(),
    val id: SelectorId? = null,
) {
    fun toResourceId(): Int {
        return try {
            val rClass = R.id::class.java
            val field = rClass.getField(value)
            field.getInt(null)
        } catch (e: Exception) {
            throw IllegalArgumentException("Could not resolve resource ID for selector value: '$value' using R.id", e)
        }
    }
}

interface SelectorGroup

@JvmInline
value class SelectorId(val value: String) {
    init {
        require(value.isNotBlank()) { "Selector id cannot be blank" }
    }

    override fun toString(): String = value
}

sealed interface SelectorLifecycle {
    data object Active : SelectorLifecycle

    data class RemovedIn(val version: Int) : SelectorLifecycle {
        init {
            require(version > 0) { "Removed selector version must be positive" }
        }
    }
}

enum class SelectorStrategy {
    /** Supported strategies for locating UI elements. */
    COMPOSE_BY_CONTENT_DESCRIPTION,
    COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,

    // A node whose content description is value, inside the node tagged secondaryValue. Tells an icon-only menu
    // item apart from a toolbar button with the same content description.
    COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
    COMPOSE_BY_TAG,
    COMPOSE_BY_TAG_AND_TEXT,

    // Compose testTag combined with a content-description that merely CONTAINS secondaryValue. Mirrors
    // the legacy onNode(hasTestTag(tag)).assert(hasContentDescription(text, substring = true)) — needed
    // for a tagged row whose identifying label lives in its content description (e.g. an installed
    // webExtension.item advertising the addon name).
    COMPOSE_BY_TAG_AND_CONTENT_DESCRIPTION_SUBSTRING,
    COMPOSE_EDITABLE_BY_ANCESTOR_TAG,
    COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
    COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST,
    COMPOSE_BY_TEXT,
    COMPOSE_BY_TEXT_MERGED,
    COMPOSE_BY_TEXT_SUBSTRING,

    // A merged node whose text matches value and also contains secondaryValue, e.g. a menu row whose
    // summary carries the detail being checked.
    COMPOSE_BY_TEXT_AND_TEXT_SUBSTRING,

    // A merged node whose text matches value and whose state description is secondaryValue, e.g. a
    // switch menu row reporting "On".
    COMPOSE_BY_TEXT_AND_STATE_DESCRIPTION,

    // A clickable node whose text contains value but none of whose texts is secondaryValue. Tells a
    // submenu item apart from its header when the header's summary repeats the item's label.
    COMPOSE_CLICKABLE_BY_TEXT_SUBSTRING_WITHOUT_TEXT,
    ESPRESSO_BY_ID,
    ESPRESSO_BY_ID_WITH_DESCENDANT_TEXT,
    ESPRESSO_BY_ID_WITH_SIBLING_TEXT,
    ESPRESSO_BY_TEXT,
    ESPRESSO_BY_TEXT_WITH_SIBLING_TEXT,
    ESPRESSO_BY_CONTENT_DESC,
    ESPRESSO_BY_RES_NAME,
    UIAUTOMATOR2_BY_RES,
    UIAUTOMATOR2_BY_CLASS,
    UIAUTOMATOR2_BY_TEXT,

    // UiObject2 content-description-contains. Prefer this over UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS
    // for a control whose reaction is SLOW (opens a dialog, starts a download): UiObject (the
    // UiSelector strategies) clicks via clickAndSync, which reports failure when no window update
    // lands inside its ~5.5s budget — a slow-but-successful click is then indistinguishable from a
    // missed one. UiObject2.click() just injects the gesture and lets the caller do the waiting.
    UIAUTOMATOR2_BY_DESCRIPTION_CONTAINS,
    UIAUTOMATOR2_BY_TEXT_CONTAINS,
    UIAUTOMATOR_WITH_TEXT_CONTAINS,
    UIAUTOMATOR_WITH_RES_ID,
    UIAUTOMATOR_WITH_COMPOSE_TAG,

    // A res-id matched verbatim (no packageName prefix) together with a content-description that merely
    // CONTAINS secondaryValue: UiSelector().resourceId(value).descriptionContains(secondaryValue).
    // Mirrors the legacy itemWithResIdAndDescription(resId, description) — needed for a row (e.g. the
    // collapsed Extensions menu row, whose res-id is the "mainMenu.extensions" Compose testTag surfaced
    // via testTagsAsResourceId) whose description advertises the installed addon name.
    UIAUTOMATOR_WITH_RES_ID_AND_DESCRIPTION_CONTAINS,
    UIAUTOMATOR_WITH_TEXT,
    UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
    UIAUTOMATOR_WITH_RES_ID_AND_TEXT,

    // Package-prefixed app res-id ("$packageName:id/<value>") whose text merely CONTAINS
    // secondaryValue. Mirrors the legacy MatcherHelper.itemWithResIdContainingText — needed when an
    // app view (e.g. an autofill suggestion row) carries a longer label than the substring we key on.
    UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,

    // Raw (un-prefixed) res-id matched together with an exact text. The res-id is a web DOM id
    // (GeckoView content), not an app resource — so it is used verbatim, like UIAUTOMATOR_WITH_COMPOSE_TAG,
    // but additionally asserts the element's text equals secondaryValue. Mirrors the legacy
    // MatcherHelper.itemWithResIdAndText applied to a web form field (e.g. verifying an autofilled value).
    UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,

    // Raw (un-prefixed) res-id whose text merely CONTAINS secondaryValue. Needed for system-UI ids that
    // are not ours to prefix — e.g. a notification action button is "android:id/action0", so the
    // packageName-prefixing done by UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT would look for
    // "org.mozilla.fenix.debug:id/action0" and never match. Mirrors the legacy
    // NotificationRobot.downloadSystemNotificationButton.
    UIAUTOMATOR_WITH_RAW_RES_ID_CONTAINING_TEXT,

    // Raw (un-prefixed) res-id on its own, for a system-UI container that carries no text of its own —
    // e.g. the notification shade's scroller. UIAUTOMATOR_WITH_RAW_RES_ID_CONTAINING_TEXT cannot stand in
    // for this: its text criterion is not optional at the UiSelector level.
    UIAUTOMATOR_WITH_RAW_RES_ID,

    // A raw (un-prefixed) res-id resolved as a UiObject2. Needed for system-dialog buttons such as
    // "com.android.permissioncontroller:id/permission_deny_and_dont_ask_again_button": UIAUTOMATOR2_BY_RES
    // cannot address them because it prefixes the app's own package, and the UiObject strategies click via
    // clickAndSync, which intermittently reports a successful dialog dismissal as a failure.
    UIAUTOMATOR2_BY_RAW_RES,
}
